###############################################################################
#
# test.tcl --
#
# Tcl Language Standard
# Test Suite Infrastructure Package File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::th8test {
  package require th8

  if {[isEagle]} then {
    package require Eagle.Test

    catch {rename isAdministrator ""}
    catch {rename initializeTests ""}
    catch {rename haveConstraint ""}
    catch {rename testConstraint ""}
    catch {rename test ""}
    catch {rename runTest ""}
    catch {rename runAllTests ""}
    catch {rename cleanupTests ""}
  }

  variable total 0
  variable passed 0
  variable failed 0
  variable mutated 0
  variable skipped 0

  variable failedNames [list]
  variable mutatedNames [list]
  variable skippedNames [list]

  variable engine ""

  variable path ""
  variable binPath ""
  variable file ""

  variable verbose 1

  #
  # ldifferences --
  #
  #   Compute the symmetric difference between two lists.  Returns
  #   a list of two-element sub-lists: elements present in list1
  #   but not list2 are tagged {MISSING <element>}, and elements
  #   present in list2 but not list1 are tagged {EXTRA <element>}.
  #   Used by the test framework to detect resource mutations
  #   (leaked or deleted procedures and variables, etc) after each
  #   test body executes.
  #
  proc ldifferences { list1 list2 } {
    set result [list]

    foreach element $list1 {
      if {[lsearch -exact $list2 $element] == -1} then {
        lappend result [list MISSING $element]
      }
    }

    foreach element $list2 {
      if {[lsearch -exact $list1 $element] == -1} then {
        lappend result [list EXTRA $element]
      }
    }

    return $result
  }

  #
  # normalizeFloat --
  #
  #   Return a floating-point VALUE using the trailing-".0" string
  #   representation that native Tcl and TH8 produce for a whole
  #   double (e.g. "3.0").  Eagle's managed (.NET) value model
  #   renders a whole double as a bare integer ("3"), so ONLY under
  #   Eagle is the value reformatted via [format] to the requested
  #   number of decimal places.  Under Tcl and TH8 the value is
  #   returned untouched, so those engines still verify the native
  #   float rendering the test exists to check; the [format] gate
  #   applies to Eagle alone.
  #
  proc normalizeFloat { value {precision 1} } {
    if {[isEagle]} then {
      return [format "%.${precision}f" $value]
    }

    return $value
  }

  #
  # isAdministrator --
  #
  #   Determine whether the current process is running with
  #   elevated privileges (root on Unix, Administrator on
  #   Windows).  Delegates to ::th8testlib::isadmin when the
  #   TH8 testlib extension is loaded; returns false otherwise.
  #   Several constraints (e.g. symlink_allowed) depend on this
  #   result.
  #
  proc isAdministrator {} {
    if {[llength [info commands ::th8testlib::isadmin]] > 0} then {
      return [::th8testlib::isadmin]
    }

    return false
  }

  #
  # setupSystemConstraints --
  #
  #   Register the baseline engine-identity constraints.  Sets
  #   "tcl" (always true -- every engine speaks Tcl), "th8"
  #   (true when running under the TH8 interpreter), "eagle"
  #   (true under the Eagle CLR bridge), "nulleval" (true when
  #   the engine supports null-body evaluation, currently TH8
  #   only), and "tip440" (true when ::tcl_platform(engine) is
  #   present, per TIP #440).  These constraints allow tests to
  #   be skipped on engines that lack the feature under test.
  #
  proc setupSystemConstraints {} {
    testConstraint tcl 1; # Everything is Tcl, I guess.
    testConstraint standard 1; # Indicates Tcl Language Standard v1.
    testConstraint th8 [isTh8]
    testConstraint eagle [isEagle]
    testConstraint nulleval [isTh8]
    testConstraint tip440 [info exists ::tcl_platform(engine)]
  }

  #
  # setupLoadConstraints --
  #
  #   Ensure the TH8 native testlib extension is loaded.  Probes
  #   for th8testlib::nop to see if the library is already
  #   present; if not, attempts to load the platform-appropriate
  #   shared library (th8test.dll on Windows, libth8test.dylib on
  #   macOS, libth8test.so on Linux).  This is the TH8-specific
  #   testlib, not the Tcl bridge testlib, and it provides
  #   commands used by later constraint-setup procs (e.g.
  #   key_token, signed_only, fault, sandbox, symlink, glob).
  #
  proc setupLoadConstraints {} {
    #
    # Try to ensure the TH8 testlib (not the Tcl bridge testlib) is
    # loaded when crypto commands are needed.
    #
    if {[catch {th8testlib::nop}] != 0} then {
      catch {load bin/th8test.dll:Th8test}
      catch {load bin/libth8test.dylib:Th8test}
      catch {load bin/libth8test.so:Th8test}
    }
  }

  #
  # setupCryptoConstraints --
  #
  #   Register constraints related to the cryptographic-signing
  #   subsystem.  Sets "crypto" (true when ENABLE_CRYPTOGRAPHY
  #   appears in the compile options), "crypto_testlib" (true when
  #   the testlib can retrieve signing-key tokens via key_token),
  #   "crypto_disabled" (true when the signed-only enforcement
  #   mode is off), "crypto_enabled" (true when signed-only
  #   enforcement is active), and "test_key" (true when the
  #   interpreter was built with ENABLE_TEST_KEY, i.e. the
  #   embedded test signing key is compiled in).  Tests for Harpy
  #   certificate verification, key management, and signed-script
  #   loading depend on these constraints.  Test-key-only testlib
  #   fixtures (e.g. the RSA-key / policy null_guard sweeps, which
  #   are compiled under TH8_ENABLE_CRYPTOGRAPHY &&
  #   TH8_ENABLE_TEST_KEY) must list "test_key" in addition to
  #   "crypto_enabled", otherwise they fail with "unknown
  #   subcommand" on a crypto build that lacks the test key.
  #
  proc setupCryptoConstraints {} {
    testConstraint crypto [expr {
      [info exists ::tcl_platform(compileOptions)] &&
      [lsearch -exact $::tcl_platform(compileOptions) ENABLE_CRYPTOGRAPHY] >= 0
    }]

    testConstraint crypto_testlib [expr {
      [catch {::th8testlib::key_token key0}] == 0
    }]

    testConstraint crypto_disabled [expr {
      [catch {::th8testlib::signed_only query} msg] == 1 || \
          $msg == "0" || [string length [unset msg]] > 0
    }]

    testConstraint crypto_enabled [expr {
      [catch {::th8testlib::signed_only query} msg] == 0 && \
          $msg == "1" && [string length [unset msg]] == 0
    }]

    testConstraint test_key [expr {
      [info exists ::tcl_platform(compileOptions)] &&
      [lsearch -exact $::tcl_platform(compileOptions) ENABLE_TEST_KEY] >= 0
    }]
  }

  #
  # setupFaultConstraints --
  #
  #   Register the "fault_injection" constraint, which is true
  #   only when the interpreter was compiled with
  #   ENABLE_FAULT_INJECTION and the ::th8testlib::fault command
  #   is available.  Fault-injection tests use this constraint to
  #   skip gracefully on release builds that lack the ability to
  #   simulate allocation failures and other internal errors.
  #
  proc setupFaultConstraints {} {
    testConstraint fault_injection [expr {
      [info exists ::tcl_platform(compileOptions)] &&
      [lsearch -exact $::tcl_platform(compileOptions) \
          ENABLE_FAULT_INJECTION] >= 0 &&
      [llength [info commands ::th8testlib::fault]] > 0
    }]
  }

  #
  # setupSqliteConstraints --
  #
  #   Register constraints for SQLite-backed features.  Sets
  #   "json" (true when the [json] command is available,
  #   indicating the SQLite extension has been loaded) and
  #   "kv_sqlite" (true when both ::th8testlib::kv and [json]
  #   are present, enabling key-value store tests).  The SQLite
  #   extension is NOT auto-loaded here because it modifies the
  #   interpreter's platform state; the test that needs it must
  #   load the extension explicitly before sourcing prologue.tcl.
  #
  proc setupSqliteConstraints {} {
    #
    # Check if the SQLite extension is already loaded (e.g.,
    # by the caller before sourcing prologue.tcl).  Do NOT
    # auto-load it here -- the extension modifies the
    # interpreter's platform and must be loaded explicitly
    # by the test that needs it.
    #
    testConstraint json [expr {[llength [info commands json]] > 0}]

    testConstraint kv_sqlite [expr {
      [llength [info commands ::th8testlib::kv]] > 0 && \
          [llength [info commands json]] > 0
    }]

    testConstraint secure_persist [expr {
      [llength [info commands ::th8testlib::secure_persist]] > 0 && \
          [llength [info commands ::th8testlib::kv]] > 0
    }]
  }

  #
  # setupFileConstraints --
  #
  #   Register constraints for filesystem-related features and
  #   create test fixtures used by file tests.  Sets "symlink"
  #   (true when ::th8testlib::symlink is available) and
  #   "symlink_allowed" (true on non-Windows or when running as
  #   Administrator, since Windows requires elevated privileges
  #   for symlink creation).  When both constraints are met,
  #   creates a file symlink (_th8test_flink -> th8sh) and a
  #   directory symlink (_th8test_dlink -> ..) in the binary
  #   directory for use by symlink tests.  Also sets "harpy_sign"
  #   and loads a test signing key when load_snk, [harpy], and
  #   signed-only mode are all available.
  #
  proc setupFileConstraints {} {
    testConstraint symlink [expr {
      [llength [info commands ::th8testlib::symlink]] > 0
    }]

    testConstraint symlink_allowed [expr {
      ![isWindows] || [isAdministrator]
    }]

    if {[testConstraint symlink] && [testConstraint symlink_allowed]} then {
      catch {; # file system
        ::th8testlib::symlink create th8sh [file nativename \
            [file join $::th8test::binPath _th8test_flink]]
      }

      catch {; # file system
        ::th8testlib::symlink create .. [file nativename \
            [file join $::th8test::binPath _th8test_dlink]]
      }
    }

    if {[llength [info commands ::th8testlib::load_snk]] > 0 && \
        [llength [info commands harpy]] > 0 && \
        [::th8testlib::signed_only query]} then {
      catch {
        unset -nocomplain ::_harpyToken

        set ::_harpyToken [::th8testlib::load_snk \
            tests/helpers/th8_test_key.snk]

        testConstraint harpy_sign 1
      }
    }
  }

  #
  # hasSubCommand --
  #
  # Check whether a command has a particular sub-command.  Uses
  # [info subcommands] on TH8/Eagle (fast, reliable).  Falls back
  # to invoking the command with a known-bad sub-command and
  # parsing the error message on native Tcl (which lacks
  # [info subcommands]).
  #
  proc hasSubCommand {cmd sub} {
    if {[llength [info commands $cmd]] == 0} then {
      return false
    }

    if {[catch {info subcommands $cmd} subs] == 0} then {
      return [expr {[lsearch -exact $subs $sub] >= 0}]
    }

    #
    # Tcl fallback: invoke with an invalid sub-command and check
    # whether the real sub-command appears in the error text.
    # The error from ensemble dispatch typically lists valid
    # sub-commands.
    #
    if {[catch {$cmd __th8_probe_nonexistent__} msg]} then {
      return [expr {[string first $sub $msg] >= 0}]
    }

    return false
  }

  #
  # setupCommandConstraints --
  #
  #   Register a constraint for each optional command that may or
  #   may not be compiled into a given engine.  Each constraint is
  #   named after the command it gates (e.g. "regexp", "exec",
  #   "coroutine") and is true when [info commands] finds that
  #   command.  Testlib-provided commands are checked under their
  #   fully qualified names (e.g. ::th8testlib::sandbox).
  #   Compound constraints are also registered for features that
  #   require two commands together: "switch_regexp" (switch +
  #   regexp), "switch_glob" (switch alone, since glob matching
  #   is built into switch), and "lsearch_regexp" (lsearch +
  #   regexp).
  #
  proc setupCommandConstraints {} {
    testConstraint flags [expr {[llength [info commands flags]] > 0}]
    testConstraint close [expr {[llength [info commands close]] > 0}]
    testConstraint downlevel [expr {[llength [info commands downlevel]] > 0}]
    testConstraint napply [expr {[llength [info commands napply]] > 0}]
    testConstraint nproc [expr {[llength [info commands nproc]] > 0}]
    testConstraint regexp [expr {[llength [info commands regexp]] > 0}]
    testConstraint regsub [expr {[llength [info commands regsub]] > 0}]
    testConstraint scan [expr {[llength [info commands scan]] > 0}]
    testConstraint tclEval [expr {[llength [info commands tclEval]] > 0}]
    testConstraint th8Eval [expr {[llength [info commands th8Eval]] > 0}]
    testConstraint concat [expr {[llength [info commands concat]] > 0}]
    testConstraint coroutine [expr {[llength [info commands coroutine]] > 0}]
    testConstraint exec [expr {[llength [info commands exec]] > 0}]
    testConstraint flush [expr {[llength [info commands flush]] > 0}]
    testConstraint gets [expr {[llength [info commands gets]] > 0}]
    testConstraint puts [expr {[llength [info commands puts]] > 0}]
    testConstraint seek [expr {[llength [info commands seek]] > 0}]
    testConstraint tell [expr {[llength [info commands tell]] > 0}]
    testConstraint subst [expr {[llength [info commands subst]] > 0}]
    testConstraint time [expr {[llength [info commands time]] > 0}]
    testConstraint base64 [expr {[llength [info commands base64]] > 0}]
    testConstraint lremove [expr {[llength [info commands lremove]] > 0}]

    #
    # Extended math functions present in TH8/Eagle (and Tcl 8.7+/9)
    # but not in the Tcl 8.6 reference: gate each test that calls one
    # so it skips on engines whose [expr] lacks the function.  Detect
    # via [info functions] (the math-function introspection) -- under
    # TH8 these resolve dynamically and are NOT listed by
    # [info commands ::tcl::mathfunc::*], so an info-commands probe
    # would wrongly skip them under TH8.
    #
    testConstraint mathfunc_pi [expr {
      [lsearch -exact [info functions] pi] >= 0
    }]
    testConstraint mathfunc_random [expr {
      [lsearch -exact [info functions] random] >= 0
    }]
    testConstraint mathfunc_epsilon [expr {
      [lsearch -exact [info functions] epsilon] >= 0
    }]

    testConstraint bigint_toggle [expr {
      [llength [info commands ::th8testlib::bigint]] > 0
    }]

    testConstraint chan [expr {
      [llength [info commands ::th8testlib::chan]] > 0
    }]

    testConstraint fuzz [expr {
      [llength [info commands ::th8testlib::fuzz]] > 0
    }]

    testConstraint glob [expr {
      [llength [info commands ::th8testlib::glob]] > 0
    }]

    testConstraint sandbox [expr {
      [llength [info commands ::th8testlib::sandbox]] > 0
    }]

    testConstraint queue_event [expr {
      [llength [info commands ::th8testlib::queue_event]] > 0
    }]

    testConstraint array_searches [expr {
      [llength [info commands ::th8testlib::array_searches]] > 0
    }]

    testConstraint switch_regexp [expr {
      [llength [info commands switch]] > 0 && \
          [llength [info commands regexp]] > 0
    }]

    testConstraint switch_glob [expr {
      [llength [info commands switch]] > 0
    }]

    testConstraint lsearch_regexp [expr {
      [llength [info commands lsearch]] > 0 && \
          [llength [info commands regexp]] > 0
    }]
  }

  #
  # setupSubCommandConstraints --
  #
  #   Register testConstraints for sub-commands that may not be
  #   available in all engines (native Tcl, Eagle, TH8).  Each
  #   constraint is named after the sub-command using underscores,
  #   e.g. "clock_seconds", "file_normalize", "flags_show".
  #
  #   Uses hasSubCommand which works across TH8, Eagle (via
  #   [info subcommands]) and native Tcl (via error-message probe).
  #
  #   Tests that require both the sub-command AND a feature gate
  #   (e.g. crypto_enabled) should list both constraints in their
  #   -constraints block, not rely on a composite check here.
  #
  proc setupSubCommandConstraints {} {
    testConstraint clock_seconds [hasSubCommand clock seconds]
    testConstraint clock_ntp [hasSubCommand clock ntp]
    testConstraint clock_https [hasSubCommand clock https]

    #
    # [info] sub-commands that exist in TH8/Eagle but not in the Tcl
    # 8.6 reference ensemble.  hasSubCommand uses [info subcommands]
    # where available and falls back to the invalid-sub-command error
    # probe on native Tcl.  (info_subcommands itself is registered in
    # setupCommonConstraints below.)
    #
    testConstraint info_expansions [hasSubCommand info expansions]
    testConstraint info_breakpoints [hasSubCommand info breakpoints]

    testConstraint file_exists [hasSubCommand file exists]
    testConstraint file_normalize [hasSubCommand file normalize]
    testConstraint file_split [hasSubCommand file split]
    testConstraint file_tempname [hasSubCommand file tempname]
    testConstraint file_channels [hasSubCommand file channels]
    testConstraint file_under [hasSubCommand file under]
    testConstraint file_validname [hasSubCommand file validname]
    testConstraint file_rootpath [hasSubCommand file rootpath]

    testConstraint flags_have [hasSubCommand flags have]
    testConstraint flags_change [hasSubCommand flags change]
    testConstraint flags_show [hasSubCommand flags show]

    testConstraint harpy_sign [hasSubCommand harpy sign]
    testConstraint harpy_verify [hasSubCommand harpy verify]

    testConstraint hash_sha512 [hasSubCommand hash sha512]

    testConstraint secure_create [hasSubCommand secure create]
    testConstraint secure_save [hasSubCommand secure save]
    testConstraint secure_load [hasSubCommand secure load]

    testConstraint interp_cancel [hasSubCommand interp cancel]

    testConstraint package_scan [hasSubCommand package scan]
  }

  #
  # setupNamespaceConstraints --
  #
  #   Register constraints for namespace sub-commands that vary
  #   across engines.  Each constraint is probed by actually
  #   executing the sub-command inside a temporary namespace and
  #   checking whether it succeeds.  Registers: "namespace_children",
  #   "namespace", "namespace_code", "namespace_export",
  #   "namespace_import", "namespace_parent", and
  #   "namespace_variable".  Temporary namespaces created during
  #   probing (e.g. ::_prologue_nc_test, ::_prologue_exp_test,
  #   ::_prologue_imp_src, ::_prologue_imp_dst,
  #   ::_prologue_var_test) are deleted after each probe.
  #
  proc setupNamespaceConstraints {} {
    testConstraint namespace_children [expr {
      [catch {namespace children ::}] == 0
    }]

    testConstraint namespace [expr {
      [catch {namespace current}] == 0
    }]

    testConstraint namespace_code [expr {
      [catch {namespace eval ::_prologue_nc_test {
          namespace code foo
      }}] == 0
    }]

    catch {namespace delete ::_prologue_nc_test}

    testConstraint namespace_export [expr {
      [catch {namespace eval ::_prologue_exp_test {
          proc _f {} {}; namespace export _f
      }}] == 0
    }]

    catch {namespace delete ::_prologue_exp_test}

    testConstraint namespace_import [expr {
      [catch {
        namespace eval ::_prologue_imp_src {
          proc _g {} {return ok}; namespace export _g
        }

        namespace eval ::_prologue_imp_dst {
          namespace import ::_prologue_imp_src::_g
        }
      }] == 0
    }]

    catch {namespace delete ::_prologue_imp_dst}
    catch {namespace delete ::_prologue_imp_src}

    testConstraint namespace_parent [expr {
      [catch {namespace parent}] == 0
    }]

    testConstraint namespace_variable [expr {
      [catch {namespace eval ::_prologue_var_test {
          variable _v 1
      }}] == 0
    }]

    catch {namespace delete ::_prologue_var_test}
  }

  #
  # setupCurlConstraints --
  #
  #   Register the "libcurl" constraint, which is true when the
  #   interpreter can fetch scripts over HTTPS via libcurl.  If
  #   the constraint has not already been set, attempts to source
  #   a remote test script from script.eagle.to over HTTPS.
  #   Success means libcurl (or an equivalent transport) is
  #   functional; the helloWorld proc left behind by the remote
  #   script is cleaned up immediately.  The guard on
  #   [haveConstraint libcurl] prevents redundant network probes
  #   if this proc is called more than once.
  #
  proc setupCurlConstraints {} {
    if {![haveConstraint libcurl]} then {
      testConstraint libcurl [expr {
        [catch {
          source https://script.eagle.to/scripts/secureTest.eagle
        }] == 0
      }]

      catch {rename helloWorld ""}
    }

    if {[testConstraint clock_https] && \
        ![haveConstraint clock_https_network]} then {
      testConstraint clock_https_network [expr {
        [catch {clock https}] == 0
      }]
    }

    if {[testConstraint clock_ntp] && \
        ![haveConstraint clock_ntp_network]} then {
      testConstraint clock_ntp_network [expr {
        [catch {clock ntp -timeout 1000}] == 0
      }]
    }
  }

  #
  # setupCommonConstraints --
  #
  #   Register a broad set of general-purpose constraints that
  #   probe for optional language features, command options, and
  #   behavioral quirks across engines.  Each constraint is
  #   tested by executing a representative operation and checking
  #   for success.  Includes: arithmetic overflow detection
  #   (overflow_check), regexp/regsub options (regsub_nocase,
  #   lookbehind, constraintEscape, regexp_all_inline,
  #   regexp_nocase, posixCharClass, areLongestMatch), lreplace
  #   edge cases (lreplaceAppend), array operations (array_set),
  #   info subcommands support (info_subcommands), clock
  #   availability (clock), Unicode \U escape support (escapeU),
  #   test_only_exec availability, file sub-commands
  #   (file_normalize, file_split), format specifier variants
  #   (format_hash_flag, format_star_precision, format_star_width),
  #   package sub-commands (package, package_forget,
  #   package_ifneeded, package_present, package_require,
  #   package_scan, package_unknown, package_vcompare,
  #   package_vsatisfies), pid command (pid), return option
  #   handling (return_errorinfo), string sub-command options
  #   (string_compare_length, string_compare_nocase,
  #   string_first_start, string_is, string_last, string_map,
  #   string_map_nocase, string_repeat, string_reverse,
  #   string_tolower_range, string_toupper_range), TIP #712 subst
  #   -variables flag (tip712), tailcall support (tailcall),
  #   leading-zero octal parsing (leadingOctal), TIP #285 interp
  #   cancel (tip285), C99 math functions (c99math), and
  #   arbitrary-precision integers (bigint).  Temporary procs and
  #   packages created during probing are cleaned up afterward.
  #
  proc setupCommonConstraints {} {
    #
    # NOTE: The "not_eagle" constraint marks tests whose expected
    #       result reflects a behavior shared by native Tcl and TH8
    #       but NOT reproduced by Eagle's managed (.NET) value model
    #       -- e.g. whole doubles rendering as "X.0", [expr] rejecting
    #       barewords, [incr]/[dict set] auto-creating a missing
    #       variable, and the [exec] "<<" here-string stdin redirect.
    #       These are genuine engine divergences (not TH8-specific
    #       features), so the test still runs under Tcl and TH8 and
    #       only skips under Eagle.
    #
    testConstraint not_eagle [expr {![isEagle]}]

    #
    # NOTE: The "breakOptArg" constraint marks tests of the optional
    #       result-string form of [break] / [continue] -- an Eagle
    #       compatible extension supported by TH8 and Eagle but NOT by
    #       native Tcl (whose [break] / [continue] take no arguments).
    #       Probe it by trapping [break foo]: TH8 and Eagle return the
    #       break code (3), whereas native Tcl reports a "wrong # args"
    #       error (1).
    #
    testConstraint breakOptArg [expr {[catch {break foo}] == 3}]

    testConstraint regsub_nocase [expr {
      [llength [info commands regsub]] > 0 && \
          [catch {regsub -nocase {abc} ABC x _prologue_r}] == 0
    }]

    testConstraint lookbehind [expr {
      [llength [info commands regexp]] > 0 && \
          [catch {regexp {(?<=a)b} ab}] == 0
    }]

    testConstraint constraintEscape [expr {
      [llength [info commands regexp]] > 0 && \
          [catch {regexp {\y} "a b"}] == 0
    }]

    testConstraint lreplaceAppend [expr {
      [catch {lreplace {a b c} 3 3 X}] == 0
    }]

    testConstraint array_set [expr {[catch {
        array set __prologue_array_set [list]
        unset __prologue_array_set
      }] == 0}]

    testConstraint info_subcommands [expr {
      [catch {info subcommands info}] == 0
    }]

    testConstraint clock [expr {
      [catch {clock seconds}] == 0
    }]

    testConstraint escapeU [expr {
      [string length \U00000041] == 1
    }]

    testConstraint test_only_exec [expr {
      [llength [info commands test_only_exec]] > 0 && \
          ([llength [info commands __test_only_exec]] > 0 || \
          [llength [info commands exec]] > 0)
    }]

    testConstraint file_normalize [expr {
      [catch {file normalize .}] == 0
    }]

    testConstraint file_split [expr {
      [catch {file split a/b}] == 0
    }]

    testConstraint format_hash_flag [expr {
      [catch {format %#x 255}] == 0
    }]

    testConstraint format_star_precision [expr {
      [catch {format %.*f 2 3.14}] == 0
    }]

    testConstraint format_star_width [expr {
      [catch {format %*d 5 42}] == 0
    }]

    testConstraint package [expr {
      [catch {package names}] == 0
    }]

    testConstraint package_forget [expr {
      [catch {
        package provide _prologue_pf_test 1.0
        package forget _prologue_pf_test
      }] == 0
    }]

    testConstraint package_ifneeded [expr {
      [catch {package ifneeded _prologue_pi_test 1.0 {}}] == 0
    }]

    catch {package forget _prologue_pi_test}

    testConstraint package_present [expr {
      [catch {
        package provide _prologue_pp_test 1.0
        package present _prologue_pp_test
      }] == 0
    }]

    catch {package forget _prologue_pp_test}

    testConstraint package_require [expr {
      [catch {
        package ifneeded _prologue_pr_test 1.0 \
            {package provide _prologue_pr_test 1.0}

        package require _prologue_pr_test
      }] == 0
    }]

    catch {package forget _prologue_pr_test}

    testConstraint package_scan [expr {
      [catch {package scan}] == 0
    }]

    testConstraint package_unknown [expr {
      [catch {package unknown}] == 0
    }]

    testConstraint package_vcompare [expr {
      [catch {package vcompare 1.0 2.0}] == 0
    }]

    testConstraint package_vsatisfies [expr {
      [catch {package vsatisfies 1.5 1.0-2.0}] == 0
    }]

    testConstraint pid [expr {
      [catch {pid}] == 0
    }]

    testConstraint regexp_all_inline [expr {
      [llength [info commands regexp]] > 0 && \
          [catch {regexp -all -inline {[0-9]+} a1b2}] == 0
    }]

    testConstraint regexp_nocase [expr {
      [llength [info commands regexp]] > 0 && \
          [catch {regexp -nocase {abc} ABC}] == 0
    }]

    testConstraint return_errorinfo [expr {
      [catch {return -code error -errorinfo test msg}] != 0
    }]

    testConstraint string_compare_length [expr {
      [catch {string compare -length 1 ab ac}] == 0
    }]

    testConstraint string_compare_nocase [expr {
      [catch {string compare -nocase A a}] == 0
    }]

    testConstraint string_first_start [expr {
      [catch {string first a ba 1}] == 0
    }]

    testConstraint string_is [expr {
      [catch {string is integer 42}] == 0
    }]

    testConstraint string_last [expr {
      [catch {string last a aba}] == 0
    }]

    testConstraint string_map [expr {
      [catch {string map {a b} a}] == 0
    }]

    testConstraint string_map_nocase [expr {
      [catch {string map -nocase {a b} A}] == 0
    }]

    testConstraint string_repeat [expr {
      [catch {string repeat a 1}] == 0
    }]

    testConstraint string_reverse [expr {
      [catch {string reverse abc}] == 0
    }]

    testConstraint string_tolower_range [expr {
      [catch {string tolower HELLO 1 3}] == 0
    }]

    testConstraint string_toupper_range [expr {
      [catch {string toupper hello 1 3}] == 0
    }]

    testConstraint tip712 [expr {
      [llength [info commands subst]] > 0 && \
          [catch {subst -variables {x}}] == 0
    }]

    testConstraint tailcall [expr {
      [catch {
        proc _prologue_tc_a {} {tailcall _prologue_tc_b}

        proc _prologue_tc_b {} {return ok}
        _prologue_tc_a
      }] == 0
    }]

    catch {rename _prologue_tc_a ""}
    catch {rename _prologue_tc_b ""}

    testConstraint leadingOctal [expr {
      [catch {expr {010}} result] == 0 && $result == 8
    }]

    testConstraint posixCharClass [expr {
      [llength [info commands regexp]] > 0 && \
          [catch {regexp {[[:digit:]]+} 123} result] == 0 &&
      $result == 1
    }]

    testConstraint areLongestMatch [expr {
      [llength [info commands regexp]] > 0 && \
          [catch {regexp {a|ab} ab m}] == 0 && \
          $m eq "ab"
    }]

    testConstraint tip285 [expr {
      [catch {interp cancel} msg] == 1 && $msg eq "eval canceled"
    }]

    testConstraint c99math [expr {
      [catch {expr {cbrt(8.0)}} r] == 0 && $r eq "2.0"
    }]

    testConstraint bigint [expr {
      [catch {expr {9223372036854775807 + 1}} r] == 0 && \
          $r eq "9223372036854775808"
    }]
  }

  #
  # initializeTests --
  #
  #   Initialize the test engine state before any tests run.
  #   Detects the interpreter engine name from
  #   ::tcl_platform(engine) (falling back to "Tcl" when that
  #   variable is absent) and stores it in the package-level
  #   "engine" variable for use in banner output.  Also checks
  #   the TH8_TEST_SUITE_QUIET environment variable; when set,
  #   verbose mode is disabled so that individual PASSED/SKIPPED
  #   lines are suppressed, keeping output limited to failures
  #   and the final summary.
  #
  proc initializeTests {} {
    variable engine
    variable verbose

    set engine [expr {
      [info exists ::tcl_platform(engine)] ? \
          $::tcl_platform(engine) : "Tcl"
    }]

    if {[info exists ::env(TH8_TEST_SUITE_QUIET)]} then {
      set verbose false
    }
  }

  #
  # haveConstraint --
  #
  #   Check whether a named test constraint is currently
  #   satisfied.  Looks up the package-level variable
  #   "th8_constraint_<name>" and returns true only if it exists
  #   and its value is a strict boolean true.  Returns false when
  #   the constraint has never been set or was set to a false
  #   value.  This is the read-only companion to testConstraint;
  #   the test proc calls it to decide whether to skip a test.
  #
  proc haveConstraint { name } {
    variable th8_constraint_$name

    if {[info exists th8_constraint_$name] && \
        [string is true -strict [set th8_constraint_$name]]} then {
      return true
    }

    return false
  }

  #
  # testConstraint --
  #
  #   Set or query a named test constraint.  When called with one
  #   argument (the constraint name), delegates to haveConstraint
  #   and returns its boolean result.  When called with two
  #   arguments (name and value), stores the value in the
  #   package-level variable "th8_constraint_<name>" and returns
  #   it.  Constraint values are typically 0/1 or boolean
  #   expressions.  The setup*Constraints procs use the two-arg
  #   form to register constraints; the test proc uses the
  #   one-arg form to check them before running a test body.
  #
  proc testConstraint { name args } {
    variable th8_constraint_$name

    if {[llength $args] == 0} then {
      return [haveConstraint $name]
    }

    return [set th8_constraint_$name [lindex $args 0]]
  }

  #
  # test --
  #
  #   Define and execute a single test case.  Accepts the test
  #   name, a human-readable description, and keyword arguments:
  #   -setup (code run before the body), -body (the code under
  #   test), -cleanup (code run after the body), -result (the
  #   expected result string), -match (comparison mode: exact,
  #   glob, or regexp; defaults to exact), -constraints (list of
  #   constraint names that must all be satisfied), and
  #   -returnCodes (list of acceptable return codes; defaults to
  #   {0}).  Skips the test if any constraint is unsatisfied.
  #   Snapshots global procedures and variables before/after the
  #   body to detect namespace mutations (leaked or deleted names)
  #   and reports them.  Compares the actual result and return
  #   code against expectations, then increments the appropriate
  #   counter (passed, failed, skipped, or mutated).  Setup and
  #   cleanup scripts, as well as the body, are evaluated in the
  #   caller's scope via [uplevel 1].
  #
  proc test { name description args } {
    variable total
    variable passed
    variable failed
    variable mutated
    variable skipped
    variable failedNames
    variable mutatedNames
    variable skippedNames
    variable file
    variable verbose

    set setup ""
    set body ""
    set cleanup ""
    set expected ""
    set match exact
    set constraints [list]
    set returnCodes [list 0]

    set length [llength $args]; set index 0

    while {$index < $length} {
      set arg [lindex $args $index]; incr index
      if {$arg eq "-setup"} then {
        set setup [lindex $args $index]; incr index
      } elseif {$arg eq "-body"} then {
        set body [lindex $args $index]; incr index
      } elseif {$arg eq "-cleanup"} then {
        set cleanup [lindex $args $index]; incr index
      } elseif {$arg eq "-result"} then {
        set expected [lindex $args $index]; incr index
      } elseif {$arg eq "-match"} then {
        set match [lindex $args $index]; incr index
      } elseif {$arg eq "-constraints"} then {
        set constraints [lindex $args $index]; incr index
      } elseif {$arg eq "-returnCodes"} then {
        set returnCodes [lindex $args $index]; incr index
      }
    }

    incr total

    foreach constraint $constraints {
      if {![testConstraint $constraint]} then {
        incr skipped
        lappend skippedNames [list constraint $constraint $name]

        if {$verbose} then {
          tputs stdout [appendArgs \
              "++++ " $name " SKIPPED (constraint: " $constraint )\n]
        }

        return
      }
    }

    set names(list,loaded,before) [info loaded]
    set names(list,procedures,before) [info procs]
    set names(list,variables,before) [info globals]
    set names(list,commands,before) [info commands]
    set names(list,namespaces,before) [namespace children ::]
    set names(list,channels,before) [file channels]
    set names(list,packages,before) [package names]
    set names(list,functions,before) [info functions]

    if {[isTh8]} then {
      set names(list,expansions,before) [info expansions]
      set names(list,breakpoints,before) [info breakpoints]
    } else {
      set names(list,expansions,before) [list]
      set names(list,breakpoints,before) [list]
    }

    if {[llength [info commands ::th8testlib::array_searches]] > 0} then {
      set names(list,arraySearches,before) [::th8testlib::array_searches]
    } else {
      set names(list,arraySearches,before) [list]
    }

    if {[string length $setup] > 0} then {
      set rc [catch {uplevel 1 $setup} error]
      if {$rc != 0} then {
        incr failed
        lappend failedNames [list setup $name]

        tputs stdout [appendArgs \
            "==== " $name " FAILED (setup error: " $error )\n]

        return
      }
    }

    set rc [catch {uplevel 1 $body} actual]

    if {[string length $cleanup] > 0} then {
      set rc2 [catch {uplevel 1 $cleanup} error]
      if {$rc2 != 0} then {
        incr failed
        lappend failedNames [list cleanup $name]

        tputs stdout [appendArgs \
            "==== " $name " FAILED (cleanup error: " $error )\n]

        return
      }
    }

    set names(list,loaded,after) [info loaded]
    set names(list,procedures,after) [info procs]
    set names(list,variables,after) [info globals]
    set names(list,commands,after) [info commands]
    set names(list,namespaces,after) [namespace children ::]
    set names(list,channels,after) [file channels]
    set names(list,packages,after) [package names]
    set names(list,functions,after) [info functions]

    if {[isTh8]} then {
      set names(list,expansions,after) [info expansions]
      set names(list,breakpoints,after) [info breakpoints]
    } else {
      set names(list,expansions,after) [list]
      set names(list,breakpoints,after) [list]
    }

    if {[llength [info commands ::th8testlib::array_searches]] > 0} then {
      set names(list,arraySearches,after) [::th8testlib::array_searches]
    } else {
      set names(list,arraySearches,after) [list]
    }

    set names(differences,loaded) [ldifferences \
        $names(list,loaded,before) $names(list,loaded,after)]

    set names(differences,procedures) [ldifferences \
        $names(list,procedures,before) $names(list,procedures,after)]

    set names(differences,variables) [ldifferences \
        $names(list,variables,before) $names(list,variables,after)]

    set names(differences,commands) [ldifferences \
        $names(list,commands,before) $names(list,commands,after)]

    set names(differences,namespaces) [ldifferences \
        $names(list,namespaces,before) $names(list,namespaces,after)]

    set names(differences,channels) [ldifferences \
        $names(list,channels,before) $names(list,channels,after)]

    set names(differences,packages) [ldifferences \
        $names(list,packages,before) $names(list,packages,after)]

    set names(differences,functions) [ldifferences \
        $names(list,functions,before) $names(list,functions,after)]

    set names(differences,expansions) [ldifferences \
        $names(list,expansions,before) $names(list,expansions,after)]

    set names(differences,breakpoints) [ldifferences \
        $names(list,breakpoints,before) $names(list,breakpoints,after)]

    set names(differences,arraySearches) [ldifferences \
        $names(list,arraySearches,before) $names(list,arraySearches,after)]

    if {[llength $names(differences,loaded)] > 0} then {
      incr mutated
      lappend mutatedNames [list loaded $name]
      tputs stdout [appendArgs "==== " $name " MUTATED loaded:\n\t"]
      tputs stdout [appendArgs [join $names(differences,loaded) \n\t] \n]
    }

    if {[llength $names(differences,procedures)] > 0} then {
      incr mutated
      lappend mutatedNames [list procedures $name]
      tputs stdout [appendArgs "==== " $name " MUTATED procedures:\n\t"]
      tputs stdout [appendArgs [join $names(differences,procedures) \n\t] \n]
    }

    if {[llength $names(differences,variables)] > 0} then {
      incr mutated
      lappend mutatedNames [list variables $name]
      tputs stdout [appendArgs "==== " $name " MUTATED variables:\n\t"]
      tputs stdout [appendArgs [join $names(differences,variables) \n\t] \n]
    }

    if {[llength $names(differences,commands)] > 0} then {
      incr mutated
      lappend mutatedNames [list commands $name]
      tputs stdout [appendArgs "==== " $name " MUTATED commands:\n\t"]
      tputs stdout [appendArgs [join $names(differences,commands) \n\t] \n]
    }

    if {[llength $names(differences,namespaces)] > 0} then {
      incr mutated
      lappend mutatedNames [list namespaces $name]
      tputs stdout [appendArgs "==== " $name " MUTATED namespaces:\n\t"]
      tputs stdout [appendArgs [join $names(differences,namespaces) \n\t] \n]
    }

    if {[llength $names(differences,channels)] > 0} then {
      incr mutated
      lappend mutatedNames [list channels $name]
      tputs stdout [appendArgs "==== " $name " MUTATED channels:\n\t"]
      tputs stdout [appendArgs [join $names(differences,channels) \n\t] \n]
    }

    if {[llength $names(differences,packages)] > 0} then {
      incr mutated
      lappend mutatedNames [list packages $name]
      tputs stdout [appendArgs "==== " $name " MUTATED packages:\n\t"]
      tputs stdout [appendArgs [join $names(differences,packages) \n\t] \n]
    }

    if {[llength $names(differences,functions)] > 0} then {
      incr mutated
      lappend mutatedNames [list functions $name]
      tputs stdout [appendArgs "==== " $name " MUTATED functions:\n\t"]
      tputs stdout [appendArgs [join $names(differences,functions) \n\t] \n]
    }

    if {[llength $names(differences,expansions)] > 0} then {
      incr mutated
      lappend mutatedNames [list expansions $name]
      tputs stdout [appendArgs "==== " $name " MUTATED expansions:\n\t"]
      tputs stdout [appendArgs [join $names(differences,expansions) \n\t] \n]
    }

    if {[llength $names(differences,breakpoints)] > 0} then {
      incr mutated
      lappend mutatedNames [list breakpoints $name]
      tputs stdout [appendArgs "==== " $name " MUTATED breakpoints:\n\t"]
      tputs stdout [appendArgs [join $names(differences,breakpoints) \n\t] \n]
    }

    if {[llength $names(differences,arraySearches)] > 0} then {
      incr mutated
      lappend mutatedNames [list arraySearches $name]
      tputs stdout [appendArgs "==== " $name " MUTATED arraySearches:\n\t"]
      tputs stdout [appendArgs [join $names(differences,arraySearches) \n\t] \n]
    }

    set codeOk 0

    foreach returnCode $returnCodes {
      if {$rc == $returnCode} then {set codeOk 1; break}
    }

    if {!$codeOk} then {
      incr failed
      lappend failedNames [list body $name]

      tputs stdout [appendArgs "==== " $name " FAILED\n"]

      tputs stdout [appendArgs \
          "---- Return code " $rc ", expected one of: " $returnCodes \n]

      tputs stdout [appendArgs "---- Result was: " $actual \n]
      return
    }

    set matchOk 0

    if {$match eq "exact"} then {
      if {$actual eq $expected} then { set matchOk 1 }
    } elseif {$match eq "glob"} then {
      set matchOk [string match $expected $actual]
    } elseif {$match eq "regexp"} then {
      set matchOk [regexp $expected $actual]
    }

    if {$matchOk} then {
      incr passed
      if {$verbose} then {
        tputs stdout [appendArgs "++++ " $name " PASSED\n"]
      }
    } else {
      incr failed
      lappend failedNames [list result $name]

      tputs stdout [appendArgs "==== " $name " FAILED\n"]
      tputs stdout [appendArgs "---- Expected: " $expected \n]
      tputs stdout [appendArgs "---- Actual: " $actual \n]
    }
  }

  #
  # getEnvironmentVariable --
  #
  #   Return the value of environment variable $name, or "" when it is
  #   unset.  Abstracts the engine difference: TH8 does not auto-link the
  #   ::env array the way Tcl and Eagle do, so under TH8 the value is read
  #   through the testlib's dedicated env command (::th8testlib::env_kv,
  #   available once detectLoadLib has run); under Tcl/Eagle the ::env
  #   array is used.  When neither is available (e.g. a bare TH8 shell
  #   without the testlib loaded) the empty string is returned.
  #
  proc getEnvironmentVariable { name } {
    if {[isTh8]} then {
      if {[llength [info commands ::th8testlib::env_kv]] > 0 && \
          [::th8testlib::env_kv exists $name]} then {
        set value [::th8testlib::env_kv get $name]
      } else {
        set value ""
      }
    } else {
      if {[info exists ::env($name)]} then {
        set value $::env($name)
      } else {
        set value ""
      }
    }

    return $value
  }

  #
  # selectionPatterns --
  #
  #   Return the whitespace-separated glob-pattern list held by the named
  #   environment variable, or the empty list when it is unset or blank.
  #   The four variables TH8_TEST_MATCH / TH8_TEST_SKIP / TH8_TEST_FILE /
  #   TH8_TEST_NOTFILE are the env-driven equivalents of tcltest's
  #   `configure -match / -skip / -file / -notFile` selection options:
  #   -match/-skip filter by TEST NAME (isTestSelected, via runTest) and
  #   -file/-notFile filter by TEST FILE NAME (isFileSelected, via
  #   runAllTests).
  #
  proc selectionPatterns { name } {
    set value [string trim [getEnvironmentVariable $name]]

    if {[string length $value] > 0} then {
      return $value
    }

    return [list]
  }

  #
  # isSelected --
  #
  #   Apply an include/exclude glob-pattern pair (read from the two named
  #   env vars) to a candidate string -- a test name or a test file name.
  #   Returns 1 (selected) when the candidate matches at least one include
  #   pattern, OR the include list is empty (meaning "all"), AND matches
  #   no exclude pattern; 0 otherwise.  The shared core of isTestSelected
  #   and isFileSelected.
  #
  proc isSelected { candidate includeVar excludeVar } {
    set include [selectionPatterns $includeVar]

    if {[llength $include] > 0} then {
      set matched 0

      foreach pattern $include {
        if {[string match $pattern $candidate]} then {
          set matched 1; break
        }
      }

      if {!$matched} then { return 0 }
    }

    foreach pattern [selectionPatterns $excludeVar] {
      if {[string match $pattern $candidate]} then { return 0 }
    }

    return 1
  }

  #
  # isTestSelected --
  #
  #   Return 1 if a test named $name should run under the env-driven
  #   tcltest-style -match / -skip filters (TH8_TEST_MATCH /
  #   TH8_TEST_SKIP), 0 if it is filtered out.  Called by runTest.
  #
  proc isTestSelected { name } {
    return [isSelected $name TH8_TEST_MATCH TH8_TEST_SKIP]
  }

  #
  # isFileSelected --
  #
  #   Return 1 if a test file named $fileName should run under the
  #   env-driven tcltest-style -file / -notFile filters (TH8_TEST_FILE /
  #   TH8_TEST_NOTFILE), 0 if it is filtered out.  Called by runAllTests.
  #
  proc isFileSelected { fileName } {
    return [isSelected $fileName TH8_TEST_FILE TH8_TEST_NOTFILE]
  }

  #
  # runTest --
  #
  #   Wrapper that evaluates a test definition script in the
  #   caller's scope.  Sets the package-level "file" variable to
  #   [info script] when it has not already been set, so that
  #   test output and the cleanupTests summary can report which
  #   file the test came from.  Every conformance test file calls
  #   [runTest { ... }] around its test definitions to ensure
  #   the file context is established correctly.
  #
  proc runTest { script } {
    variable file

    if {![info exists file] || [string length $file] == 0} then {
      set file [info script]
    }

    rename test __savedTest
    proc test { name args } { return $name }

    set name [namespace eval [namespace current] $script]

    rename test ""
    rename __savedTest test

    if {![isTestSelected $name]} then {
      return ""
    }

    return [uplevel 1 $script]
  }

  #
  # runAllTests --
  #
  #   Source and execute a list of test files, accumulating
  #   results across all of them.  Prints a banner identifying
  #   the engine and its version, then iterates over fileNames,
  #   sourcing each from the "tests/" directory.  Per-file
  #   counters (total, passed, failed, mutated, skipped) are
  #   reset before each file and accumulated into grand totals.
  #   Errors during sourcing are caught and reported as failures
  #   without aborting the remaining files.  After all files have
  #   been run, prints a summary table and an overall
  #   SUCCESS/FAILURE verdict.  This is the entry point used by
  #   all.tcl to run the entire conformance suite.
  #
  proc runAllTests { fileNames } {
    variable total
    variable passed
    variable failed
    variable mutated
    variable skipped
    variable engine
    variable failedNames
    variable mutatedNames
    variable skippedNames
    variable file

    tputs stdout \n
    tputs stdout "============================================\n"
    tputs stdout "Tcl Language Standard Conformance Test Suite\n"

    tputs stdout [appendArgs \
        "Tcl Language Implementation: " $engine " v" [info patchlevel] \n]

    tputs stdout "============================================\n"
    tputs stdout \n

    set totalFiles 0
    set totalTests 0
    set totalPassed 0
    set totalFailed 0
    set totalMutated 0
    set totalSkipped 0

    foreach fileName $fileNames {
      if {![isFileSelected $fileName]} then {
        continue
      }

      set path [file join tests $fileName]

      set total 0
      set passed 0
      set failed 0
      set mutated 0
      set skipped 0

      tputs stdout [appendArgs "---- Running: " $fileName " ----\n"]
      tputs stdout \n; set file $fileName

      if {[catch {
          uplevel 1 [list source $path]
        } error] == 0} then {
        incr totalFiles
      } else {
        incr totalFiles; incr failed
        lappend failedNames [list file $fileName]
        tputs stdout [appendArgs "==== FILE " $fileName " ERROR: " $error \n]
      }

      incr totalTests $total
      incr totalPassed $passed
      incr totalFailed $failed
      incr totalMutated $mutated
      incr totalSkipped $skipped
    }

    if {[llength $failedNames] > 0} then {
      tputs stdout [appendArgs \
          "==== FAILED:\n\t" [join $failedNames \n\t] \n\n]
    }

    if {[llength $mutatedNames] > 0} then {
      tputs stdout [appendArgs \
          "==== MUTATED:\n\t" [join $mutatedNames \n\t] \n\n]
    }

    if {[llength $skippedNames] > 0} then {
      tputs stdout [appendArgs \
          "==== SKIPPED:\n\t" [join $skippedNames \n\t] \n\n]
    }

    tputs stdout "============================================\n"
    tputs stdout "Tcl Language Standard Conformance Test Suite\n"

    tputs stdout [appendArgs \
        "Tcl Language Implementation: " $engine " v" [info patchlevel] \n]

    tputs stdout \n
    tputs stdout [appendArgs "\tFiles:\t\t" $totalFiles \n]
    tputs stdout [appendArgs "\tTotal:\t\t" $totalTests \n]
    tputs stdout [appendArgs "\tPassed:\t\t" $totalPassed \n]
    tputs stdout [appendArgs "\tFailed:\t\t" $totalFailed \n]
    tputs stdout [appendArgs "\tMutated:\t" $totalMutated \n]
    tputs stdout [appendArgs "\tSkipped:\t" $totalSkipped \n]
    tputs stdout \n

    if {$totalFailed > 0} then {
      tputs stdout "OVERALL STATUS: FAILURE\n"
    } else {
      tputs stdout "OVERALL STATUS: SUCCESS\n"
    }

    tputs stdout "============================================\n"
    tputs stdout \n
  }

  #
  # cleanupTests --
  #
  #   Print the per-file test summary and reset engine state.
  #   Outputs a one-line tally of total, passed, failed, mutated,
  #   and skipped counts, followed by a per-file SUCCESS/FAILURE
  #   verdict.  Clears the "file" and "engine" variables so the
  #   next test file starts with a clean slate.  Returns the
  #   failure count, which callers can use to set an exit code.
  #   Each test file calls this at the end of its runTest block
  #   (typically in epilogue.tcl).
  #
  proc cleanupTests {} {
    variable total
    variable passed
    variable failed
    variable mutated
    variable skipped
    variable file
    variable verbose

    tputs stdout \n

    tputs stdout [appendArgs \
        "Total: " $total ", Passed: " $passed ", Failed: " $failed \
        ", Mutated: " $mutated ", Skipped: " $skipped \n]

    if {$failed > 0} then {
      tputs stdout [appendArgs "FILE " $file " STATUS: FAILURE\n"]
    } else {
      tputs stdout [appendArgs "FILE " $file " STATUS: SUCCESS\n"]
    }

    if {$mutated > 0} then {
      tputs stdout [appendArgs "FILE " $file " STATUS: DIRTY\n"]
    } else {
      tputs stdout [appendArgs "FILE " $file " STATUS: CLEAN\n"]
    }

    tputs stdout \n; set result [expr {$failed + $mutated}]

    unset -nocomplain file
    unset -nocomplain engine

    return $result
  }

  #
  # sandboxRc / sandboxResult / sandboxSteps / sandboxAllocCount --
  #
  #   Accessor procs that extract individual fields from the
  #   list returned by [::th8testlib::sandbox].  A sandbox result
  #   is a four-element list: {rc result steps alloc}, where "rc"
  #   is the return code, "result" is the evaluation result
  #   string, "steps" is the number of bytecode steps executed,
  #   and "alloc" is the number of bytes allocated during
  #   evaluation.  Using named accessors instead of bare [lindex]
  #   makes sandbox tests self-documenting and insulates them
  #   from future changes to the result layout.
  #
  proc sandboxRc { result } {
    return [lindex $result 0]
  }

  proc sandboxResult { result } {
    return [lindex $result 1]
  }

  proc sandboxSteps { result } {
    return [lindex $result 2]
  }

  proc sandboxAllocCount { result } {
    return [lindex $result 3]
  }

  #
  # faultRc / faultResult / faultAllocCount / faultTriggered --
  #
  #   Accessor procs that extract individual fields from the
  #   list returned by [::th8testlib::fault].  A fault-injection
  #   result is a four-element list: {rc result allocCount
  #   triggered}, where "rc" is the return code, "result" is the
  #   evaluation result string, "allocCount" is the total number
  #   of allocations observed, and "triggered" is a boolean
  #   indicating whether the injected fault actually fired during
  #   evaluation.  Like the sandbox accessors, these provide
  #   named field access to keep fault-injection tests readable
  #   and resilient to layout changes.
  #
  proc faultRc { result } {
    return [lindex $result 0]
  }

  proc faultResult { result } {
    return [lindex $result 1]
  }

  proc faultAllocCount { result } {
    return [lindex $result 2]
  }

  proc faultTriggered { result } {
    return [lindex $result 3]
  }

  namespace export ldifferences normalizeFloat isAdministrator hasSubCommand

  namespace export initializeTests haveConstraint testConstraint test \
      runTest runAllTests cleanupTests

  namespace export setupSystemConstraints setupLoadConstraints \
      setupCryptoConstraints setupFaultConstraints setupSqliteConstraints \
      setupFileConstraints setupCommandConstraints setupSubCommandConstraints \
      setupNamespaceConstraints setupCurlConstraints setupCommonConstraints

  namespace export sandboxRc sandboxResult sandboxSteps sandboxAllocCount \
      faultRc faultResult faultAllocCount faultTriggered

  namespace eval :: {namespace import -force ::th8test::*}

  set path [file normalize [file dirname [info script]]]
  set binPath [file dirname [info nameofexecutable]]

  setupSystemConstraints

  package provide th8test 1.0
}
