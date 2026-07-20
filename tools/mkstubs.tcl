#!/usr/bin/env tclsh
###############################################################################
#
# mkstubs.tcl --
#
#     Generate TH8 stubs table files from th8.h.
#
#     Parses the public API declarations from th8.h and generates:
#       1. th8Decls.h      -- Stubs table struct and redirection macros.
#       2. th8StubInit.c   -- Static stubs table initializer.
#
# Usage:
#     tclsh tools/mkstubs.tcl src/th8.h src/th8Decls.h src/th8StubInit.c
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

proc emit {fd args} {
  foreach line $args {
    puts $fd $line
  }
}

#
# parseHeader --
#     Extract TH8_API function declarations from one public header
#     and return a list of {funcName retType params} triples in
#     declaration order.
#
#     A public API function declaration begins with the TH8_API
#     export marker at column 0.  The return type, function name,
#     and parameter list MAY span multiple lines: clang-format
#     reflows a declaration whose single-line form exceeds the
#     column limit into
#
#         TH8_API int
#         Th8_Foo(...);
#
#     so the earlier same-line-only regex silently dropped every
#     such function from the stubs table (leaving it linkable on
#     platforms that link the full library, but an unresolved
#     external on stubs-only builds such as the MSVC testlib DLL).
#     Key off the TH8_API marker and accumulate the whole
#     declaration up to its closing ");" so both the single-line
#     and multi-line forms are captured identically.
#
proc parseHeader {hdr} {
  set fd [open $hdr r]
  set lines [split [read $fd] \n]
  close $fd

  set funcs {}
  set nLines [llength $lines]
  for {set i 0} {$i < $nLines} {incr i} {
    set line [lindex $lines $i]

    if {[string match "TH8_API *" $line]} then {
      set fullDecl $line
      while {![string match "*);*" $fullDecl] && $i < $nLines - 1} {
        incr i
        append fullDecl " " [string trim [lindex $lines $i]]
      }

      #
      # Collapse interior whitespace (including the joined line
      # breaks) to single spaces so the extraction regex is
      # line-break agnostic.
      #
      regsub -all {\s+} $fullDecl { } fullDecl
      set fullDecl [string trim $fullDecl]

      #
      # Extract "TH8_API <retType> <FuncName>(<params>);".  The
      # function name is the last th8-prefixed identifier directly
      # before the '('; the required "\(" skips data declarations
      # (TH8_API extern <type> <name>;), which have no parameter
      # list.
      #
      if {[regexp \
              {^TH8_API\s+(.+?)\s*([Tt]h8_?\w+)\s*\((.*)\)\s*;} \
              $fullDecl -> retType funcName params]} then {
        if {$funcName eq "Th8_Interp"} then { continue }
        set retType [string trim $retType]
        set params [string trim $params]
        lappend funcs [list $funcName $retType $params]
      }
    }
  }
  return $funcs
}

proc main {argv} {
  if {[llength $argv] != 3} then {
    puts stderr "Usage: tclsh mkstubs.tcl <th8.h> <th8Decls.h> <th8StubInit.c>"
    exit 1
  }
  set hdr  [lindex $argv 0]
  set decl [lindex $argv 1]
  set init [lindex $argv 2]

  #
  # Aggregate TH8_API declarations from every public header.  The
  # primary header (th8.h) is parsed FIRST so its stub-table slot
  # offsets are unchanged when the sibling public headers append
  # their functions -- this keeps the stubs ABI
  # (TH8_STUBS_VERSION) stable.  TH8_API functions are not confined
  # to th8.h: th8_hash.h and th8_plugin.h also export public API
  # (e.g. Th8_RegisterPlugin), and omitting them left the MSVC
  # stubs-only testlib DLL with unresolved externals.  New public
  # headers must be added to the sibling list below.
  #
  set dir [file dirname $hdr]
  set funcs [parseHeader $hdr]
  foreach sib {th8_hash.h th8_plugin.h} {
    set sibPath [file join $dir $sib]
    if {![file exists $sibPath]} then { continue }
    foreach f [parseHeader $sibPath] {
      set nm [lindex $f 0]
      set dup 0
      foreach g $funcs {
        if {[lindex $g 0] eq $nm} then { set dup 1; break }
      }
      if {!$dup} then { lappend funcs $f }
    }
  }

  #
  # Generate th8Decls.h.
  #

  #
  # Conditionally compiled functions and their preprocessor
  # guards.  The stubs table struct always has slots for all
  # functions; the initializer wraps conditional entries with
  # #if / #else 0 so the slot is NULL on platforms where the
  # function does not exist.
  #

  array set guardMap {
    Th8_GetPosixPlatform           {!defined(_WIN32) && !defined(WIN32)}
    Th8_GetWin32Platform           {defined(_WIN32) || defined(WIN32)}
    Th8_GetMacOSPlatform           {defined(__APPLE__)}
    Th8_GetIosPlatform             {defined(TH8_PLATFORM_IOS)}
    Th8_GetAndroidPlatform         {defined(TH8_PLATFORM_ANDROID)}
    Th8_GetCurlPlatform            {defined(TH8_ENABLE_LIBCURL)}
    Th8_RegisterRegex              {defined(TH8_ENABLE_REGEXP)}
    Th8_RsaKeyLoad                 {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyFree                 {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyBitLen               {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyHasPrivate           {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyPubExp               {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyModulus              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyPrivExp              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyPrime1               {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyPrime2               {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyToken                {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaKeyTokenHex             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_HarpySigLoad               {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaVerify                  {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaSign                    {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_PolicyFindKey              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ProtectedAlloc             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ProtectedFree              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ProtectedCheckCanary       {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ProtectedData              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ProtectedPageSize          {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ProtectedCanarySize        {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_InstallSignedPolicy        {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RemoveSignedPolicy         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_PolicyPreloadKey           {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_PolicyGetKeyTokens         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetEmbeddedKeyRoot         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetEmbeddedKey0            {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetEmbeddedKeyTest         {defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)}
    Th8_GetPublicKeyTest           {defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)}
    Th8_GetPublicKeyTestToken      {defined(TH8_ENABLE_CRYPTOGRAPHY) && defined(TH8_ENABLE_TEST_KEY)}
    Th8_GetPublicKeyZero           {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetPublicKeyZeroToken      {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetPublicKeyRoot           {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetPublicKeyRootToken      {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_Sha512Hex                  {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RsaExtractHash             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_EvalFileAndRsaKeyLoad      {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_EnableSignedPolicy         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_AttrFlagsChange            {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_AttrFlagsFormat            {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_AttrFlagsHave              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_AttrFlagsParse             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SecureInit                 {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SecureFinish               {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SecureVarCreate            {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SecureVarDelete            {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_IsSecureVar                {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetSecureKeyStore          {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SetSecureKeyStore          {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetSecureVarHash           {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SetSecureVarHash           {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_MarkResultSensitive        {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SetResultSensitive         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SecureSetMasterKey         {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SecureClearMasterKey       {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_EnableSecurePersist        {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_IsSecurePersistEnabled     {defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_NtpQuery                   {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_HttpsTimeQuery             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetLastNtpSec              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SetLastNtpSec              {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetLastLocalMs             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SetLastLocalMs             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetEmbeddedKeyTime         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_SaveSignedOnly             {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_RestoreSignedOnly          {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_ResetSecurityArray         {defined(TH8_ENABLE_CRYPTOGRAPHY)}
    Th8_GetInput                   {defined(TH8_PLUGIN_IO)}
    Th8_RedirectInput              {defined(TH8_PLUGIN_IO)}
    Th8_GetOutput                  {defined(TH8_PLUGIN_IO)}
    Th8_RedirectOutput             {defined(TH8_PLUGIN_IO)}
    Th8_GetErrorOutput             {defined(TH8_PLUGIN_IO)}
    Th8_RedirectErrorOutput        {defined(TH8_PLUGIN_IO)}
    Th8_GetMimallocPlatform        {defined(TH8_USE_MIMALLOC)}
    Th8_GetCosmopolitanPlatform    {defined(TH8_PLATFORM_COSMOPOLITAN)}
    Th8_FaultConfigInit            {defined(TH8_ENABLE_FAULT_INJECTION)}
    Th8_FaultInstall               {defined(TH8_ENABLE_FAULT_INJECTION)}
    Th8_FaultUninstall             {defined(TH8_ENABLE_FAULT_INJECTION)}
    Th8_FaultCtxSize               {defined(TH8_ENABLE_FAULT_INJECTION)}
    Th8_GetCacheStats              {defined(TH8_BENCHMARKING)}
    Th8_ResetCacheStats            {defined(TH8_BENCHMARKING)}
    Th8_EnableBigint               {defined(TH8_ENABLE_BIGINT)}
    Th8_IsBigintEnabled            {defined(TH8_ENABLE_BIGINT)}
    Th8_EnableLoad                 {defined(TH8_ENABLE_LOAD)}
    Th8_EnableUnload               {defined(TH8_ENABLE_LOAD)}
    Th8_IsLoadEnabled              {defined(TH8_ENABLE_LOAD)}
    Th8_IsUnloadDangerous          {defined(TH8_ENABLE_LOAD)}
    Th8_IsUnloadEnabled            {defined(TH8_ENABLE_LOAD)}
    Th8_ListAppendLoaded           {defined(TH8_ENABLE_LOAD)}
    Th8_Load                       {defined(TH8_ENABLE_LOAD)}
    Th8_SetPreLoadCallback         {defined(TH8_ENABLE_LOAD)}
    Th8_Unload                     {defined(TH8_ENABLE_LOAD)}
    Th8_DeclareSystemVar           {defined(TH8_ENABLE_VARIABLES)}
    Th8_ExistsArrayVar             {defined(TH8_ENABLE_VARIABLES)}
    Th8_ExistsVar                  {defined(TH8_ENABLE_VARIABLES)}
    Th8_GetVar                     {defined(TH8_ENABLE_VARIABLES)}
    Th8_IsSystemVar                {defined(TH8_ENABLE_VARIABLES)}
    Th8_LinkVar                    {defined(TH8_ENABLE_VARIABLES)}
    Th8_ListAppendArray            {defined(TH8_ENABLE_VARIABLES)}
    Th8_ListAppendGlobalVariables  {defined(TH8_ENABLE_VARIABLES)}
    Th8_ListAppendVarLinks         {defined(TH8_ENABLE_VARIABLES)}
    Th8_ListAppendNsVariables      {defined(TH8_ENABLE_VARIABLES)}
    Th8_ListAppendVariables        {defined(TH8_ENABLE_VARIABLES)}
    Th8_ParseVarName               {defined(TH8_ENABLE_VARIABLES)}
    Th8_RestoreSystemVar           {defined(TH8_ENABLE_VARIABLES)}
    Th8_SaveSystemVar              {defined(TH8_ENABLE_VARIABLES)}
    Th8_GetVarValue                {defined(TH8_ENABLE_VARIABLES)}
    Th8_SetVar                     {defined(TH8_ENABLE_VARIABLES)}
    Th8_SetVarLength               {defined(TH8_ENABLE_VARIABLES)}
    Th8_SetVarValue                {defined(TH8_ENABLE_VARIABLES)}
    Th8_UnsetVar                   {defined(TH8_ENABLE_VARIABLES)}
    Th8_CreateMathFunc             {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_DeleteMathFunc             {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_Expr                       {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_FindMathFunc               {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_ListAppendMathFunctions    {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_MathOp                     {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_ParseExpr                  {defined(TH8_ENABLE_EXPRESSIONS)}
    Th8_ResetSecurityArray         {defined(TH8_ENABLE_VARIABLES)}
  }

  #
  # NOTE: Th8_GetNullIoPlatform, Th8_GetLibcPlatform, and
  # Th8_GetCompileOptions are unconditional (always available).
  #

  #
  # Bootstrap functions excluded from the stubs table entirely.
  # These are part of the stubs library (th8StubLib.c) and are
  # called BEFORE the stubs pointer exists, so they cannot
  # appear in the table or the initializer.
  #

  set excludeNames {
    Th8_InitStubs Th8_GetStubs
  }

  set filtered {}
  foreach f $funcs {
    set name [lindex $f 0]
    if {$name in $excludeNames} then { continue }
    lappend filtered $f
  }
  set funcs $filtered

  set fd [open $decl w]
  fconfigure $fd -encoding binary -translation binary
  emit $fd \
      "/*" \
      " * th8Decls.h --" \
      " *" \
      " *\tTH8 stubs table declarations.  Auto-generated by" \
      " *\ttools/mkstubs.tcl.  DO NOT EDIT BY HAND." \
      " *" \
      " * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved." \
      " *" \
      " * See the file \"license.terms\" for information on usage and redistribution of" \
      " * this file, and for a DISCLAIMER OF ALL WARRANTIES." \
      " */" \
      "" \
      "#ifndef TH8_DECLS_H" \
      "#define TH8_DECLS_H" \
      "" \
      "/*" \
      " * The stubs table aggregates the public API from every public" \
      " * header, so th8Decls.h must pull in each of them for the" \
      " * types they define (e.g. Th8_GetCommandsProc from th8_plugin.h," \
      " * Th8_Hash / Th8_HashEntry from th8_hash.h).  Keep this list in" \
      " * sync with the sibling-header list in mkstubs.tcl." \
      " */" \
      "#include \"th8.h\"" \
      "#include \"th8_hash.h\"" \
      "#include \"th8_plugin.h\"" \
      "" \
      "/*" \
      " * The stubs table: a struct of function pointers for every" \
      " * public TH8 API function." \
      " */" \
      "" \
      "typedef struct Th8StubsTable \{" \
      "    int magic;     /* Must be TH8_STUBS_MAGIC. */" \
      "    int version;   /* Stubs table version. */"

  set idx 0
  foreach f $funcs {
    set origName [lindex $f 0]
    set name [string tolower \
        [string index $origName 0]][string range $origName 1 end]

    set ret  [lindex $f 1]
    set parm [lindex $f 2]

    #
    # For guarded functions, the struct must have a slot
    # regardless of whether the guard is active.  When the
    # guard is false, stub in a "void *" to maintain the
    # table layout (size and slot indices).
    #
    if {[info exists guardMap($origName)]} then {
      puts $fd "#if $guardMap($origName)"
      puts $fd "    $ret (*$name)($parm);"
      puts $fd "#else"
      puts $fd "    void *$name;"
      puts $fd "#endif"
    } else {
      puts $fd "    $ret (*$name)($parm);"
    }
    incr idx
  }

  emit $fd \
      "\} Th8StubsTable;" \
      "" \
      "#define TH8_STUBS_MAGIC   (0x54483853)  /* \"TH8S\" */" \
      "#define TH8_STUBS_VERSION (1)" \
      "" \
      "/*" \
      " * Th8_InitStubs --" \
      " *\tRetrieve the stubs table from the interpreter." \
      " *\tReturns NULL on failure (version mismatch or stubs" \
      " *\tnot available).  Extensions must call this before" \
      " *\tusing any other TH8 API." \
      " */" \
      "const Th8StubsTable *Th8_InitStubs(Th8_Interp *interp," \
      "    const char *version, int exact);" \
      "" \
      "/*" \
      " * When USE_TH8_STUBS is defined, all Th8_* calls are" \
      " * redirected through the stubs table." \
      " */" \
      "" \
      "#ifdef USE_TH8_STUBS" \
      "" \
      "extern const Th8StubsTable *th8StubsPtr;" \
      ""

  #
  # skipNames: functions that ARE in the stubs table but should
  # NOT get a #define redirect macro.  Currently empty since
  # the only candidates (InitStubs, GetStubs) are excluded
  # from the table entirely via excludeNames above.
  #

  set skipNames {}

  foreach f $funcs {
    set name [lindex $f 0]
    if {$name in $skipNames} then { continue }

    set lowerName [string tolower \
        [string index $name 0]][string range $name 1 end]

    if {[info exists guardMap($name)]} then {
      puts $fd "#if $guardMap($name)"
      puts $fd "#define $name  (th8StubsPtr->$lowerName)"
      puts $fd "#endif"
    } else {
      puts $fd "#define $name  (th8StubsPtr->$lowerName)"
    }
  }

  emit $fd \
      "" \
      "#endif /* USE_TH8_STUBS */" \
      "" \
      "#endif /* TH8_DECLS_H */"
  close $fd

  puts "Generated $decl ($idx functions)"

  #
  # Generate th8StubInit.c.
  #

  set fd [open $init w]
  fconfigure $fd -encoding binary -translation binary
  emit $fd \
      "/*" \
      " * th8StubInit.c --" \
      " *" \
      " *\tTH8 stubs table static initializer.  Auto-generated by" \
      " *\ttools/mkstubs.tcl.  DO NOT EDIT BY HAND." \
      " *" \
      " *\tThis file is compiled into the TH8 core library (not the" \
      " *\tstubs library).  It provides the static Th8StubsTable" \
      " *\tpopulated with pointers to all public API functions." \
      " *" \
      " * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved." \
      " *" \
      " * See the file \"license.terms\" for information on usage and redistribution of" \
      " * this file, and for a DISCLAIMER OF ALL WARRANTIES." \
      " */" \
      "" \
      "#include \"th8.h\"" \
      "#include \"th8Decls.h\"" \
      "" \
      "const Th8StubsTable th8StubsTableData = \{" \
      "    TH8_STUBS_MAGIC," \
      "    TH8_STUBS_VERSION,"

  foreach f $funcs {
    set name [lindex $f 0]
    if {[info exists guardMap($name)]} then {
      puts $fd "#if $guardMap($name)"
      puts $fd "    $name,"
      puts $fd "#else"
      puts $fd "    0,"
      puts $fd "#endif"
    } else {
      puts $fd "    $name,"
    }
  }

  emit $fd "\};"
  close $fd

  puts "Generated $init ($idx functions)"
}

main $argv
