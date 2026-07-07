#
# Makefile -- Build system for the TH8 interpreter.
#
# Targets:
#   all             Build everything possible.
#   static          Build libth8.a only.
#   shared          Build libth8.so (or .dylib on macOS) only.
#   stubs           Build libth8stub.a only.
#   shell           Build th8sh (interactive shell with bestline).
#   static-shell    Build th8shs (statically linked interactive shell).
#   testlib         Build libth8test.so and/or libtclth8test.so.
#   bridge          Build libth8bridge_tcl.so and/or libth8bridge_th8.so.
#   clean           Remove all build artifacts.
#   th8test         Run test suite in TH8.
#   tcltest         Run test suite in Tcl.
#   eagletest       Run test suite in Eagle.
#   install         Install libraries, headers, and man page.
#
# Directory layout:
#   src/                 All C source and headers
#   externals/regex/     PostgreSQL Spencer regex engine
#   externals/bestline/  bestline readline replacement
#   externals/spilornis/ Eagle list parser
#   bin/                 Build artifacts (created by make)
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

.PHONY: all static shared stubs shell static-shell testlib bridge \
        genstubs audit audit-reqs regex_vendor bestline_vendor tommath_vendor clean install debug FORCE \
        th8test tcltest eagletest \
        amalgamation amalgamation-test \
        asan ubsan msan sanitize asan-test asan-test-macos \
        sanitize-test sanitize-test-macos valgrind check-crt check-crt-debug \
        coverage coverage-report coverage-branches coverage-clean \
        mcdc mcdc-report mcdc-uncovered mcdc-clean \
        fuzz-corpus fuzz fuzz-eval fuzz-expr fuzz-list fuzz-harpy \
        fuzz-snk fuzz-format \
        afl-run-all afl-run-eval afl-run-expr afl-run-list \
        afl-run-format afl-run-harpy afl-run-snk \
        afl-status afl-stop \
        profile-shell profile profile-mimalloc-shell profile-mimalloc

#
# Source and build output directories.
#

S = src/
B ?= bin/

#
# Canonical version source.  The VERSION file at the project root is
# read once here (e.g. "1.0.0") and used wherever the build needs to
# stamp a binary with version metadata -- currently macOS dylib
# -current_version / -compatibility_version flags.  The same file
# also feeds tools/mkversion.tcl which generates the C-side
# TH8_PATCH_LEVEL / TH8_RC_VERSION / TH8_RC_PATCH_LEVEL macros, so
# every version stamp on every platform traces back to one source.
# Falls back to "0.0.0" if the file is unreadable (defensive).
#

TH8_PATCH_LEVEL := $(shell cat VERSION 2>/dev/null || echo 0.0.0)

#
# AFL binary support.
#

AFL_B = bin-afl/
AFL_FUZZ_OUT ?= $(AFL_B)$(FUZZ_DIR)

#
# Static shell support.
#

STATIC_BUILD ?= 0
STATIC_B = bin-static/

#
# Platform detection.
#
# TH8_TARGET selects the build target.  Default "host" uses uname
# to pick a native build matching the build machine.  Setting it
# to "ios" or "android" enables a cross-compile to the named
# mobile platform; the corresponding toolchain (Xcode for iOS, the
# Android NDK for Android) must be available on the build host.
#

TH8_TARGET ?= host

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)

#
# TCLSH: path to a Tcl 8.6 interpreter for tools/*.tcl invocations.
# `tools/format_code.tcl` and `tools/format_scripts.tcl` explicitly
# `package require Tcl 8.6`; the macOS system `tclsh` is 8.5.9 which
# fails those scripts.  Auto-detect a 8.6 binary; embedders can
# override with `make TCLSH=/path/to/tclsh ...`.
#
TCLSH ?= $(shell command -v /opt/homebrew/opt/tcl-tk@8/bin/tclsh 2>/dev/null \
                 || command -v tclsh8.6 2>/dev/null \
                 || command -v tclsh)

# ---- iOS cross-compile (Xcode toolchain on macOS host) -------------
ifeq ($(TH8_TARGET),ios)
  # Pick the SDK -- "iphoneos" for device builds, "iphonesimulator"
  # for simulator builds.  IOS_SDK selects which one; default is the
  # device SDK so a plain "make TH8_TARGET=ios" produces an arm64
  # device library.
  IOS_SDK     ?= iphoneos
  # arm64 for device, arm64+x86_64 for the simulator (universal).
  ifeq ($(IOS_SDK),iphonesimulator)
    IOS_ARCHS ?= arm64 x86_64
  else
    IOS_ARCHS ?= arm64
  endif
  # Deployment target floor: 13.0 covers all currently-supported
  # devices and gives us os_log, arc4random_buf, and modern dlopen
  # without weak-linking checks.
  IOS_MIN_VERSION ?= 13.0

  XCRUN       := xcrun --sdk $(IOS_SDK)
  CC          := $(shell $(XCRUN) --find clang)
  IOS_SDKPATH := $(shell $(XCRUN) --show-sdk-path)
  IOS_ARCH_FLAGS := $(foreach a,$(IOS_ARCHS),-arch $(a))
  ifeq ($(IOS_SDK),iphonesimulator)
    IOS_VERSION_FLAG := -mios-simulator-version-min=$(IOS_MIN_VERSION)
  else
    IOS_VERSION_FLAG := -miphoneos-version-min=$(IOS_MIN_VERSION)
  endif
  CFLAGS      += -isysroot $(IOS_SDKPATH) $(IOS_ARCH_FLAGS) \
                 $(IOS_VERSION_FLAG)
  # iOS apps embed the static library; no .dylib target.
  SHLIB_EXT    = .a
  SHLIB_FLAGS  =
  PLAT_SRC     = th8_posix.c
  MACOS_SRC    = th8_macos.c
  IOS_SRC      = th8_ios.c
  PLAT_LIBS    =

# ---- Android cross-compile (NDK on Linux or macOS host) -----------
else ifeq ($(TH8_TARGET),android)
  ANDROID_NDK ?= $(error TH8_TARGET=android requires ANDROID_NDK \
                          to be set to your NDK install root)
  # API floor 21 (Android 5.0 Lollipop) maximises device coverage;
  # API 28 unlocks getrandom(2).
  ANDROID_API ?= 21
  # ABI choices: arm64-v8a (default; covers ~all modern phones),
  # armeabi-v7a (32-bit ARM), x86_64 (emulator), x86 (older emu).
  ANDROID_ABI ?= arm64-v8a

  ANDROID_HOSTTAG := $(shell uname -s | tr A-Z a-z)-x86_64
  ANDROID_TC      := $(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOSTTAG)/bin

  # Map ABI to clang triple.
  ifeq ($(ANDROID_ABI),arm64-v8a)
    ANDROID_TRIPLE := aarch64-linux-android
  else ifeq ($(ANDROID_ABI),armeabi-v7a)
    ANDROID_TRIPLE := armv7a-linux-androideabi
  else ifeq ($(ANDROID_ABI),x86_64)
    ANDROID_TRIPLE := x86_64-linux-android
  else ifeq ($(ANDROID_ABI),x86)
    ANDROID_TRIPLE := i686-linux-android
  else
    $(error Unknown ANDROID_ABI=$(ANDROID_ABI); use one of \
            arm64-v8a, armeabi-v7a, x86_64, x86)
  endif

  CC          := $(ANDROID_TC)/$(ANDROID_TRIPLE)$(ANDROID_API)-clang
  CFLAGS      += -fPIC
  SHLIB_EXT    = .so
  SHLIB_FLAGS  = -shared -Wl,-soname,libth8$(SHLIB_EXT)
  PLAT_SRC     = th8_posix.c
  ANDROID_SRC  = th8_android.c
  # -ldl: dlopen / -llog: __android_log_print
  PLAT_LIBS    = -ldl -llog

# ---- Native host builds (existing behaviour, unchanged) -----------
else ifeq ($(UNAME_S),Darwin)
  SHLIB_EXT    = .dylib
  # -current_version / -compatibility_version are the macOS dylib
  # equivalent of the Win32 VERSIONINFO FILEVERSION / PRODUCTVERSION
  # fields.  Visible to `otool -L` and `dyld_info`; consulted by the
  # dyld loader to verify a client's runtime requirements against
  # the installed dylib.  Format: major[.minor[.patch]], integers.
  SHLIB_FLAGS  = -dynamiclib -install_name @rpath/libth8$(SHLIB_EXT) \
                 -current_version $(TH8_PATCH_LEVEL) \
                 -compatibility_version $(TH8_PATCH_LEVEL)
  PLAT_SRC = th8_posix.c
  MACOS_SRC    = th8_macos.c
  PLAT_LIBS =
else ifeq ($(UNAME_S),Linux)
  SHLIB_EXT    = .so
  SHLIB_FLAGS  = -shared -Wl,-soname,libth8$(SHLIB_EXT)
  PLAT_SRC = th8_posix.c
  PLAT_LIBS = -ldl -lpthread
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  SHLIB_EXT    = .dll
  SHLIB_FLAGS  = -shared
  PLAT_SRC = th8_win32.c
  PLAT_LIBS =
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  SHLIB_EXT    = .dll
  SHLIB_FLAGS  = -shared
  PLAT_SRC = th8_win32.c
  PLAT_LIBS =
else
  SHLIB_EXT    = .so
  SHLIB_FLAGS  = -shared
  PLAT_SRC = th8_posix.c
  PLAT_LIBS = -ldl -lpthread
endif

#
# Toolchain.
#

CC       ?= cc
AR       ?= ar
RANLIB   ?= ranlib

#
# Compiler flags.
#

CFLAGS_BASE = \
  -std=c99 \
  -pedantic \
  -Wall \
  -Wextra \
  -Wdeclaration-after-statement \
  -Wstrict-prototypes \
  -Wmissing-prototypes \
  -Wold-style-definition \
  -Wno-long-long \
  -Wno-unused-parameter \
  -ffp-contract=off

#
# Optional features.  Set ENABLE_REGEXP=0 to exclude regex support.
#

ENABLE_REGEXP ?= 1

ifeq ($(ENABLE_REGEXP),1)
  REGEXP_DEFS = -DTH8_ENABLE_REGEXP
else
  REGEXP_DEFS =
endif

ENABLE_BIGINT ?= 1

ifeq ($(ENABLE_BIGINT),1)
  BIGINT_DEFS    = -DTH8_ENABLE_BIGINT
  BIGINT_OBJ     = $(B)th8_bigint.o $(B)tommath_amalg.o
  BIGINT_OBJ_PIC = $(B)th8_bigint.pic.o $(B)tommath_amalg.pic.o
  BIGINT_INCLUDES = -I$(TOMMATH_BUILD)
else
  BIGINT_DEFS    =
  BIGINT_OBJ     =
  BIGINT_OBJ_PIC =
  BIGINT_INCLUDES =
endif

ENABLE_CRYPTOGRAPHY ?= 1

ifeq ($(ENABLE_CRYPTOGRAPHY),1)
  CRYPTOGRAPHY_DEFS     = -DTH8_ENABLE_CRYPTOGRAPHY
  ifeq ($(UNAME_S),Darwin)
    OPENSSL_PREFIX = $(shell brew --prefix openssl@3 2>/dev/null || echo /usr/local/opt/openssl)
    CRYPTOGRAPHY_INCLUDES = -I$(OPENSSL_PREFIX)/include
    CRYPTOGRAPHY_LIBS     = -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
  else
    CRYPTOGRAPHY_INCLUDES = $(shell pkg-config --cflags-only-I openssl 2>/dev/null)
    ifeq ($(STATIC_BUILD),1)
      CRYPTOGRAPHY_LIBS   := $(shell pkg-config --libs --static openssl 2>/dev/null || echo "-lssl -lcrypto")
    else
      CRYPTOGRAPHY_LIBS   := $(shell pkg-config --libs openssl 2>/dev/null || echo "-lssl -lcrypto")
    endif
  endif
  CRYPTOGRAPHY_OBJ      = $(B)th8_snk.o $(B)th8_harpy.o $(B)th8_policy.o $(B)th8_keyRoot.o $(B)th8_key0.o $(B)th8_keyTime.o $(B)th8_keyTest.o $(B)th8_keyring_stub.o $(B)th8_secure.o $(B)th8_protect.o $(B)th8_time.o $(B)th8_attrflags.o $(B)th8_crypto_cmds.o $(B)th8_harpy_cmds.o
  CRYPTOGRAPHY_OBJ_PIC  = $(B)th8_snk.pic.o $(B)th8_harpy.pic.o $(B)th8_policy.pic.o $(B)th8_keyRoot.pic.o $(B)th8_key0.pic.o $(B)th8_keyTime.pic.o $(B)th8_keyTest.pic.o $(B)th8_keyring_stub.pic.o $(B)th8_secure.pic.o $(B)th8_protect.pic.o $(B)th8_time.pic.o $(B)th8_attrflags.pic.o $(B)th8_crypto_cmds.pic.o $(B)th8_harpy_cmds.pic.o
else
  CRYPTOGRAPHY_DEFS     =
  CRYPTOGRAPHY_INCLUDES =
  CRYPTOGRAPHY_OBJ      =
  CRYPTOGRAPHY_OBJ_PIC  =
  CRYPTOGRAPHY_LIBS     =
endif

#
# libunbound for DNSSEC-validated DNS resolution (optional).
# When enabled, NTP server lookups are cryptographically validated
# through the full DNSSEC chain of trust using the IANA root anchor.
# Also, the various static libraries it would require for static
# builds are not widely available; hence, it should be disabled in
# those cases.
#

ifeq ($(STATIC_BUILD),1)
  ENABLE_UNBOUND ?= 0
else
  ENABLE_UNBOUND ?= 1
endif

ifeq ($(ENABLE_UNBOUND),1)
  UNBOUND_OBJ     = $(B)th8_unbound.o
  UNBOUND_OBJ_PIC = $(B)th8_unbound.pic.o
  ifeq ($(UNAME_S),Darwin)
    UNBOUND_PREFIX = $(shell brew --prefix unbound 2>/dev/null || echo /opt/homebrew)
    UNBOUND_DEFS     = -DTH8_ENABLE_UNBOUND
    UNBOUND_INCLUDES = -I$(UNBOUND_PREFIX)/include
    UNBOUND_LIBS     = -L$(UNBOUND_PREFIX)/lib -lunbound
  else
    UNBOUND_DEFS     = -DTH8_ENABLE_UNBOUND
    UNBOUND_INCLUDES = $(shell pkg-config --cflags-only-I libunbound 2>/dev/null)
    ifeq ($(STATIC_BUILD),1)
      UNBOUND_LIBS  := $(shell pkg-config --libs --static libunbound 2>/dev/null || echo "-lunbound -levent -lhogweed -lnettle -lgmp")
    else
      UNBOUND_LIBS  := $(shell pkg-config --libs libunbound 2>/dev/null || echo "-lunbound -levent -lhogweed -lnettle -lgmp")
    endif
  endif
else
  UNBOUND_OBJ      =
  UNBOUND_OBJ_PIC  =
  UNBOUND_DEFS     =
  UNBOUND_INCLUDES =
  UNBOUND_LIBS     =
endif

ENABLE_LIBCURL ?= 1

ifeq ($(ENABLE_LIBCURL),1)
  CURL_DEFS    = -DTH8_ENABLE_LIBCURL
  CURL_OBJ     = $(B)th8_curl.o
  CURL_OBJ_PIC = $(B)th8_curl.pic.o
  #
  # Use curl-config to get the full set of libraries that libcurl
  # was built against (includes OpenSSL, zlib, etc. as needed).
  # For static linking, --static-libs includes transitive deps
  # (e.g. -lnghttp2, -lzstd, -lbrotlidec) that --libs omits.
  # Fall back to a reasonable default if the tool isn't available.
  #
  ifeq ($(STATIC_BUILD),1)
    CURL_LIBS  := $(shell curl-config --static-libs 2>/dev/null || curl-config --libs 2>/dev/null || echo "-lcurl -lssl -lcrypto")
  else
    CURL_LIBS  := $(shell curl-config --libs 2>/dev/null || echo "-lcurl -lssl -lcrypto")
  endif
else
  CURL_DEFS    =
  CURL_OBJ     =
  CURL_OBJ_PIC =
  CURL_LIBS    =
endif

#
# mimalloc high-performance allocator (optional).
# When enabled, provides Th8_GetMimallocPlatform() backed by
# Microsoft's mimalloc.  Gated on TH8_USE_MIMALLOC.
#

ENABLE_MIMALLOC ?= 1

ifeq ($(ENABLE_MIMALLOC),1)
  MIMALLOC_DEFS    = -DTH8_USE_MIMALLOC
  MIMALLOC_INC     = -Iexternals/mimalloc/include
  MIMALLOC_OBJ     = $(B)th8_mimalloc.o $(B)mimalloc_static.o
  MIMALLOC_OBJ_PIC = $(B)th8_mimalloc.pic.o $(B)mimalloc_static.pic.o
else
  MIMALLOC_DEFS    =
  MIMALLOC_INC     =
  MIMALLOC_OBJ     =
  MIMALLOC_OBJ_PIC =
endif

ENABLE_TEST_KEY ?= 0

ifeq ($(ENABLE_TEST_KEY),1)
  TEST_DEFS = -DTH8_ENABLE_TEST_KEY
else
  TEST_DEFS =
endif

ENABLE_FAULT_INJECTION ?= 1

ifeq ($(ENABLE_FAULT_INJECTION),1)
  FAULT_DEFS = -DTH8_ENABLE_FAULT_INJECTION
else
  FAULT_DEFS =
endif

ENABLE_BESTLINE ?= 1

ifeq ($(ENABLE_BESTLINE),1)
  BESTLINE_DEFS = -DTH8_USE_BESTLINE
else
  BESTLINE_DEFS =
endif

USE_AMALGAMATION ?= 0

#
# Generated version header.
#

VERSIONHDR = $(B)th8_version_gen.h

#
# Compile-time hardening:
#   -fstack-protector-strong: Stack canary on functions with arrays/alloca.
#   -D_FORTIFY_SOURCE=2: Bounds-checked CRT replacements (requires -O1+).
# Link-time hardening (POSIX only, not macOS):
#   -Wl,-z,relro,-z,now: Full RELRO (read-only GOT after relocation).
#
HARDEN_CFLAGS  = -fstack-protector-strong -D_FORTIFY_SOURCE=2
ifeq ($(UNAME_S),Linux)
  HARDEN_LDFLAGS = -Wl,-z,relro,-z,now
else
  HARDEN_LDFLAGS =
endif

#
# Plugin compile-time gates.  Each plugin can be excluded by setting
# the corresponding variable to 0 on the make command line, e.g.:
#   make PLUGIN_LOOPING=0 PLUGIN_FORMATTING=0 fresh
# All plugins are enabled by default.
#

#
# Feature dependency cascade.  Disabling a core feature
# automatically disables the plugins that require it.
#

ENABLE_EXPRESSIONS ?= 1
ENABLE_LOAD ?= 1
ENABLE_VARIABLES ?= 1

ifeq ($(ENABLE_VARIABLES),1)
  VARIABLES_DEFS = -DTH8_ENABLE_VARIABLES
else
  VARIABLES_DEFS =
  override PLUGIN_VARIABLES    := 0
endif

ifeq ($(ENABLE_LOAD),1)
  LOAD_DEFS = -DTH8_ENABLE_LOAD
else
  LOAD_DEFS =
  override PLUGIN_EXTENSIBILITY := 0
endif

ifeq ($(ENABLE_EXPRESSIONS),1)
  EXPR_DEFS = -DTH8_ENABLE_EXPRESSIONS
else
  EXPR_DEFS =
  # Plugins that require the expression engine.
  override PLUGIN_CONTROL       := 0
  override PLUGIN_EXPRESSIONS   := 0
  override PLUGIN_FORMATTING    := 0
  override PLUGIN_LOOPING       := 0
endif

PLUGIN_BINARY        ?= 1
PLUGIN_CONTROL       ?= 1
PLUGIN_EXPRESSIONS   ?= 1
PLUGIN_EXTENSIBILITY ?= 1
PLUGIN_FILE_SYSTEMS  ?= 1
PLUGIN_FORMATTING    ?= 1
PLUGIN_INTROSPECTION ?= 1
PLUGIN_IO            ?= 1
PLUGIN_LISTS         ?= 1
PLUGIN_LOOPING       ?= 1
PLUGIN_MANAGEMENT    ?= 1
PLUGIN_PROCEDURES    ?= 1
PLUGIN_STRINGS       ?= 1
PLUGIN_TIMEKEEPING   ?= 1
PLUGIN_VARIABLES     ?= 1
PLUGIN_EVENTS        ?= 1

PLUGIN_DEFS =
ifeq ($(PLUGIN_BINARY),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_BINARY
endif
ifeq ($(PLUGIN_CONTROL),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_CONTROL
endif
ifeq ($(PLUGIN_EVENTS),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_EVENTS
endif
ifeq ($(PLUGIN_EXPRESSIONS),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_EXPRESSIONS
endif
ifeq ($(PLUGIN_EXTENSIBILITY),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_EXTENSIBILITY
endif
ifeq ($(PLUGIN_FILE_SYSTEMS),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_FILE_SYSTEMS
endif
ifeq ($(PLUGIN_FORMATTING),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_FORMATTING
endif
ifeq ($(PLUGIN_INTROSPECTION),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_INTROSPECTION
endif
ifeq ($(PLUGIN_IO),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_IO
endif
ifeq ($(PLUGIN_LISTS),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_LISTS
endif
ifeq ($(PLUGIN_LOOPING),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_LOOPING
endif
ifeq ($(PLUGIN_MANAGEMENT),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_MANAGEMENT
endif
ifeq ($(PLUGIN_PROCEDURES),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_PROCEDURES
endif
ifeq ($(PLUGIN_STRINGS),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_STRINGS
endif
ifeq ($(PLUGIN_TIMEKEEPING),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_TIMEKEEPING
endif
ifeq ($(PLUGIN_VARIABLES),1)
  PLUGIN_DEFS += -DTH8_PLUGIN_VARIABLES
endif

CFLAGS_RELEASE = $(CFLAGS_BASE) $(REGEXP_DEFS) $(CURL_DEFS) $(BIGINT_DEFS) $(CRYPTOGRAPHY_DEFS) $(UNBOUND_DEFS) $(EXPR_DEFS) $(LOAD_DEFS) $(VARIABLES_DEFS) $(PLUGIN_DEFS) $(MIMALLOC_DEFS) $(BESTLINE_DEFS) $(TEST_DEFS) $(FAULT_DEFS) $(HARDEN_CFLAGS) -O2 -DTH8_BENCHMARKING -DNDEBUG
CFLAGS_DEBUG   = $(CFLAGS_BASE) $(REGEXP_DEFS) $(CURL_DEFS) $(BIGINT_DEFS) $(CRYPTOGRAPHY_DEFS) $(UNBOUND_DEFS) $(EXPR_DEFS) $(LOAD_DEFS) $(VARIABLES_DEFS) $(PLUGIN_DEFS) $(MIMALLOC_DEFS) $(BESTLINE_DEFS) $(TEST_DEFS) $(FAULT_DEFS) $(HARDEN_CFLAGS) -g -O0 -DTH8_BENCHMARKING -DTH8_DEBUG

EXTRA_CFLAGS  ?=
EXTRA_LDFLAGS ?=
CFLAGS  ?= $(CFLAGS_RELEASE) $(EXTRA_CFLAGS)

#
# Common include path: source dir + build dir (for generated header).
#

UTF_DIR    = externals/utf
#
# External system library includes (OpenSSL, unbound, mimalloc).
# Used directly by the amalgamation compile (proving th8.c + th8.h
# are self-contained) and folded into INCLUDES for non-amalgamation.
#
EXTLIB_INCLUDES = $(CRYPTOGRAPHY_INCLUDES) $(UNBOUND_INCLUDES) $(MIMALLOC_INC)

#
# TH8-internal include paths + vendored externals + system libraries.
#
INCLUDES   = -I$(S) -I$(B) -I$(S)test -I$(UTF_DIR) -I$(SPILORNIS_DIR) $(BIGINT_INCLUDES) $(EXTLIB_INCLUDES)

#
# Installation directories.
#

PREFIX  ?= /usr/local
LIBDIR   = $(PREFIX)/lib
INCDIR   = $(PREFIX)/include
BINDIR   = $(PREFIX)/bin
MANROOT  = $(PREFIX)/share/man
MAN1DIR  = $(MANROOT)/man1
MAN3DIR  = $(MANROOT)/man3
DATADIR  = $(PREFIX)/share/th8

#
# Man-page sources (one logical list, split by section so each lands
# in the correct $(MAN[N]DIR) at install time).  Section 1 = user-
# facing commands (the th8 shell, syntax references); section 3 =
# library functions (the C API).  All sources live under docs/public/.
#

MAN1_SRCS = docs/public/th8.1 docs/public/th8_re_syntax.1
MAN3_SRCS = docs/public/th8_api.3
MAN_SRCS  = $(MAN1_SRCS) $(MAN3_SRCS)

#
# External source locations.
#

SPILORNIS_DIR    = externals/spilornis
SPILORNIS_SRC    = $(SPILORNIS_DIR)/Spilornis.c
BESTLINE_BUILD   = externals/bestline/build
REGEX_BUILD      = externals/regex/build
TOMMATH_BUILD    = externals/tommath/build

#
# Source files.
#

CORE_HDR = $(S)th8.h $(S)th8_plat.h $(S)th8_hash.h $(S)th8_util.h $(S)th8_spilornis.h

#
# Spencer regex engine objects (only when ENABLE_REGEXP=1).
#

REGEX_CFLAGS = -Wno-sign-compare -Wno-unused-function \
               -Wno-unused-variable -I$(S)plugins/regexp -I$(REGEX_BUILD)

ifeq ($(ENABLE_REGEXP),1)
  REGEX_OBJ     = $(B)regex_regcomp.o $(B)regex_regexec.o \
                  $(B)regex_regfree.o $(B)regex_regprefix.o \
                  $(B)regex_regerror.o
  REGEX_OBJ_PIC = $(B)regex_regcomp.pic.o $(B)regex_regexec.pic.o \
                  $(B)regex_regfree.pic.o $(B)regex_regprefix.pic.o \
                  $(B)regex_regerror.pic.o
else
  REGEX_OBJ     =
  REGEX_OBJ_PIC =
endif

#
# Bestline (for interactive shell).
#

BESTLINE_CFLAGS = -D_GNU_SOURCE \
                  -Wno-sign-compare -Wno-missing-prototypes \
                  -Wno-strict-prototypes -Wno-old-style-definition \
                  -Wno-declaration-after-statement \
                  -Wno-unused-variable \
                  -Wno-variadic-macros \
                  -Wno-variadic-macro-arguments-omitted \
                  -Wno-pedantic

#
# Spilornis flags (suppress warnings from external code).
#

SPILORNIS_CFLAGS = -Wno-unused-function -Wno-unused-variable \
                   -Wno-macro-redefined -Wno-missing-prototypes \
                   -Wno-embedded-directive -Wno-format-extra-args \
                   -Wno-type-limits

#
# Object files (static and PIC variants).
#

CORE_OBJ     = $(B)th8_core.o $(B)th8_plat.o $(B)th8_hash.o $(B)th8_util.o \
               $(B)th8_base64.o $(B)th8_expr.o $(B)th8_load.o $(B)th8_vars.o \
               $(B)th8_glob.o $(B)th8_math.o $(B)th8_cache.o $(B)th8_channel.o \
               $(B)th8_plugin.o \
               $(B)th8_binary.o $(B)th8_control.o $(B)th8_events.o \
               $(B)th8_expressions.o $(B)th8_extensibility.o \
               $(B)th8_filesystems.o \
               $(B)th8_formatting.o \
               $(B)th8_introspection.o \
               $(B)th8_io.o $(B)th8_lists.o $(B)th8_looping.o \
               $(B)th8_management.o $(B)th8_procedures.o \
               $(B)th8_strings.o \
               $(B)th8_timekeeping.o $(B)th8_variables.o \
               $(B)th8_lang.o $(B)th8_xlib.o $(B)th8_fault.o \
               $(B)th8_env.o $(B)th8_mem.o \
               $(B)th8_regex.o $(B)th8_nullio.o $(B)th8_ctime.o \
               $(CURL_OBJ) $(BIGINT_OBJ) $(CRYPTOGRAPHY_OBJ) \
               $(MIMALLOC_OBJ) $(UNBOUND_OBJ) \
               $(B)th8_spilornis.o $(B)spilornis.o \
               $(B)th8StubInit.o $(B)th8InternalStubInit.o \
               $(B)ConvertUTF_v2.o $(REGEX_OBJ)

CORE_OBJ_PIC = $(B)th8_core.pic.o $(B)th8_plat.pic.o $(B)th8_hash.pic.o \
               $(B)th8_util.pic.o $(B)th8_base64.pic.o $(B)th8_expr.pic.o $(B)th8_load.pic.o $(B)th8_vars.pic.o \
               $(B)th8_glob.pic.o $(B)th8_math.pic.o \
               $(B)th8_cache.pic.o $(B)th8_channel.pic.o \
               $(B)th8_plugin.pic.o \
               $(B)th8_binary.pic.o $(B)th8_control.pic.o $(B)th8_events.pic.o \
               $(B)th8_expressions.pic.o $(B)th8_extensibility.pic.o \
               $(B)th8_filesystems.pic.o \
               $(B)th8_formatting.pic.o \
               $(B)th8_introspection.pic.o \
               $(B)th8_io.pic.o $(B)th8_lists.pic.o $(B)th8_looping.pic.o \
               $(B)th8_management.pic.o $(B)th8_procedures.pic.o \
               $(B)th8_strings.pic.o \
               $(B)th8_timekeeping.pic.o $(B)th8_variables.pic.o \
               $(B)th8_lang.pic.o $(B)th8_xlib.pic.o $(B)th8_fault.pic.o \
               $(B)th8_env.pic.o $(B)th8_mem.pic.o \
               $(B)th8_regex.pic.o $(B)th8_nullio.pic.o \
               $(B)th8_ctime.pic.o $(CURL_OBJ_PIC) $(BIGINT_OBJ_PIC) \
               $(CRYPTOGRAPHY_OBJ_PIC) $(MIMALLOC_OBJ_PIC) $(UNBOUND_OBJ_PIC) \
               $(B)th8_spilornis.pic.o $(B)spilornis.pic.o $(B)th8StubInit.pic.o \
               $(B)th8InternalStubInit.pic.o \
               $(B)ConvertUTF_v2.pic.o $(REGEX_OBJ_PIC)

PLAT_OBJ     = $(B)$(PLAT_SRC:.c=.o)
PLAT_OBJ_PIC = $(B)$(PLAT_SRC:.c=.pic.o)

ifdef MACOS_SRC
  PLAT_OBJ     += $(B)$(MACOS_SRC:.c=.o)
  PLAT_OBJ_PIC += $(B)$(MACOS_SRC:.c=.pic.o)
endif

ifdef IOS_SRC
  PLAT_OBJ     += $(B)$(IOS_SRC:.c=.o)
  PLAT_OBJ_PIC += $(B)$(IOS_SRC:.c=.pic.o)
endif

ifdef ANDROID_SRC
  PLAT_OBJ     += $(B)$(ANDROID_SRC:.c=.o)
  PLAT_OBJ_PIC += $(B)$(ANDROID_SRC:.c=.pic.o)
endif

#
# Amalgamation build mode.
#
# When USE_AMALGAMATION=1, the entire library is compiled from a single
# th8.c file instead of individual source files.  This is the mode
# used by embedders (like LadyBird) who receive th8.c + th8.h as a
# two-file package.
#
#
# In amalgamation mode, the single th8_amal.o replaces all individual
# core/platform/plugin objects.  External vendored libraries (mimalloc,
# regex) are still linked separately since they are not part of the
# TH8 source tree.
#
#
# In amalgamation mode, the single th8_amal.o replaces ALL TH8 source
# files.  The only external dependency is mimalloc's static library
# (when TH8_USE_MIMALLOC is defined), which is not part of TH8's
# source tree and must be linked separately.
#
#
# In amalgamation mode, the single th8_amal.o contains ALL TH8 source
# code including the mimalloc wrapper.  Only the external mimalloc
# static library itself (not the TH8 wrapper) needs separate linking.
#
ifeq ($(USE_AMALGAMATION),1)
  CORE_OBJ     = $(B)th8_amal.o $(B)mimalloc_static.o
  CORE_OBJ_PIC = $(B)th8_amal.pic.o $(B)mimalloc_static.pic.o
  PLAT_OBJ     =
  PLAT_OBJ_PIC =
endif

#
# Output files.
#

STATIC_LIB = $(B)libth8.a
SHARED_LIB = $(B)libth8$(SHLIB_EXT)
STUBS_LIB  = $(B)libth8stub.a
SHELL_BIN  ?= $(B)th8sh

# ----------------------------------------------------------------
# Directory creation.
# ----------------------------------------------------------------

$(B):
	mkdir -p $(B)
ifneq ($(B),$(STATIC_B))
	mkdir -p $(STATIC_B)
endif

# ----------------------------------------------------------------
# Default target.
# ----------------------------------------------------------------

ALL_NON_STATIC_TARGETS = shared stubs shell testlib bridge sqlite3-ext
ALL_TARGETS = $(ALL_NON_STATIC_TARGETS) static

ifneq ($(UNAME_S),Darwin)
  ALL_TARGETS += static-shell
endif

ifeq ($(USE_AMALGAMATION),1)
AMAL_PREREQ = amalgamation
else
AMAL_PREREQ =
endif

all-non-static: genstubs $(VERSIONHDR) $(AMAL_PREREQ) $(ALL_NON_STATIC_TARGETS)

all: genstubs $(VERSIONHDR) $(AMAL_PREREQ) $(ALL_TARGETS) manlint audit audit-reqs

# ----------------------------------------------------------------
# Generated version header.
# ----------------------------------------------------------------

$(VERSIONHDR): FORCE | $(B)
	@$(TCLSH) tools/mkversion.tcl $(VERSIONHDR)

FORCE:

# ----------------------------------------------------------------
# Static library.
# ----------------------------------------------------------------

static: $(STATIC_LIB)

$(STATIC_LIB): $(CORE_OBJ) | $(B)
	$(AR) rcs $@ $(CORE_OBJ)
	$(RANLIB) $@

# ----------------------------------------------------------------
# Stubs library (for extensions to link against).
# ----------------------------------------------------------------

stubs: $(STUBS_LIB)

$(B)th8StubLib.o: $(S)th8StubLib.c $(S)th8.h $(S)th8Decls.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC -DUSE_TH8_STUBS -c -o $@ $(S)th8StubLib.c

$(STUBS_LIB): $(B)th8StubLib.o | $(B)
	$(AR) rcs $@ $(B)th8StubLib.o
	$(RANLIB) $@

# ----------------------------------------------------------------
# Shared library.
# ----------------------------------------------------------------

shared: $(SHARED_LIB)

ifeq ($(USE_AMALGAMATION),1)
  LIBC_OBJ     =
  LIBC_OBJ_PIC =
else
  LIBC_OBJ     = $(B)th8_libc.o
  LIBC_OBJ_PIC = $(B)th8_libc.pic.o
endif

$(SHARED_LIB): $(CORE_OBJ_PIC) $(PLAT_OBJ_PIC) $(LIBC_OBJ_PIC) | $(B)
	$(CC) $(CFLAGS) $(HARDEN_LDFLAGS) $(SHLIB_FLAGS) -o $@ $(CORE_OBJ_PIC) $(PLAT_OBJ_PIC) \
	  $(LIBC_OBJ_PIC) -lm $(CURL_LIBS) $(CRYPTOGRAPHY_LIBS) $(UNBOUND_LIBS) $(PLAT_LIBS)

# ----------------------------------------------------------------
# Interactive shell (with bestline and platform file).
# ----------------------------------------------------------------

shell: $(VERSIONHDR) shared stubs $(SHELL_BIN)

#
# The shell links dynamically against libth8.so/.dylib.
# It does NOT link $(CORE_OBJ) or $(PLAT_OBJ) directly --
# those are in the shared library.
#
# The shell is NOT compiled with -DUSE_TH8_STUBS because it
# calls Th8_Initialize and Th8_CreateInterp before any stubs
# table exists.  Stubs are for extensions (loaded via dlopen),
# not for the host application.
#
# On Linux, -Wl,-rpath,'$$ORIGIN' makes the dynamic linker
# search the executable's directory for libth8.so.
# On macOS, -Wl,-rpath,@executable_path does the same.
#
ifeq ($(UNAME_S),Darwin)
  SHELL_RPATH = -Wl,-rpath,@executable_path
else
  SHELL_RPATH = -Wl,-rpath,'$$ORIGIN'
endif

#
# When building the static shell, we do not need to link to the TH8
# shared library; otherwise, we do.
#
ifeq ($(STATIC_BUILD),1)
  SHELL_DEPS = $(B)th8sh.o $(B)th8_shell.o $(B)bestline.o $(STATIC_LIB) $(LIBC_OBJ) $(PLAT_OBJ)
  TH8_LIBS = $(STATIC_LIB) $(PLAT_OBJ) $(LIBC_OBJ) -lm $(CURL_LIBS) $(CRYPTOGRAPHY_LIBS) $(UNBOUND_LIBS) $(PLAT_LIBS)
else
  SHELL_DEPS = $(B)th8sh.o $(B)th8_shell.o $(B)bestline.o $(SHARED_LIB)
  TH8_LIBS = -L$(B) -lth8
endif

$(SHELL_BIN): $(SHELL_DEPS) | $(B)
	$(CC) $(CFLAGS) $(HARDEN_LDFLAGS) $(EXTRA_LDFLAGS) $(SHELL_EXPORT) \
	  $(SHELL_RPATH) -o $@ $(B)th8sh.o $(B)th8_shell.o \
	  $(B)bestline.o $(TH8_LIBS)

$(B)th8sh.o: $(S)th8sh.c $(S)th8_shell.h $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) \
	  -c -o $@ $(S)th8sh.c

$(B)th8_shell.o: $(S)th8_shell.c $(S)th8_shell.h $(CORE_HDR) | bestline_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) \
	  -I$(BESTLINE_BUILD) -c -o $@ $(S)th8_shell.c

$(B)bestline.o: $(BESTLINE_BUILD)/bestline.c $(BESTLINE_BUILD)/bestline.h | bestline_vendor $(B)
	$(CC) $(CFLAGS) $(BESTLINE_CFLAGS) \
	  -I$(BESTLINE_BUILD) -c -o $@ $(BESTLINE_BUILD)/bestline.c

# ----------------------------------------------------------------
# Static shell (for chroot deployment).
#
# Passes -static through CFLAGS so both compile and link use it.
# NOTE: macOS does not support fully static executables (Apple ld
# rejects -static).  On Linux, -static-pie may be preferable for
# position-independent static binaries; override CFLAGS manually
# in that case:
#   make CFLAGS="$(CFLAGS_RELEASE) -static-pie" shell
# ----------------------------------------------------------------

#
# Static shell for chroot deployment.  Requires Linux (or another
# platform that supports -static).  macOS does not support fully
# static executables; use the regular shell target there.
#
# By default, libcurl is disabled for static builds because static
# linking requires .a archives for ALL transitive dependencies
# (nghttp2, libidn2, libssh, libpsl, zstd, brotli, gssapi, etc.)
# which are often not installed.  To enable:
#   make static-shell STATIC_ENABLE_LIBCURL=1
# (requires all static -dev packages to be installed).
#

STATIC_ENABLE_LIBCURL ?= 0

static-shell:
ifeq ($(shell uname -s),Darwin)
	@echo "ERROR: macOS does not support fully static executables."
	@echo "Use 'make shell' instead and deploy with the shared libraries."
	@exit 1
else
	$(MAKE) EXTRA_LDFLAGS="-static" \
	  B="$(STATIC_B)" STATIC_BUILD=1 \
	  ENABLE_REGEXP="$(ENABLE_REGEXP)" \
	  ENABLE_LIBCURL="$(STATIC_ENABLE_LIBCURL)" \
	  ENABLE_BIGINT="$(ENABLE_BIGINT)" \
	  ENABLE_CRYPTOGRAPHY="$(ENABLE_CRYPTOGRAPHY)" shell
endif

# ----------------------------------------------------------------
# Fuzz testing.
#
#   make fuzz          Build all fuzz targets.
#   make fuzz-eval     Build and run the script evaluator fuzzer.
#   make fuzz-expr     Build and run the expression parser fuzzer.
#   make fuzz-list     Build and run the list parser fuzzer.
#   make fuzz-harpy    Build and run the Harpy signature fuzzer.
#   make fuzz-snk      Build and run the SNK key parser fuzzer.
#   make fuzz-format   Build and run the format command fuzzer.
#   make fuzz          Build all fuzz targets.
#   make fuzz-eval     Build and run a single fuzzer.
#   make fuzz-corpus   Generate seed corpora from test suite.
#
# FUZZ_MODE selects the fuzzing engine:
#
#   standalone  (default) Plain executables reading one input from
#               stdin.  Useful for manual testing.
#
#   libfuzzer   Requires LLVM clang with libFuzzer support.
#               Runs for 60 s by default (override FUZZ_ARGS).
#
#   afl         Instruments with afl-clang-fast for AFL++.
#               Rebuilds the entire static library into bin-afl/
#               so that every object has AFL coverage feedback.
#
# AFL++ parallel / headless targets:
#
#   make FUZZ_MODE=afl afl-run-all
#               Launches all 6 fuzzers in the background with one
#               -M (main) instance per target.  Output goes to
#               $(AFL_FUZZ_OUT)/out_<target>/.  Logs to
#               $(AFL_FUZZ_OUT)/log_<target>.txt.
#
#   make FUZZ_MODE=afl afl-run-eval   (or -expr, -list, etc.)
#               Launches a single fuzzer in the background.
#
#   make afl-status
#               Shows afl-whatsup summary of all running fuzzers.
#
#   make afl-stop
#               Sends SIGINT to all afl-fuzz processes.
#
#   AFL_FUZZ_TIME  Duration per fuzzer (default: 3600 = 1 hour).
#   AFL_FUZZ_JOBS  Parallel instances per target (default: 1).
#                  >1 creates one -M main + (N-1) -S secondaries.
#
# All fuzz targets also support ASan + UBSan regardless of mode.
# ----------------------------------------------------------------

FUZZ_DIR    = fuzz
FUZZ_MODE  ?= standalone

ifeq ($(FUZZ_MODE),libfuzzer)
  FUZZ_CC       ?= clang
  FUZZ_SANITIZE  = -fsanitize=fuzzer,address,undefined
  FUZZ_ARGS     ?= -max_total_time=60
else ifeq ($(FUZZ_MODE),afl)
  FUZZ_CC       ?= afl-clang-fast
  FUZZ_SANITIZE  = -fsanitize=address,undefined -DTH8_FUZZ_STANDALONE
  FUZZ_ARGS     ?=
else
  FUZZ_CC       ?= clang
  FUZZ_SANITIZE  = -fsanitize=address,undefined -DTH8_FUZZ_STANDALONE
  FUZZ_ARGS     ?=
endif

FUZZ_FLAGS  = -g -O1 $(FUZZ_SANITIZE) \
  $(CFLAGS_BASE) $(FEATURE_DEFS) -I$(S) -I$(B) -I$(UTF_DIR) \
  $(BIGINT_INCLUDES) $(CRYPTOGRAPHY_INCLUDES)
FUZZ_LIBS   = -lm $(CURL_LIBS) $(CRYPTOGRAPHY_LIBS) $(UNBOUND_LIBS) $(PLAT_LIBS)

FUZZ_TARGETS = $(B)fuzz_eval $(B)fuzz_expr \
  $(B)fuzz_list $(B)fuzz_format \
  $(B)fuzz_harpy $(B)fuzz_snk

#
# AFL++ requires every object (library, platform, harness) to be
# compiled with afl-clang-fast for coverage instrumentation.  The
# "afl" mode triggers a recursive make that rebuilds everything
# into bin-afl/ with CC=afl-clang-fast, then links the fuzz
# harnesses there.  The normal bin/ build is not touched.
#

ifeq ($(FUZZ_MODE),afl)

AFL_FUZZ_TIME  ?= 3600
AFL_FUZZ_JOBS  ?= 1
AFL_FUZZ_NAMES  = eval expr list format harpy snk

fuzz-corpus:
	$(TCLSH) tools/mkfuzzcorpus.tcl $(FUZZ_DIR)

fuzz:
	$(MAKE) B=$(AFL_B) CC="$(FUZZ_CC)" FUZZ_CC="$(FUZZ_CC)" \
	  EXTRA_CFLAGS="-DTH8_FUZZ_STANDALONE" FUZZ_MODE=standalone fuzz

fuzz-%:
	$(MAKE) B=$(AFL_B) CC="$(FUZZ_CC)" FUZZ_CC="$(FUZZ_CC)" \
	  EXTRA_CFLAGS="-DTH8_FUZZ_STANDALONE" FUZZ_MODE=standalone fuzz-$*
	@echo ""
	@echo "AFL++ mode: run the fuzzer with:"
	@echo "  afl-fuzz -i $(FUZZ_DIR)/corpus_$* -o $(AFL_FUZZ_OUT)/out_$* -- $(AFL_B)fuzz_$*"

#
# afl-run-<target>: launch a single AFL++ fuzzer in the background.
#
# Creates the output and corpus directories, then starts afl-fuzz
# detached from the terminal (nohup + background).  With
# AFL_FUZZ_JOBS > 1, spawns one -M main and (N-1) -S secondaries.
#

define AFL_RUN_template
afl-run-$(1): fuzz
	@mkdir -p $(FUZZ_DIR)/corpus_$(1) $(AFL_FUZZ_OUT)
	@echo "Starting AFL++ for $(1) ($(AFL_FUZZ_JOBS) job(s), $(AFL_FUZZ_TIME)s) ..."
	@AFL_NO_UI=1 nohup afl-fuzz \
	  -M main_$(1) \
	  -V $(AFL_FUZZ_TIME) \
	  -i $(FUZZ_DIR)/corpus_$(1) \
	  -o $(AFL_FUZZ_OUT)/out_$(1) \
	  -- $(AFL_B)fuzz_$(1) \
	  > $(AFL_FUZZ_OUT)/log_$(1)_main.txt 2>&1 &
	@j=2; while [ $$$$j -le $(AFL_FUZZ_JOBS) ]; do \
	  AFL_NO_UI=1 nohup afl-fuzz \
	    -S sec$$$${j}_$(1) \
	    -V $(AFL_FUZZ_TIME) \
	    -i $(FUZZ_DIR)/corpus_$(1) \
	    -o $(AFL_FUZZ_OUT)/out_$(1) \
	    -- $(AFL_B)fuzz_$(1) \
	    > $(AFL_FUZZ_OUT)/log_$(1)_sec$$$${j}.txt 2>&1 & \
	  j=$$$$((j + 1)); \
	done
	@echo "  Output: $(AFL_FUZZ_OUT)/out_$(1)/"
	@echo "  Log:    $(AFL_FUZZ_OUT)/log_$(1)_main.txt"
endef

$(foreach t,$(AFL_FUZZ_NAMES),$(eval $(call AFL_RUN_template,$(t))))

#
# afl-run-all: launch all 6 fuzzers in parallel.
#

afl-run-all: $(addprefix afl-run-,$(AFL_FUZZ_NAMES))
	@echo ""
	@echo "All fuzzers running.  Use 'make afl-status' to monitor."

#
# afl-status: show summary of all running AFL++ instances.
#

afl-status:
	@if command -v afl-whatsup > /dev/null 2>&1; then \
	  for t in $(AFL_FUZZ_NAMES); do \
	    if [ -d "$(AFL_FUZZ_OUT)/out_$$t" ]; then \
	      echo "=== $$t ==="; \
	      afl-whatsup -s "$(AFL_FUZZ_OUT)/out_$$t" 2>/dev/null || true; \
	      echo ""; \
	    fi; \
	  done; \
	else \
	  echo "afl-whatsup not found; showing process list:"; \
	  ps aux | grep '[a]fl-fuzz' || echo "  (no afl-fuzz processes)"; \
	fi

#
# afl-stop: terminate all running AFL++ instances.
#

afl-stop:
	@echo "Stopping all afl-fuzz processes..."
	@-pkill -INT -f 'afl-fuzz.*fuzz_' 2>/dev/null || true
	@sleep 1
	@if pgrep -f 'afl-fuzz.*fuzz_' > /dev/null 2>&1; then \
	  echo "Sending SIGTERM to remaining processes..."; \
	  pkill -TERM -f 'afl-fuzz.*fuzz_' 2>/dev/null || true; \
	else \
	  echo "All fuzzers stopped."; \
	fi

else
# ---- non-AFL modes (standalone / libfuzzer) ----

fuzz-corpus:
	$(TCLSH) tools/mkfuzzcorpus.tcl $(FUZZ_DIR)

fuzz: $(STATIC_LIB) $(FUZZ_TARGETS)

$(FUZZ_TARGETS): $(B)fuzz_%: $(FUZZ_DIR)/fuzz_%.c $(STATIC_LIB) $(PLAT_OBJ) $(LIBC_OBJ) | $(B)
	$(FUZZ_CC) $(FUZZ_FLAGS) -o $@ $< $(PLAT_OBJ) $(LIBC_OBJ) $(STATIC_LIB) $(FUZZ_LIBS)

fuzz-%: $(B)fuzz_%
	mkdir -p $(B)fuzz
	mkdir -p $(FUZZ_DIR)/corpus_$*
ifeq ($(FUZZ_MODE),libfuzzer)
	./$< $(FUZZ_DIR)/corpus_$* $(FUZZ_ARGS)
else
	@echo "Standalone mode: feed input via stdin."
	@echo "  Example: ./$< < $(FUZZ_DIR)/corpus_$*/seed1.*"
	@echo "  For AFL: make FUZZ_MODE=afl fuzz"
endif

endif

# ----------------------------------------------------------------
# Testing rules.
# ----------------------------------------------------------------

th8test-only:
	TH8SH_YES_TESTLIB=1 $(SHELL_BIN) tests/all.tcl | tee $(B)th8-test.log

th8test: fresh th8test-only

tcltest:
	$(TCLSH) tests/all.tcl | tee $(B)tcl-test.log

eagletest:
	CreateFlags=+UseNamespaces dotnet exec --roll-forward Major "$(EAGLE_SHELL)" -preInitialize "expr {flags(\"+BooleanToInteger StringToInteger\")}" -file tests/all.tcl | tee $(B)eagle-test.log

# ----------------------------------------------------------------
# Compilation rules.
# ----------------------------------------------------------------

#
# Static objects (no -fPIC).
#

$(B)th8_core.o: $(S)th8_core.c $(CORE_HDR) | tommath_vendor spilornis_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_core.c

$(B)th8_plat.o: $(S)th8_plat.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_plat.c

$(B)th8_hash.o: $(S)th8_hash.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_hash.c

$(B)th8_hash_standalone.o: $(S)th8_hash.c $(S)th8_hash.h | $(B)
	$(CC) $(CFLAGS) -DTH8_HASH_STANDALONE -I$(S) -c -o $@ $(S)th8_hash.c

$(B)th8_util.o: $(S)th8_util.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_util.c

$(B)th8_base64.o: $(S)th8_base64.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_base64.c

$(B)th8_expr.o: $(S)th8_expr.c $(S)th8.h $(S)th8_int.h $(S)th8_expr.h | tommath_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_expr.c

$(B)th8_expr.pic.o: $(S)th8_expr.c $(S)th8.h $(S)th8_int.h $(S)th8_expr.h | tommath_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_expr.c

$(B)th8_load.o: $(S)th8_load.c $(S)th8.h $(S)th8_int.h $(S)th8_int_core.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_load.c

$(B)th8_load.pic.o: $(S)th8_load.c $(S)th8.h $(S)th8_int.h $(S)th8_int_core.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_load.c

$(B)th8_vars.o: $(S)th8_vars.c $(S)th8.h $(S)th8_int.h $(S)th8_int_core.h $(S)th8_vars.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_vars.c

$(B)th8_vars.pic.o: $(S)th8_vars.c $(S)th8.h $(S)th8_int.h $(S)th8_int_core.h $(S)th8_vars.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_vars.c

$(B)th8_glob.o: $(S)th8_glob.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_glob.c

$(B)th8_math.o: $(S)th8_math.c $(CORE_HDR) | tommath_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_math.c

$(B)th8_cache.o: $(S)th8_cache.c $(CORE_HDR) $(S)th8_int.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_cache.c

$(B)th8_channel.o: $(S)th8_channel.c $(CORE_HDR) $(S)th8_int.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_channel.c

$(B)th8_attrflags.o: $(S)plugins/harpy/th8_attrflags.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_attrflags.c

$(B)th8_attrflags.pic.o: $(S)plugins/harpy/th8_attrflags.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_attrflags.c

$(B)th8_plugin.o: $(S)th8_plugin.c $(S)th8_plugin.h $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_plugin.c

$(B)th8_plugin.pic.o: $(S)th8_plugin.c $(S)th8_plugin.h $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_plugin.c

$(B)th8_binary.o: $(S)plugins/th8_binary.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_binary.c

$(B)th8_binary.pic.o: $(S)plugins/th8_binary.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_binary.c

$(B)th8_control.o: $(S)plugins/th8_control.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_control.c

$(B)th8_control.pic.o: $(S)plugins/th8_control.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_control.c

$(B)th8_events.o: $(S)plugins/th8_events.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_events.c

$(B)th8_events.pic.o: $(S)plugins/th8_events.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_events.c

$(B)th8_expressions.o: $(S)plugins/th8_expressions.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_expressions.c

$(B)th8_expressions.pic.o: $(S)plugins/th8_expressions.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_expressions.c

$(B)th8_extensibility.o: $(S)plugins/th8_extensibility.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_extensibility.c

$(B)th8_extensibility.pic.o: $(S)plugins/th8_extensibility.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_extensibility.c

$(B)th8_filesystems.o: $(S)plugins/th8_filesystems.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_filesystems.c

$(B)th8_filesystems.pic.o: $(S)plugins/th8_filesystems.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_filesystems.c

$(B)th8_formatting.o: $(S)plugins/th8_formatting.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_formatting.c

$(B)th8_formatting.pic.o: $(S)plugins/th8_formatting.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_formatting.c

$(B)th8_introspection.o: $(S)plugins/th8_introspection.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_introspection.c

$(B)th8_introspection.pic.o: $(S)plugins/th8_introspection.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_introspection.c

$(B)th8_io.o: $(S)plugins/th8_io.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_io.c

$(B)th8_io.pic.o: $(S)plugins/th8_io.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_io.c

$(B)th8_lists.o: $(S)plugins/th8_lists.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_lists.c

$(B)th8_lists.pic.o: $(S)plugins/th8_lists.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_lists.c

$(B)th8_looping.o: $(S)plugins/th8_looping.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_looping.c

$(B)th8_looping.pic.o: $(S)plugins/th8_looping.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_looping.c

$(B)th8_management.o: $(S)plugins/th8_management.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_management.c

$(B)th8_management.pic.o: $(S)plugins/th8_management.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_management.c

$(B)th8_procedures.o: $(S)plugins/th8_procedures.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_procedures.c

$(B)th8_procedures.pic.o: $(S)plugins/th8_procedures.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_procedures.c

$(B)th8_strings.o: $(S)plugins/th8_strings.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_strings.c

$(B)th8_strings.pic.o: $(S)plugins/th8_strings.c $(S)th8.h $(S)th8_int.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_strings.c

$(B)th8_timekeeping.o: $(S)plugins/th8_timekeeping.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_timekeeping.c

$(B)th8_timekeeping.pic.o: $(S)plugins/th8_timekeeping.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_timekeeping.c

$(B)th8_variables.o: $(S)plugins/th8_variables.c $(S)th8.h $(S)th8_int.h $(S)th8_vars.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_variables.c

$(B)th8_variables.pic.o: $(S)plugins/th8_variables.c $(S)th8.h $(S)th8_int.h $(S)th8_vars.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_variables.c

$(B)th8_lang.o: $(S)th8_lang.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_lang.c

$(B)th8_xlib.o: $(S)th8_xlib.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_xlib.c

$(B)th8_fault.o: $(S)th8_fault.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_fault.c

$(B)th8_fault.pic.o: $(S)th8_fault.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_fault.c

$(B)th8_env.o: $(S)th8_env.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_env.c

$(B)th8_env.pic.o: $(S)th8_env.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_env.c

$(B)th8_mem.o: $(S)th8_mem.c $(S)th8.h $(S)th8_int.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_mem.c

$(B)th8_mem.pic.o: $(S)th8_mem.c $(S)th8.h $(S)th8_int.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_mem.c

$(B)th8_regex.o: $(S)plugins/regexp/th8_regex.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/regexp/th8_regex.c

$(B)th8_nullio.o: $(S)th8_nullio.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_nullio.c

$(B)th8_ctime.o: $(S)th8_ctime.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_ctime.c

ifeq ($(ENABLE_LIBCURL),1)
$(B)th8_curl.o: $(S)th8_curl.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_curl.c
endif

ifeq ($(ENABLE_UNBOUND),1)
$(B)th8_unbound.o: $(S)th8_unbound.c $(S)th8.h $(S)th8_unbound.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_unbound.c

$(B)th8_unbound.pic.o: $(S)th8_unbound.c $(S)th8.h $(S)th8_unbound.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC -c -o $@ $(S)th8_unbound.c
endif

ifeq ($(ENABLE_MIMALLOC),1)
MI_SRC = externals/mimalloc/src
MI_INC = externals/mimalloc/include
#
# mimalloc's static.c includes all source files.  We suppress
# warnings that conflict with TH8's -pedantic -Wall flags since
# mimalloc uses GCC/Clang extensions internally.
#
MI_CFLAGS = -std=c11 -O2 -DNDEBUG -DMI_STATIC_LIB \
	    -I$(MI_INC) -I$(MI_SRC) \
	    -Wno-pedantic -Wno-long-long -Wno-unused-parameter \
	    -Wno-strict-prototypes -Wno-missing-prototypes \
	    -Wno-old-style-definition

$(B)mimalloc_static.o: $(MI_SRC)/static.c | $(B)
	$(CC) $(MI_CFLAGS) -c -o $@ $(MI_SRC)/static.c

$(B)mimalloc_static.pic.o: $(MI_SRC)/static.c | $(B)
	$(CC) $(MI_CFLAGS) -fPIC -c -o $@ $(MI_SRC)/static.c

$(B)th8_mimalloc.o: $(S)th8_mimalloc.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_mimalloc.c

$(B)th8_mimalloc.pic.o: $(S)th8_mimalloc.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_mimalloc.c
endif

ifeq ($(ENABLE_BIGINT),1)
$(B)th8_bigint.o: $(S)th8_bigint.c $(S)th8.h $(S)th8_bigint.h | tommath_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_bigint.c

$(B)th8_bigint.pic.o: $(S)th8_bigint.c $(S)th8.h $(S)th8_bigint.h | tommath_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_bigint.c

$(B)tommath_amalg.c: externals/tommath/vendor/tommath.h tools/tommath_amalg.tcl | tommath_vendor $(B)
	$(TCLSH) tools/tommath_amalg.tcl $(B)

$(B)tommath_amalg.o: $(B)tommath_amalg.c | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(TOMMATH_BUILD) -c -o $@ $(B)tommath_amalg.c

$(B)tommath_amalg.pic.o: $(B)tommath_amalg.c | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(TOMMATH_BUILD) $(PIC_DEFS) -c -o $@ $(B)tommath_amalg.c
endif

ifeq ($(ENABLE_CRYPTOGRAPHY),1)
$(B)th8_snk.o: $(S)plugins/harpy/th8_snk.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_snk.c

$(B)th8_snk.pic.o: $(S)plugins/harpy/th8_snk.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_snk.c

$(B)th8_harpy.o: $(S)plugins/harpy/th8_harpy.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_harpy.c

$(B)th8_harpy.pic.o: $(S)plugins/harpy/th8_harpy.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_harpy.c

$(B)th8_policy.o: $(S)plugins/harpy/th8_policy.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_policy.c

$(B)th8_policy.pic.o: $(S)plugins/harpy/th8_policy.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_policy.c

$(B)th8_keyRoot.o: $(S)plugins/harpy/th8_keyRoot.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_keyRoot.c

$(B)th8_keyRoot.pic.o: $(S)plugins/harpy/th8_keyRoot.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_keyRoot.c

$(B)th8_key0.o: $(S)plugins/harpy/th8_key0.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_key0.c

$(B)th8_key0.pic.o: $(S)plugins/harpy/th8_key0.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_key0.c

$(B)th8_keyring_stub.o: $(S)plugins/harpy/th8_keyring_stub.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_keyring_stub.c

$(B)th8_keyring_stub.pic.o: $(S)plugins/harpy/th8_keyring_stub.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_keyring_stub.c

$(B)th8_keyTime.o: $(S)plugins/harpy/th8_keyTime.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_keyTime.c

$(B)th8_keyTime.pic.o: $(S)plugins/harpy/th8_keyTime.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_keyTime.c

$(B)th8_keyTest.o: $(S)plugins/harpy/th8_keyTest.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_keyTest.c

$(B)th8_keyTest.pic.o: $(S)plugins/harpy/th8_keyTest.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_keyTest.c

$(B)th8_secure.o: $(S)plugins/crypto/th8_secure.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/crypto/th8_secure.c

$(B)th8_secure.pic.o: $(S)plugins/crypto/th8_secure.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/crypto/th8_secure.c

$(B)th8_protect.o: $(S)th8_protect.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_protect.c

$(B)th8_protect.pic.o: $(S)th8_protect.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_protect.c

$(B)th8_time.o: $(S)plugins/harpy/th8_time.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/harpy/th8_time.c

$(B)th8_time.pic.o: $(S)plugins/harpy/th8_time.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/harpy/th8_time.c

$(B)th8_crypto_cmds.o: $(S)plugins/crypto/th8_crypto_cmds.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/crypto/th8_crypto_cmds.c

$(B)th8_crypto_cmds.pic.o: $(S)plugins/crypto/th8_crypto_cmds.c $(S)th8.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/crypto/th8_crypto_cmds.c

$(B)th8_harpy_cmds.o: $(S)plugins/th8_harpy.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)plugins/th8_harpy.c

$(B)th8_harpy_cmds.pic.o: $(S)plugins/th8_harpy.c $(S)th8.h $(S)th8_util.h $(S)th8_plugin.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/th8_harpy.c
endif

$(B)th8_spilornis.o: $(S)th8_spilornis.c $(S)th8_spilornis.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_spilornis.c

$(B)spilornis.o: $(B)Spilornis.c $(S)th8_spilornis.h | spilornis_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(SPILORNIS_CFLAGS) \
	  -include $(S)th8_spilornis.h -c -o $@ $(B)Spilornis.c

$(B)th8StubInit.o: $(S)th8StubInit.c $(S)th8.h $(S)th8Decls.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8StubInit.c

$(B)th8InternalStubInit.o: $(S)th8InternalStubInit.c $(S)th8.h \
	    $(S)th8_int.h $(S)th8InternalDecls.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8InternalStubInit.c

$(B)ConvertUTF_v2.o: $(UTF_DIR)/ConvertUTF_v2.c $(UTF_DIR)/ConvertUTF_v2.h | $(B)
	$(CC) $(CFLAGS) -I$(UTF_DIR) -Wno-unused-function -Wno-implicit-fallthrough -c -o $@ \
	  $(UTF_DIR)/ConvertUTF_v2.c

#
# PIC objects (for shared library).
#
# NOTE: PIC objects define TH8_BUILD_DLL so that TH8_API resolves
#       to __attribute__((visibility("default"))) on GCC/Clang.
#

PIC_DEFS = -DTH8_BUILD_DLL -fPIC -fvisibility=hidden

$(B)th8_core.pic.o: $(S)th8_core.c $(CORE_HDR) | tommath_vendor spilornis_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_core.c

$(B)th8_plat.pic.o: $(S)th8_plat.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_plat.c

$(B)th8_hash.pic.o: $(S)th8_hash.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_hash.c

$(B)th8_util.pic.o: $(S)th8_util.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_util.c

$(B)th8_base64.pic.o: $(S)th8_base64.c $(S)th8.h $(S)th8_util.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_base64.c

$(B)th8_glob.pic.o: $(S)th8_glob.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_glob.c

$(B)th8_math.pic.o: $(S)th8_math.c $(CORE_HDR) | tommath_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_math.c

$(B)th8_cache.pic.o: $(S)th8_cache.c $(CORE_HDR) $(S)th8_int.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_cache.c

$(B)th8_channel.pic.o: $(S)th8_channel.c $(CORE_HDR) $(S)th8_int.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_channel.c

$(B)th8_lang.pic.o: $(S)th8_lang.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_lang.c

$(B)th8_xlib.pic.o: $(S)th8_xlib.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_xlib.c

$(B)th8_regex.pic.o: $(S)plugins/regexp/th8_regex.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)plugins/regexp/th8_regex.c

$(B)th8_nullio.pic.o: $(S)th8_nullio.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_nullio.c

$(B)th8_ctime.pic.o: $(S)th8_ctime.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_ctime.c

ifeq ($(ENABLE_LIBCURL),1)
$(B)th8_curl.pic.o: $(S)th8_curl.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_curl.c
endif

$(B)th8_spilornis.pic.o: $(S)th8_spilornis.c $(S)th8_spilornis.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_spilornis.c

$(B)spilornis.pic.o: $(B)Spilornis.c $(S)th8_spilornis.h | spilornis_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(SPILORNIS_CFLAGS) \
	  -include $(S)th8_spilornis.h $(PIC_DEFS) -c -o $@ $(B)Spilornis.c

$(B)th8StubInit.pic.o: $(S)th8StubInit.c $(S)th8.h $(S)th8Decls.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8StubInit.c

$(B)th8InternalStubInit.pic.o: $(S)th8InternalStubInit.c $(S)th8.h \
	    $(S)th8_int.h $(S)th8InternalDecls.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ \
	  $(S)th8InternalStubInit.c

$(B)th8_libc.pic.o: $(S)th8_libc.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_libc.c

$(B)ConvertUTF_v2.pic.o: $(UTF_DIR)/ConvertUTF_v2.c $(UTF_DIR)/ConvertUTF_v2.h | $(B)
	$(CC) $(CFLAGS) -I$(UTF_DIR) -Wno-unused-function -Wno-implicit-fallthrough $(PIC_DEFS) -c -o $@ \
	  $(UTF_DIR)/ConvertUTF_v2.c

#
# Stubs regeneration (from th8.h).
#

genstubs:
	$(TCLSH) tools/mkstubs.tcl $(S)th8.h $(S)th8Decls.h $(S)th8StubInit.c
	clang-format -i --style=file $(S)th8Decls.h $(S)th8StubInit.c

#
# Banned-pattern audit (tools/audit_patterns.tcl).
#
# Scans every .c / .h file under src/ for patterns the project bans
# (currently: bare size_t multiplications, which can overflow without
# defence-in-depth checks).  Exits non-zero on any violation, so it
# fits naturally into a CI gate.  Genuinely-safe call sites can be
# annotated with /* AUDIT-OK[<rule>]: <reason> */ on the offending
# line to suppress the check; see the tool header for details.
#

audit:
	$(TCLSH) tools/audit_patterns.tcl source
	$(TCLSH) tools/audit_patterns.tcl crt-objects $(B)
	$(TCLSH) tools/audit_patterns.tcl format

#
# Formatting check (clang-format).  Kept as a separate target for
# tools that want to run only the format check (e.g. pre-commit
# hooks).  Also runs as part of the main `audit` gate above.
#

audit-format:
	$(TCLSH) tools/audit_patterns.tcl format

#
# Requirements-marker audit (tools/mkreq.tcl).
#
# Two checks:
#   * --verify:      every R-NNNNN-NNNNN block in the standard
#                    hashes correctly to its (multi-line-tolerant)
#                    requirement text.  Catches drift introduced
#                    by post-generation edits.
#   * --check-tests: every R-marker referenced by a test in
#                    tests/ exists in the standard.  Test
#                    descriptions may wrap across multiple lines
#                    (see doc/tcl_eagle_th8_style_guide.md Sec.12.1.1).
#                    Reports orphan markers; pass --report-uncovered
#                    via the underlying tool to also list standard
#                    R-markers that no test references.
#

audit-reqs:
	$(TCLSH) tools/mkreq.tcl --verify docs/pending/tcl_language_standard_v1.md
	$(TCLSH) tools/mkreq.tcl --verify docs/pending/th8_language_extensions.md
	$(TCLSH) tools/mkreq.tcl --verify docs/pending/th8_public_c_api_specification.md
	$(TCLSH) tools/mkreq.tcl --verify docs/pending/th8_internal_api_specification.md
	$(TCLSH) tools/mkreq.tcl --check-tests \
	    docs/pending/tcl_language_standard_v1.md \
	    docs/pending/th8_language_extensions.md \
	    docs/pending/th8_public_c_api_specification.md \
	    docs/pending/th8_internal_api_specification.md \
	    tests

#
# Spencer regex engine: prepare patched sources.
#

regex_vendor:
	$(TCLSH) tools/regex_vendor.tcl

#
# Bestline: prepare patched sources.
#

bestline_vendor:
	$(TCLSH) tools/bestline_vendor.tcl

#
# libtommath: copy vendor/ to build/ (with patches).
#

tommath_vendor:
	$(TCLSH) tools/tommath_amalg.tcl bin/

#
# Spencer regex engine objects.
#

$(B)regex_regcomp.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) -c -o $@ $(REGEX_BUILD)/regcomp.c

$(B)regex_regexec.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) -c -o $@ $(REGEX_BUILD)/regexec.c

$(B)regex_regfree.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) -c -o $@ $(REGEX_BUILD)/regfree.c

$(B)regex_regprefix.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) -c -o $@ $(REGEX_BUILD)/regprefix.c

$(B)regex_regerror.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) -c -o $@ $(REGEX_BUILD)/regerror.c

$(B)regex_regcomp.pic.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) $(PIC_DEFS) -c -o $@ $(REGEX_BUILD)/regcomp.c

$(B)regex_regexec.pic.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) $(PIC_DEFS) -c -o $@ $(REGEX_BUILD)/regexec.c

$(B)regex_regfree.pic.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) $(PIC_DEFS) -c -o $@ $(REGEX_BUILD)/regfree.c

$(B)regex_regprefix.pic.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) $(PIC_DEFS) -c -o $@ $(REGEX_BUILD)/regprefix.c

$(B)regex_regerror.pic.o: | regex_vendor $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(REGEX_CFLAGS) $(PIC_DEFS) -c -o $@ $(REGEX_BUILD)/regerror.c

#
# Platform objects.
#

$(B)th8_posix.o: $(S)th8_posix.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_posix.c

$(B)th8_posix.pic.o: $(S)th8_posix.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_posix.c

$(B)th8_win32.o: $(S)th8_win32.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_win32.c

$(B)th8_macos.o: $(S)th8_macos.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_macos.c

$(B)th8_macos.pic.o: $(S)th8_macos.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_macos.c

$(B)th8_ios.o: $(S)th8_ios.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_ios.c

$(B)th8_ios.pic.o: $(S)th8_ios.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_ios.c

$(B)th8_android.o: $(S)th8_android.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_android.c

$(B)th8_android.pic.o: $(S)th8_android.c $(CORE_HDR) | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) $(PIC_DEFS) -c -o $@ $(S)th8_android.c

#
# C runtime (optional; linked into shell).
#

$(B)th8_libc.o: $(S)th8_libc.c $(S)th8.h | $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(S)th8_libc.c

# ----------------------------------------------------------------
# Installation.
# ----------------------------------------------------------------

install: all
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCDIR)
	mkdir -p $(DESTDIR)$(MAN1DIR)
	mkdir -p $(DESTDIR)$(MAN3DIR)
	mkdir -p $(DESTDIR)$(DATADIR)
	cp $(STATIC_LIB) $(DESTDIR)$(LIBDIR)/
	cp $(SHARED_LIB) $(DESTDIR)$(LIBDIR)/
	cp $(S)th8.h $(S)th8_util.h $(DESTDIR)$(INCDIR)/
	# Ship the Win32-format icon alongside the package data so any
	# downstream consumer (desktop integration, packaging tooling,
	# embedded GUI shells) has a canonical file to reference.  POSIX
	# has no native binary-embedded icon mechanism, so the file
	# itself is the artifact.
	cp rc/th8.ico $(DESTDIR)$(DATADIR)/
	cp $(MAN1_SRCS) $(DESTDIR)$(MAN1DIR)/
	cp $(MAN3_SRCS) $(DESTDIR)$(MAN3DIR)/

# ----------------------------------------------------------------
# Man-page sanity check.
#
# `mandoc -Tlint` parses every man-page source as `man(7)`/`mdoc(7)`
# and reports on:
#   - unknown / misspelled macros
#   - unbalanced .nf/.fi, .RS/.RE, etc.
#   - missing/extraneous .TH/.SH/.Dt arguments
#   - bad cross-references
#   - non-portable constructs (groff-only extensions etc.)
# Output goes to stderr; the rule fails on parse errors but tolerates
# style warnings (which mandoc also surfaces).  Run `make manlint`
# directly for a verbose look, or rely on `all` to invoke it.
#
# We tolerate "mandoc not installed" as a soft skip so a build host
# without mandoc (rare on modern POSIX) does not break -- the check
# is a quality gate, not a hard build dependency.
# ----------------------------------------------------------------

MANDOC ?= mandoc

manlint:
	@if command -v $(MANDOC) >/dev/null 2>&1; then \
	    echo "Linting man pages with $(MANDOC) -Tlint -Wstyle ..."; \
	    $(MANDOC) -Tlint -Wstyle $(MAN_SRCS); \
	else \
	    echo "manlint: $(MANDOC) not installed, skipping."; \
	fi

.PHONY: manlint

# ----------------------------------------------------------------
# Clean.
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# Test shared libraries (for [load]/[unload] lifecycle testing).
#
# Two separate binaries from the same source file:
#
#   libth8test   -- TH8 mode (entry points: Th8test_Init/Unload)
#   libtclth8test -- Tcl mode (entry points: Tclth8test_Init/Unload)
#
# The Tcl integration libraries can be built with overridden
# values for the TCL_INCLUDE and TCL_STUBS_LIB locations:
#
#   make testlib TCL_INCLUDE=/opt/homebrew/include/tcl-tk \
#                TCL_STUBS_LIB=/opt/homebrew/lib/libtclstub8.6.a
# ----------------------------------------------------------------

TESTLIB_TH8 = $(B)libth8test$(SHLIB_EXT)
TESTLIB_TCL = $(B)libtclth8test$(SHLIB_EXT)

#
# Tcl auto-detection.  Searches in this order:
#   1. User-supplied TCL_INCLUDE / TCL_STUBS_LIB / TCL_LIB
#   2. Local externals/tcl (bundled headers + stubs)
#   3. tclsh-based detection (tclConfig.sh paths)
#   4. Common system paths (Homebrew, apt, etc.)
#
# Override by passing explicit values on the command line:
#   make TCL_INCLUDE=/path/to/include TCL_STUBS_LIB=/path/to/libtclstub.a
#

# Search helper: find the first existing path from a list.
_tcl_find = $(firstword $(wildcard $(1)))

# --- Priority 1: User-supplied (already set via command line or env) ---

# --- Priority 2: tclsh-based detection ---
ifndef TCL_INCLUDE
  _tcl_prefix := $(shell tclsh tools/tcl_detect.tcl prefix 2>/dev/null)
  ifneq (,$(_tcl_prefix))
    _tcl_inc_candidates := \
        $(_tcl_prefix)/include/tcl-tk \
        $(_tcl_prefix)/include \
        $(shell tclsh tools/tcl_detect.tcl include 2>/dev/null)
    TCL_INCLUDE := $(call _tcl_find,$(addsuffix /tcl.h,$(_tcl_inc_candidates)))
    ifneq (,$(TCL_INCLUDE))
      TCL_INCLUDE := $(patsubst %/tcl.h,%,$(TCL_INCLUDE))
    endif
  endif
endif

ifndef TCL_STUBS_LIB
  _tcl_libdir := $(shell tclsh tools/tcl_detect.tcl libdir 2>/dev/null)
  ifneq (,$(_tcl_libdir))
    TCL_STUBS_LIB := $(call _tcl_find,\
        $(_tcl_libdir)/libtclstub8.6.a \
        $(_tcl_libdir)/libtclstub8.7.a \
        $(_tcl_libdir)/libtclstub9.0.a \
        $(_tcl_libdir)/libtclstub.a)
  endif
endif

ifndef TCL_LIB
  ifneq (,$(_tcl_libdir))
    ifeq ($(UNAME_S),Darwin)
      TCL_LIB := $(call _tcl_find,\
          $(_tcl_libdir)/libtcl8.6.dylib \
          $(_tcl_libdir)/libtcl8.7.dylib \
          $(_tcl_libdir)/libtcl9.0.dylib)
    else
      TCL_LIB := $(call _tcl_find,\
          $(_tcl_libdir)/libtcl8.6.so \
          $(_tcl_libdir)/libtcl8.7.so \
          $(_tcl_libdir)/libtcl9.0.so)
    endif
  endif
endif

# --- Priority 3: Common system paths ---
ifndef TCL_INCLUDE
  _tcl_sys_inc := $(call _tcl_find,\
      /opt/homebrew/include/tcl-tk/tcl.h \
      /opt/homebrew/opt/tcl-tk/include/tcl.h \
      /opt/homebrew/opt/tcl-tk@8/include/tcl-tk/tcl.h \
      /usr/local/include/tcl8.6/tcl.h \
      /usr/include/tcl8.6/tcl.h \
      /usr/include/tcl/tcl.h \
      /usr/include/tcl.h)
  ifneq (,$(_tcl_sys_inc))
    TCL_INCLUDE := $(patsubst %/tcl.h,%,$(_tcl_sys_inc))
  endif
endif

ifndef TCL_STUBS_LIB
  TCL_STUBS_LIB := $(call _tcl_find,\
      /opt/homebrew/lib/libtclstub8.6.a \
      /opt/homebrew/opt/tcl-tk/lib/libtclstub8.6.a \
      /opt/homebrew/opt/tcl-tk@8/lib/libtclstub8.6.a \
      /usr/local/lib/libtclstub8.6.a \
      /usr/lib/x86_64-linux-gnu/libtclstub8.6.a \
      /usr/lib/libtclstub8.6.a)
endif

# --- Priority 4: Local externals/tcl (bundled, mainly for Windows) ---
ifndef TCL_INCLUDE
ifneq (,$(call _tcl_find,externals/tcl/include/tcl.h))
  TCL_INCLUDE := externals/tcl/include
endif
endif

ifndef TCL_STUBS_LIB
  TCL_STUBS_LIB := $(call _tcl_find,\
      externals/tcl/lib/libtclstub8.6.a \
      externals/tcl/lib/libtclstub.a)
endif

# --- Default to empty if nothing found ---
TCL_INCLUDE   ?=
TCL_STUBS_LIB ?=
TCL_LIB       ?=

# --- Report detection results ---
ifneq ($(TCL_INCLUDE),)
  $(info Tcl headers:  $(TCL_INCLUDE))
endif
ifneq ($(TCL_STUBS_LIB),)
  $(info Tcl stubs:   $(TCL_STUBS_LIB))
endif
ifneq ($(TCL_LIB),)
  $(info Tcl library: $(TCL_LIB))
endif

ifeq ($(UNAME_S),Darwin)
  TESTLIB_LINK = -dynamiclib -undefined dynamic_lookup
  SHELL_EXPORT = -Wl,-export_dynamic
else
  TESTLIB_LINK = -shared
  SHELL_EXPORT = -rdynamic
endif

TESTLIB_CFLAGS = $(CFLAGS) $(INCLUDES) -I$(S)test -fPIC \
  -Wno-missing-prototypes -Wno-strict-prototypes

# TH8 test library (always buildable).
testlib: $(TESTLIB_TH8)

$(B)th8_testlib_th8.pic.o: $(S)test/th8_testlib.c $(S)th8.h $(S)th8Decls.h \
	$(UTF_DIR)/ConvertUTF_v2.h | $(B)
	$(CC) $(TESTLIB_CFLAGS) -DTH8_TESTLIB_TH8 -DUSE_TH8_STUBS \
	  -c -o $@ $(S)test/th8_testlib.c

$(TESTLIB_TH8): $(B)th8_testlib_th8.pic.o $(B)ConvertUTF_v2.pic.o stubs | $(B)
	$(CC) $(CFLAGS) $(TESTLIB_LINK) -o $@ $(B)th8_testlib_th8.pic.o $(B)ConvertUTF_v2.pic.o $(STUBS_LIB)

# Tcl test library (requires TCL_INCLUDE and TCL_STUBS_LIB).
ifneq ($(TCL_INCLUDE),)
testlib: $(TESTLIB_TCL)

$(B)th8_testlib_tcl.pic.o: $(S)test/th8_testlib.c $(S)th8.h | $(B)
	$(CC) $(TESTLIB_CFLAGS) -DTH8_TESTLIB_TCL \
	  -I$(TCL_INCLUDE) -c -o $@ $(S)test/th8_testlib.c

$(TESTLIB_TCL): $(B)th8_testlib_tcl.pic.o | $(B)
	$(CC) $(CFLAGS) $(TESTLIB_LINK) -o $@ $(B)th8_testlib_tcl.pic.o $(TCL_STUBS_LIB)
endif

# ----------------------------------------------------------------
# SQLite key-value extension.
# ----------------------------------------------------------------

SQLITE3_DIR    = externals/sqlite3
SQLITE3_EXT    = $(B)libth8sqlite3$(SHLIB_EXT)

SQLITE3_CFLAGS = $(CFLAGS) $(INCLUDES) -I$(S)sqlite3 -I$(SQLITE3_DIR) \
                 -fPIC -DUSE_TH8_STUBS \
                 -Wno-missing-prototypes -Wno-strict-prototypes

SQLITE3_OPTS = -DSQLITE_ENABLE_API_ARMOR=1 \
               -DSQLITE_ENABLE_BYTECODE_VTAB=1 \
               -DSQLITE_ENABLE_COLUMN_METADATA=1 \
               -DSQLITE_ENABLE_DBPAGE_VTAB=1 \
               -DSQLITE_ENABLE_DBSTAT_VTAB=1 \
               -DSQLITE_ENABLE_FTS5=1 \
               -DSQLITE_ENABLE_LOAD_EXTENSION=1 \
               -DSQLITE_ENABLE_MATH_FUNCTIONS=1 \
               -DSQLITE_ENABLE_MEMORY_MANAGEMENT=1 \
               -DSQLITE_ENABLE_RTREE=1 \
               -DSQLITE_ENABLE_STAT4=1 \
               -DSQLITE_ENABLE_STMTVTAB=1 \
               -DSQLITE_ENABLE_UPDATE_DELETE_LIMIT=1 \
               -DSQLITE_SECURE_DELETE=1 \
               -DSQLITE_SOUNDEX=1 \
               -DSQLITE_THREADSAFE=1 \
               -DSQLITE_USE_URI=1

SQLITE3_OBJ_CFLAGS = -std=c99 -O2 -DNDEBUG $(SQLITE3_OPTS) -fPIC \
                      -Wno-long-long -Wno-unused-parameter \
                      -Wno-missing-prototypes -Wno-strict-prototypes \
                      -Wno-old-style-definition -Wno-declaration-after-statement \
                      -Wno-sign-compare -Wno-pedantic

sqlite3-ext: stubs $(SQLITE3_EXT)

$(B)th8_sqlite3.pic.o: $(S)sqlite3/th8_sqlite3.c $(S)sqlite3/th8_sqlite3.h \
    $(S)th8.h $(S)th8Decls.h $(SQLITE3_DIR)/sqlite3.h | $(B)
	$(CC) $(SQLITE3_CFLAGS) -c -o $@ $(S)sqlite3/th8_sqlite3.c

$(B)sqlite3.pic.o: $(SQLITE3_DIR)/sqlite3.c $(SQLITE3_DIR)/sqlite3.h | $(B)
	$(CC) $(SQLITE3_OBJ_CFLAGS) -c -o $@ $(SQLITE3_DIR)/sqlite3.c

$(SQLITE3_EXT): $(B)th8_sqlite3.pic.o $(B)sqlite3.pic.o stubs | $(B)
	$(CC) $(CFLAGS) $(TESTLIB_LINK) -o $@ \
	  $(B)th8_sqlite3.pic.o $(B)sqlite3.pic.o $(STUBS_LIB)

# ----------------------------------------------------------------
# Bridge library (TH8 <-> Tcl cross-interpreter evaluation).
#
# Two separate shared libraries:
#
#   libth8bridge_tcl  -- loaded into tclsh, provides [th8Eval]
#                        Links: libth8.a + libtclstub.a
#
#   libth8bridge_th8  -- loaded into th8sh, provides [tclEval]
#                        Uses reverse stubs to load Tcl at runtime.
#                        Links: libth8stub.a + libtclstub.a
#
# Usage:
#   make bridge TCL_INCLUDE=... TCL_STUBS_LIB=...
# ----------------------------------------------------------------

TCL_LIB ?=

BRIDGE_CFLAGS = $(CFLAGS) $(INCLUDES) -fPIC \
  -Wno-missing-prototypes -Wno-strict-prototypes

ifneq ($(TCL_INCLUDE),)

bridge: $(B)libth8bridge_tcl$(SHLIB_EXT)

$(B)th8_tcl_tclmode.pic.o: $(S)test/th8_tcl.c $(S)th8.h | $(B)
	$(CC) $(BRIDGE_CFLAGS) -DTH8_TCL_BRIDGE_TCL \
	  -I$(TCL_INCLUDE) -c -o $@ $(S)test/th8_tcl.c

$(B)libth8bridge_tcl$(SHLIB_EXT): $(B)th8_tcl_tclmode.pic.o $(CORE_OBJ_PIC) $(PLAT_OBJ_PIC) $(LIBC_OBJ_PIC) | $(B)
	$(CC) $(CFLAGS) $(TESTLIB_LINK) -o $@ $(B)th8_tcl_tclmode.pic.o \
	  $(CORE_OBJ_PIC) $(PLAT_OBJ_PIC) $(LIBC_OBJ_PIC) \
	  $(TCL_STUBS_LIB) -lm $(PLAT_LIBS)

# Reverse-stubs Tcl loader (generated from externals/mmm/tcl.c).

$(B)th8_tcl_stubs.c: externals/mmm/tcl.c tools/vendor_tcl.tcl | $(B)
	$(TCLSH) tools/vendor_tcl.tcl $(B)

$(B)th8_tcl_stubs.pic.o: $(B)th8_tcl_stubs.c | $(B)
	$(CC) $(BRIDGE_CFLAGS) -DUSE_TCL_STUBS -DENABLE_TCL_PRIVATE_STUBS \
	  -Wno-format -Wno-pedantic \
	  -I$(TCL_INCLUDE) -c -o $@ $(B)th8_tcl_stubs.c

# Bridge: Tcl-in-TH8 mode (reverse stubs -- no libtcl needed).

bridge: $(B)libth8bridge_th8$(SHLIB_EXT)

$(B)th8_tcl_th8mode.pic.o: $(S)test/th8_tcl.c $(S)th8.h $(S)th8Decls.h | $(B)
	$(CC) $(BRIDGE_CFLAGS) -DTH8_TCL_BRIDGE_TH8 -DUSE_TH8_STUBS \
	  -c -o $@ $(S)test/th8_tcl.c

$(B)libth8bridge_th8$(SHLIB_EXT): $(B)th8_tcl_th8mode.pic.o $(B)th8_tcl_stubs.pic.o stubs | $(B)
	$(CC) $(CFLAGS) $(TESTLIB_LINK) -o $@ $(B)th8_tcl_th8mode.pic.o \
	  $(B)th8_tcl_stubs.pic.o $(STUBS_LIB) $(TCL_STUBS_LIB) \
	  $(PLAT_LIBS)

else
bridge:
	@echo "Set TCL_INCLUDE and TCL_STUBS_LIB (and optionally TCL_LIB) to build the bridge."
endif

# ----------------------------------------------------------------

clean:
	rm -rf $(B)
	rm -rf $(STATIC_B)
	rm -rf $(AFL_B)
	rm -rf $(REGEX_BUILD)
	rm -rf $(BESTLINE_BUILD)
	rm -rf $(TOMMATH_BUILD)
	rm -f *.gcov

fresh-non-static: clean all-non-static
	@echo BUILT ALL-NON-STATIC FRESH AND CLEAN...

fresh: clean all
	@echo BUILT ALL FRESH AND CLEAN...

# ----------------------------------------------------------------
# Convenience targets.
# ----------------------------------------------------------------

debug:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG)" all

# ----------------------------------------------------------------
# Amalgamation.
#
# Produces a single th8.c file containing all TH8 core source.
# Compile with:
#   cc -DTH8_ENABLE_REGEXP -I. th8.c -lm
#
# The public header th8.h must be alongside or on the include path.
# ----------------------------------------------------------------

AMAL_FILE = $(B)th8.c

$(B)regex_amalg.c: regex_vendor | $(B)
	$(TCLSH) tools/regex_amalg.tcl $(B)

# ----------------------------------------------------------------
# Spilornis: rename conflicting types (WCHAR, DWORD, etc.) to se_*
# prefixed names so they don't collide with <windows.h>.
# ----------------------------------------------------------------

$(B)Spilornis.c $(B)Spilornis.h $(B)SpilornisInt.h $(B)SpilornisDef.h: \
    $(SPILORNIS_DIR)/Spilornis.c $(SPILORNIS_DIR)/Spilornis.h \
    $(SPILORNIS_DIR)/SpilornisInt.h $(SPILORNIS_DIR)/SpilornisDef.h \
    tools/mkspilornis.tcl | $(B)
	$(TCLSH) tools/mkspilornis.tcl $(SPILORNIS_DIR)/Spilornis.c $(B)Spilornis.c
	$(TCLSH) tools/mkspilornis.tcl $(SPILORNIS_DIR)/Spilornis.h $(B)Spilornis.h
	$(TCLSH) tools/mkspilornis.tcl $(SPILORNIS_DIR)/SpilornisInt.h $(B)SpilornisInt.h
	$(TCLSH) tools/mkspilornis.tcl $(SPILORNIS_DIR)/SpilornisDef.h $(B)SpilornisDef.h

spilornis_vendor: $(B)Spilornis.c

amalgamation: $(VERSIONHDR) $(B)regex_amalg.c $(B)tommath_amalg.c spilornis_vendor
	$(TCLSH) tools/mkamal.tcl -o $(AMAL_FILE)
	cp $(S)th8.h $(B)th8.h

#
# Amalgamation compile rules.  Only -I$(B) (for th8.h) and
# EXTLIB_INCLUDES (system libraries like OpenSSL) are used.
# No TH8-internal include paths are permitted -- this proves
# that th8.c + th8.h are fully self-contained.
#
$(B)th8_amal.o: $(AMAL_FILE) $(B)th8.h | $(B)
	$(CC) $(CFLAGS) -I$(B) $(EXTLIB_INCLUDES) $(REGEX_CFLAGS) \
	  -c -o $@ $(AMAL_FILE)

$(B)th8_amal.pic.o: $(AMAL_FILE) $(B)th8.h | $(B)
	$(CC) $(CFLAGS) -I$(B) $(EXTLIB_INCLUDES) $(REGEX_CFLAGS) \
	  $(PIC_DEFS) -c -o $@ $(AMAL_FILE)

amalgamation-shell: amalgamation $(B)bestline.o $(B)mimalloc_static.o
	$(CC) $(CFLAGS) -I$(B) $(EXTLIB_INCLUDES) $(REGEX_CFLAGS) \
	  -c -o $(B)th8_amal.o $(AMAL_FILE)
	$(CC) $(CFLAGS) -I$(B) $(EXTLIB_INCLUDES) \
	  -DTH8_AMALGAMATION \
	  -c -o $(B)th8sh_amal.o $(S)th8sh.c
	$(CC) $(CFLAGS) -I$(B) $(EXTLIB_INCLUDES) \
	  -DTH8_AMALGAMATION \
	  -I$(BESTLINE_BUILD) \
	  -c -o $(B)th8_shell_amal.o $(S)th8_shell.c
	$(CC) $(CFLAGS) $(SHELL_EXPORT) \
	  -o $(B)th8sh_amal \
	  $(B)th8_amal.o $(B)th8sh_amal.o $(B)th8_shell_amal.o $(B)bestline.o \
	  $(B)mimalloc_static.o \
	  -lm $(CURL_LIBS) $(CRYPTOGRAPHY_LIBS) $(UNBOUND_LIBS) $(PLAT_LIBS)

amalgamation-test: amalgamation-shell
	$(B)th8sh_amal tests/all.tcl

# ----------------------------------------------------------------
# Cosmopolitan APE build wrappers.
#
# These are thin convenience targets that delegate to
# Makefile.cosmopolitan after verifying the cosmocc toolchain is
# present (via tools/bootstrap_cosmo.sh --check).  Run
# `bash tools/bootstrap_cosmo.sh` once to install cosmocc; after
# that `make cosmo` and `make cosmo-test` work standalone.
#
#   make cosmo        Build bin-cosmo/th8sh.com (Actually Portable
#                     Executable) via Makefile.cosmopolitan.
#   make cosmo-test   Build the APE shell and run tests/all.tcl
#                     through it.  Honors ENABLE_TEST_KEY=1 the
#                     same way as the regular debug/test target.
#
# ----------------------------------------------------------------

cosmo:
	bash tools/bootstrap_cosmo.sh --check
	$(MAKE) -f Makefile.cosmopolitan fresh

cosmo-test: cosmo
	bin-cosmo/th8sh.com tests/all.tcl

# ----------------------------------------------------------------
# Memory checking targets.
#
# asan:      Build with AddressSanitizer (GCC/Clang).  Detects
#            heap buffer overflows, use-after-free, double-free,
#            stack buffer overflows, and memory leaks at exit.
#
# ubsan:     Build with UndefinedBehaviorSanitizer.  Detects
#            signed integer overflow, null pointer dereference,
#            misaligned access, and other UB.
#
# msan:      Build with MemorySanitizer (Clang only).  Detects
#            reads of uninitialized memory.
#
# sanitize:  Build with both ASan and UBSan.
#
# valgrind:  Run the test suite under valgrind memcheck (requires
#            a debug build and valgrind installed).
#
# Usage:
#   make asan                 Build with ASan
#   make sanitize             Build with ASan + UBSan
#   make sanitize-test        Build with ASan + UBSan, run tests
#   make valgrind             Run tests under valgrind
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# Profiling targets.
#
# profile-shell:  Build the shell with debug symbols for profiling.
# profile:        Run SCRIPT with sampling profiler (sample on macOS,
#                 perf on Linux).  Output is written to PROFILE_OUT.
# profile-mimalloc / profile-mimalloc-shell: Same but with mimalloc.
#
# Usage:
#   make profile-shell
#   make profile SCRIPT=tests/benchmark.tcl
#   make profile SCRIPT=tests/benchmark.tcl PROFILE_DURATION=15
#
#   make profile-mimalloc-shell
#   make profile-mimalloc SCRIPT=tests/benchmark.tcl
# ----------------------------------------------------------------

PROFILE_OUT      ?= profile_output.txt
PROFILE_DURATION ?= 10
PROFILE_SCRIPT   ?= $(SCRIPT)

profile-shell:
	$(MAKE) EXTRA_CFLAGS="-g" fresh shell

profile-mimalloc-shell:
	$(MAKE) EXTRA_CFLAGS="-g" ENABLE_MIMALLOC=1 fresh shell

#
# Run a script under the sampling profiler.  The script runs in the
# background; the profiler attaches by PID.
#
# macOS: uses "sample" (built-in, no install needed).
# Linux: uses "perf record" + "perf report" (requires perf).
#
profile:
ifeq ($(SCRIPT),)
	@echo "Usage: make profile SCRIPT=path/to/script.tcl"
	@echo "  Optional: PROFILE_DURATION=10  (seconds to sample)"
	@echo "            PROFILE_OUT=profile_output.txt"
	@exit 1
endif
ifeq ($(UNAME_S),Darwin)
	@echo "=== Profiling $(SCRIPT) for $(PROFILE_DURATION)s (macOS sample) ==="
	@TH8SH_NO_SCRIPT_SECURITY=1 $(SHELL_BIN) $(SCRIPT) & \
	  BGPID=$$!; \
	  sleep 1; \
	  sample $$BGPID $(PROFILE_DURATION) -f $(PROFILE_OUT); \
	  wait $$BGPID 2>/dev/null; \
	  echo ""; \
	  echo "=== Profile written to $(PROFILE_OUT) ==="; \
	  echo ""; \
	  echo "--- Top 30 hotspots (by top-of-stack) ---"; \
	  grep "Sort by top of stack" -A 35 $(PROFILE_OUT) | tail -31
else
	@echo "=== Profiling $(SCRIPT) for $(PROFILE_DURATION)s (Linux perf) ==="
	@TH8SH_NO_SCRIPT_SECURITY=1 perf record -g -o $(B)perf.data \
	  -- $(SHELL_BIN) $(SCRIPT); \
	  perf report -i $(B)perf.data --stdio --no-children \
	    --percent-limit 0.5 > $(PROFILE_OUT); \
	  echo ""; \
	  echo "=== Profile written to $(PROFILE_OUT) ==="; \
	  head -60 $(PROFILE_OUT)
endif

profile-mimalloc:
ifeq ($(SCRIPT),)
	@echo "Usage: make profile-mimalloc SCRIPT=path/to/script.tcl"
	@exit 1
endif
	$(MAKE) ENABLE_MIMALLOC=1 SCRIPT="$(SCRIPT)" profile

# ----------------------------------------------------------------
# Sanitizers (ASan, UBSan, MSan).
# ----------------------------------------------------------------

SANITIZE_ASAN  = -fsanitize=address -fno-omit-frame-pointer
SANITIZE_UBSAN = -fsanitize=undefined -fno-sanitize-recover=all
SANITIZE_MSAN  = -fsanitize=memory -fno-omit-frame-pointer
SANITIZE_ALL   = $(SANITIZE_ASAN) $(SANITIZE_UBSAN)

asan:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG) $(SANITIZE_ASAN)" all-non-static

ubsan:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG) $(SANITIZE_UBSAN)" all-non-static

msan:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG) $(SANITIZE_MSAN)" all-non-static

sanitize:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG) $(SANITIZE_ALL)" all-non-static

asan-test: asan
	TH8SH_YES_TESTLIB=1 \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 \
	  $(SHELL_BIN) tests/all.tcl

asan-test-macos: asan
	TH8SH_YES_TESTLIB=1 \
	  ASAN_OPTIONS=halt_on_error=0:detect_odr_violation=0 \
	  $(SHELL_BIN) tests/all.tcl

sanitize-test: sanitize
	TH8SH_YES_TESTLIB=1 \
	  ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 \
	  UBSAN_OPTIONS=print_stacktrace=1 \
	  $(SHELL_BIN) tests/all.tcl

sanitize-test-macos: sanitize
	TH8SH_YES_TESTLIB=1 \
	  ASAN_OPTIONS=halt_on_error=0:detect_odr_violation=0 \
	  UBSAN_OPTIONS=print_stacktrace=1 \
	  $(SHELL_BIN) tests/all.tcl

valgrind:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG)" all-non-static
	TH8SH_YES_TESTLIB=1 valgrind --leak-check=full \
	  --show-leak-kinds=all --track-origins=yes \
	  --suppressions=tools/data/th8.supp \
	  --error-exitcode=1 $(SHELL_BIN) tests/all.tcl

# ----------------------------------------------------------------
# CRT dependency audit.
#
# Scans all object files for unexpected C runtime function calls.
# Platform files, shell, and externals are allowed.  Core and
# plugin files must route through the platform abstraction layer.
# Compiler-generated calls (memcpy/memset for struct ops) and
# documented exceptions (abort in panic path, vsnprintf in trace)
# are whitelisted.
#
# check-crt:         Scan release build objects.
# check-crt-debug:   Scan debug build objects.
# ----------------------------------------------------------------

check-crt:
	$(TCLSH) tools/audit_patterns.tcl crt-objects $(B)

check-crt-debug:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG)" all-non-static
	$(TCLSH) tools/audit_patterns.tcl crt-objects $(B)

# ----------------------------------------------------------------
# Code coverage targets.
#
# coverage:       Build with gcov instrumentation and run tests.
# coverage-report: Generate per-file coverage summary.
# coverage-branches: Run the Tcl tool to identify uncovered branches.
# coverage-clean: Remove .gcda/.gcno/.gcov files.
#
# Usage:
#   make coverage               Build + run tests with gcov
#   make coverage-report        Per-file line/branch summary
#   make coverage-branches      Detailed uncovered branch report
#   make coverage-clean         Remove coverage data
# ----------------------------------------------------------------

COVERAGE_FLAGS = --coverage -fprofile-arcs -ftest-coverage

coverage:
	$(MAKE) CFLAGS="$(CFLAGS_DEBUG) $(COVERAGE_FLAGS)" shell
	TH8SH_YES_TESTLIB=1 $(SHELL_BIN) tests/all.tcl
	@echo ""
	@echo "Coverage data generated.  Run 'make coverage-report'"
	@echo "or 'make coverage-branches' to analyze."

coverage-report:
	@echo "=== TH8 Code Coverage Report ==="
	@echo ""
	@for f in $(S)/th8_core.c $(S)/th8_lang.c $(S)/th8_plat.c \
	          $(S)/th8_hash.c $(S)/th8_xlib.c $(S)/th8_regex.c; do \
	    gcov -b -o $(B) $$f 2>/dev/null | grep -A1 "^File" | \
	        grep -v "^--$$" ; \
	done
	@echo ""
	@echo "Detailed .gcov files are in the current directory."

coverage-branches:
	@for f in $(S)/th8_core.c $(S)/th8_lang.c $(S)/th8_plat.c \
	          $(S)/th8_hash.c $(S)/th8_xlib.c $(S)/th8_regex.c; do \
	    gcov -b -o $(B) $$f > /dev/null 2>&1 ; \
	done
	$(TCLSH) tools/coverage.tcl *.gcov

coverage-clean:
	rm -f $(B)*.gcda $(B)*.gcno *.gcov

# ----------------------------------------------------------------
# MC/DC (Modified Condition / Decision Coverage) targets.
#
# Uses LLVM's source-based coverage instrumentation with the
# -fcoverage-mcdc extension.  Reports a per-file MC/DC percentage
# and a per-line "uncovered condition" breakdown.
#
# In-scope files are non-plugin sources only (the runtime / core /
# platform layer); plugins, the shell, the SQLite glue, and the
# test infrastructure are excluded per the project's MC/DC scope.
#
# Usage:
#   make mcdc            Build with MC/DC instrumentation, run tests
#   make mcdc-report     Per-file MC/DC summary (table)
#   make mcdc-uncovered  Per-line uncovered condition listing
#   make mcdc-clean      Remove MC/DC profile data
# ----------------------------------------------------------------

#
# MCDC_FLAGS includes -DTH8_OMIT_AUXILIARY_SAFETY_CHECKS so NEVER(X)
# and ALWAYS(X) macros expand to compile-time constants 0 / 1.
# Surrounding short-circuit boolean expressions then fold via the
# compiler's constant-propagation pass and the dead branches
# disappear from the MC/DC coverage map entirely -- the SQLite
# convention for excluding deactivated defensive code from MC/DC
# accounting (see SQLite's SQLITE_OMIT_AUXILIARY_SAFETY_CHECKS).
# Without this define, every NEVER()/ALWAYS()-guarded condition
# would count as an uncovered MC/DC operand, inflating the missed-
# conditions count for code that is defensive-only by design.
#
#
# Note: `-fprofile-instr-generate=PATH` (with =PATH) bakes the
# default profile-output path into the instrumented binary.
# The runtime falls back to this path when LLVM_PROFILE_FILE
# is not set in the environment -- without it, an unwrapped
# direct invocation (e.g. `./bin/th8sh tests/foo.tcl` from the
# project root) would write `default.profraw` to cwd.  With it,
# stray invocations land their .profraw next to the build
# artefacts under bin/, which `mcdc-clean` already covers.
# `$(abspath ...)` is used so the path is correct regardless
# of the cwd at run time.
#
MCDC_FLAGS    = -fprofile-instr-generate=$(abspath $(B))/mcdc-%p.profraw \
		-fcoverage-mapping \
		-fcoverage-mcdc -DTH8_OMIT_AUXILIARY_SAFETY_CHECKS
MCDC_PROFRAW  = $(B)mcdc.profraw
MCDC_PROFDATA = $(B)mcdc.profdata

# Source files in scope for MC/DC measurement.  Includes the core,
# the platform layers, and every plugin.  Excludes the shell
# (th8sh.c main wrapper and th8_shell.c shell-support translation
# unit) so MC/DC focuses on the embeddable library, matching
# SQLite's convention of excluding shell.c from the coverage
# numerator.  Shell glob; expanded by the recipe.
#
# th8_win32.c is also excluded: it is Windows-only and llvm-cov
# MC/DC instrumentation is impractical to run there (clang/clang-cl
# coverage on Windows is documented as unreliable for MC/DC and the
# CI matrix here does not currently include a Windows MC/DC build).
# Treating it as exempt avoids penalising the per-platform-merged
# coverage number for code we cannot measure.
#
# th8_mimalloc.c is the vendored mimalloc allocator integration.  We
# do not maintain that code; including it in the MC/DC numerator
# would just measure how thoroughly tests/all.tcl exercises a third-
# party allocator we treat as a black box.  Excluded for the same
# reason vendored upstream code typically is.  See FINDINGS.md
# Finding 001 (2026-06-15 / 2026-06-16 batch) for the audit that
# identified the three uncovered decisions inside this file.
MCDC_SOURCES  = $(filter-out \
    $(S)/th8sh.c $(S)/th8_shell.c $(S)/th8_win32.c \
    $(S)/th8_mimalloc.c, \
    $(wildcard $(S)/th8_*.c) $(wildcard $(S)/th8StubInit.c) \
    $(wildcard $(S)/plugins/th8_*.c) \
    $(wildcard $(S)/plugins/crypto/th8_*.c) \
    $(wildcard $(S)/plugins/harpy/th8_*.c))

mcdc:
	$(MAKE) clean
	#
	# Note: CFLAGS=... on the submake's command line OVERRIDES
	# the submake's own concatenation of TEST_DEFS / CRYPTO_DEFS /
	# FAULT_DEFS into CFLAGS.  We need TH8_ENABLE_TEST_KEY embedded
	# so the test-signing key is compiled in (otherwise tests/*.tcl
	# fail signature verification under signed-only and the run
	# tries to fetch the production key from the network).  Inline
	# the define here.  ENABLE_TEST_KEY=1 is still passed for any
	# Makefile-level conditionals that consume it apart from
	# CFLAGS.
	#
	#
	# Log path is OUTSIDE $(B) because `fresh` begins with
	# `clean` which `rm -rf`s $(B) -- a log file inside $(B)
	# would be orphaned by the directory removal even though
	# tee's fd kept it alive in-process.  `logs/` is a peer of
	# `bin/` that the build system never wipes; each mcdc run
	# overwrites mcdc-build.log in place.
	#
	@mkdir -p logs
	$(MAKE) ENABLE_TEST_KEY=1 CC=clang \
	    CFLAGS="$(CFLAGS_DEBUG) $(MCDC_FLAGS) -DTH8_ENABLE_TEST_KEY" \
	    LDFLAGS="$(MCDC_FLAGS)" fresh shell 2>&1 \
	  | tee logs/mcdc-build.log
	#
	# Audit: clang's `-fcoverage-mcdc` emits the unflagged
	# warning "unsupported MC/DC boolean expression; contains an
	# operation with a nested boolean expression. Expression will
	# not be covered" when a decision exceeds clang's MC/DC
	# truth-table representation (~6 conditions per decision plus
	# specific nesting limits).  When that fires, the decision
	# is SILENTLY ABSENT from the coverage map -- worse than
	# 50% MC/DC, it is unreported.  Vendored externals (regex,
	# spilornis, tommath, ...) are excluded; TH8 code is not.
	#
	@if grep -E "^[^/].*\.c.*: warning: unsupported MC/DC boolean expression" \
	    logs/mcdc-build.log \
	  | grep -v "^externals/" \
	  | grep -v "^bin/Spilornis\.c"; \
	then \
	  echo ""; \
	  echo "ERROR: clang emitted at least one 'unsupported MC/DC"; \
	  echo "boolean expression' warning in TH8 code.  The"; \
	  echo "corresponding decision is silently absent from the"; \
	  echo "coverage map.  Refactor the decision into smaller"; \
	  echo "pieces (split compound conditions, lift nested"; \
	  echo "ternaries) so clang's MC/DC instrumentation can"; \
	  echo "represent it.  See FINDINGS.md Finding 005."; \
	  exit 1; \
	fi
	#
	# Notes:
	#   * The LLVM profile runtime writes .profraw via an atexit
	#     hook.  Test subprocesses (spawned by [exec], fault
	#     injection sweeps, fork tests, etc.) inherit
	#     LLVM_PROFILE_FILE; without %p they would all write to
	#     the same path and overwrite each other.  Using %p
	#     gives each PID a distinct file; llvm-profdata merge
	#     then combines them.
	#
	#   * The macOS shell expands %p in single-quoted strings,
	#     so we build the env value via printf to keep the
	#     literal '%p' intact for the LLVM runtime to consume.
	#
	#   * Both the parent th8sh and the test subprocesses must
	#     have signal handlers disabled while the LLVM profile
	#     runtime is active: the runtime installs its own atexit
	#     flush, and any TH8 fatal/Ctrl-C handler firing during
	#     that window can short-circuit the flush before profile
	#     data hits disk.  TH8SH_NO_FATAL_HANDLER and
	#     TH8SH_NO_CTRLC_HANDLER suppress the relevant handlers.
	#
	LLVM_PROFILE_FILE=$$(printf '$(B)mcdc-%%p.profraw') \
	    TH8SH_YES_TESTLIB=1 \
	    TH8SH_NO_FATAL_HANDLER=1 \
	    TH8SH_NO_CTRLC_HANDLER=1 \
	    $(SHELL_BIN) tests/all.tcl
	xcrun llvm-profdata merge -sparse $(B)mcdc-*.profraw \
	    -o $(MCDC_PROFDATA)
	rm -f $(B)mcdc-*.profraw
	@echo ""
	@echo "MC/DC data generated.  Run 'make mcdc-report' or"
	@echo "'make mcdc-uncovered' to analyze."

mcdc-report:
	@echo "=== TH8 MC/DC Coverage Report ==="
	@echo ""
	#
	# Note: the core / platform / plugin sources live in
	# bin/libth8.dylib (built shared by default).  llvm-cov can only
	# read the coverage map from the binary that contains it, so the
	# dylib must be passed via -object alongside the shell binary.
	# Without -object, only sources linked directly into bin/th8sh
	# (th8sh.c, th8_shell.c, bestline.c) appear in the report.
	#
	xcrun llvm-cov report -instr-profile=$(MCDC_PROFDATA) \
	    -show-mcdc-summary $(SHELL_BIN) -object $(SHARED_LIB) \
	    $(MCDC_SOURCES)

mcdc-uncovered:
	@echo "=== TH8 MC/DC Uncovered Conditions ==="
	@echo ""
	xcrun llvm-cov show -instr-profile=$(MCDC_PROFDATA) \
	    -show-mcdc -show-mcdc-summary \
	    -region-coverage-lt=100 \
	    $(SHELL_BIN) -object $(SHARED_LIB) $(MCDC_SOURCES)

mcdc-clean:
	rm -f $(MCDC_PROFRAW) $(MCDC_PROFDATA) $(B)*.profraw
