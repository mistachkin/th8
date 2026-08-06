# Tcl / Eagle / TH8 Scripting Style Guide

*Audit-reviewed 2026-06-22; style rules below remain canonical
for the TH8 release.  Per-rule changes are dated inline where
they have occurred (e.g. the 2026-04-28 unification of test-
file indentation in §20.2).*

**Audience:** human authors and AI / LLM agents writing or editing Tcl
scripts in the Eagle, System.Data.SQLite, and TH8 projects (and any
project that follows the same conventions).

**Authority:** the rules below are derived **empirically** from these
canonical bodies of work, in priority order:

1. `mistachkin/eagle/Eagle/lib/Eagle1.0/*.eagle` — Eagle core library
   (28 files; the foundational style reference).
2. `mistachkin/eagle/Eagle/Library/Tests/*.eagle` — Eagle language
   conformance tests (157 files; canonical test-framework usage).
3. `mistachkin/scratch/eagle/startup/*.eagle` — production startup
   scripts (newer reference; see especially `startup.eagle`).
4. `sqlite/dotnet/lib/System.Data.SQLite/*.eagle` — data provider
   library (4 files).
5. `sqlite/dotnet/Tests/*.eagle` — data provider tests (112 files).

If a rule below conflicts with what you observe in those files,
**the file wins** — file the conflict as a guide bug.

> ### TL;DR for AI/LLM agents
>
> 1. **Indent: 2 spaces. No tabs. Ever.**
> 2. **Line endings:** CRLF in Eagle source; LF in TH8 source. Match
>    the file you are editing.
> 3. **`if {cond} then { ... }`** — the `then` keyword is **required**.
> 4. **`elseif`** is one word. Never `else if`.
> 5. **`expr` is always braced**: `expr {$x + 1}`. Never bare.
> 6. **Conditions are always braced**: `if {[catch ...] == 0}`.
> 7. **Opening brace on the same line** as the keyword (`proc`, `if`,
>    `foreach`, `while`, `for`, `switch`, `catch`, `namespace eval`).
> 8. **Closing brace flush left** at the column of the line that
>    opened the block.
> 9. **Procs are camelCase** (`getColumnValue`, `isEagle`); library
>    procs live inside a `namespace eval ::Ns { ... }`.
> 10. **File header:** divider padded to column 79 (file-header lines
>     start at column 0 → 79 `#` chars), file name + dash-dash,
>     project line, copyright, license-pointer line, RCS Id line,
>     divider. **All `#` dividers are pad-to-column-79**, not
>     fixed-79 — at deeper indent, the `#` run is shorter (see #14).
> 11. **Block comments use `# NOTE:`** on the line introducing the
>     comment; continuation lines align under the text after the
>     marker (7 spaces in: `#       continuation text`).
> 12. **Test names are `prefix-N.M`** (e.g. `array-1.3`). Test
>     bodies live inside `runTest { test ... -body { ... } -result
>     { ... } }`.
> 13. **`unset -nocomplain`** — always use `-nocomplain` in test
>     `-cleanup` blocks.
> 14. **Section dividers:** a `#` run **padded to column 79** —
>     line total is always 79 chars, so deeper indents have fewer
>     `#` chars (col 0 → 79 `#`, col 2 → 77 `#`, col 4 → 75 `#`,
>     col 6 → 73 `#`, etc.). Dividers are valid at top level
>     between sections, AND inside proc bodies between phases of
>     a complex procedure (see §1.3).
> 15. **Backslash line continuation** (`\` at end of line) for long
>     statements; continuation lines indented +4 spaces from the
>     statement's own indent column.
> 16. **Tag-comment annotations** are the highest-value
>     best-practice signal. Public procs get `# <public>` above
>     the `proc` line; bootstrap procs get `# <bootstrap>`; help
>     text uses `# <help>` ... `# </help>` as the first lines of
>     the body; inline behavioural annotations use `# <<inline>>`,
>     `# <<nonCaching>>`. See §5.8 for the full taxonomy.
> 17. **Form-feed (`\x0C`, `^L`) page break** alone on a line
>     between every adjacent pair of procs inside a `namespace
>     eval` block. See §1.2.
> 18. **Bare words over quotes**: write `set x hello`, not
>     `set x "hello"`. Double-quotes and braces are only for
>     strings that genuinely need them — substitutions inside a
>     longer string, embedded whitespace or special characters,
>     literal `{...}` lists or code blocks, `expr` operands.
>     See §7.1.1.
> 19. **Module order inside `namespace eval`**: variables →
>     helper procs → mid-level → public procs → engine-conditional
>     init block → `package provide` at the very end. See §12.
> 20. **`file join` is mandatory** for path construction; never
>     concatenate strings with `/`. The "directory of current
>     script" idiom is
>     `[file normalize [file dirname [info script]]]`. See §15.
> 21. **Error messages**: lowercase first word, **no trailing
>     punctuation**, `appendArgs` for multi-part composition.
>     Standard openings: `cannot ...`, `must be ...`, `invalid
>     ...`, `wrong # args: should be "..."`. See §16.
> 22. **`regexp` and `regsub` always include `--`** to terminate
>     option processing before the pattern. See §18.6.
> 23. **Bodies of `if` / `elseif` / `else` are ALWAYS braced**,
>     even single-statement bodies: `if {$x} then { return }`,
>     never `if {$x} return`. See §4.5.
> 24. **TH8 base-path security** — under TH8, `pwd` returns the
>     current directory **relative to the base path** (not
>     OS-absolute); `cd` confines navigation to the base
>     subtree (rejects paths that escape the base).  Both work
>     for in-subtree navigation under all three engines.
>     Don't treat `[pwd]` as OS-absolute in cross-engine code;
>     when you need an OS-absolute anchor, use `[file normalize
>     [file dirname [info script]]]`.  See §15.5.
> 25. **TH8 package names are lowercase** (`th8`, `th8test`,
>     `th8sqlite3`); Eagle uses PascalCase (`Eagle`,
>     `Eagle.Test`).  `package require th8` must be the FIRST
>     `package require` in any TH8 script that depends on
>     `[isEagle]`, `[isTh8]`, `[appendArgs]`, etc.  See §13.1,
>     §6.4.
> 26. **`.b64sig` files are RSA signatures** over their parent
>     script.  NEVER hand-edit, lint, or reformat them.  After
>     ANY content change to a `.tcl`/`.eagle`/`.th8` shipped
>     under signed-only policy, re-sign via
>     `bash tools/signScript.sh <file>`.  See §20.7.
> 27. **`nproc` is engine-specific** — Eagle's `nproc` defines
>     a .NET-callable native-frame proc; TH8's `nproc` defines
>     a procedure with named (keyword) arguments.  Same name,
>     unrelated semantics.  Disambiguate with `[isEagle]` /
>     `[isTh8]`; `[info commands nproc] > 0` is truthy in either
>     engine for different reasons.  See §17.2.
> 28. **Argument expansion: `eval [list ...]`, not `{*}`.**
>     **Eagle does not support `{*}`.**  Cross-engine code
>     forwards variadic args via `eval [list realCmd] $args`,
>     not `realCmd {*}$args`.  The wrapped-`[list]` form is
>     just as type-safe and works under all three engines.
>     `{*}` is acceptable only in code explicitly gated to
>     Tcl 8.5+ / TH8.  TH8's custom `{tag}value` operators
>     have the same non-portability.  See §7.7, §8.7, §20.9.
> 29. **Event loop: `[update]` and `[vwait]` only.**  Scripts
>     never enqueue events.  Embedders inject work via
>     `Th8_QueueEvent` (one of two thread-safe public APIs,
>     alongside `Th8_CancelEval`).  Never busy-wait via
>     `update`; use `vwait -timeout MS varName`.  See §8.10.
> 30. **R-markers are MD5-derived, not random.**  Editing
>     requirement text changes the marker.  After any edit:
>     `mkreq.tcl --refresh`, then update every test that
>     references a renamed marker, then re-sign only the
>     touched test files.  `--check-tests` MUST report 0
>     orphans / 0 uncovered.  See §20.10.
> 31. **TH8 testlib is `::th8testlib::*` and is for tests
>     only.**  Production scripts MUST NOT call into it ---
>     the namespace is not a stable public surface.  Gate on
>     `crypto_enabled` / `crypto_disabled` / `fault_injection`
>     constraints rather than probing inside `-body`.  See
>     §10.5.1, §20.11.

---

## Table of contents

1. [File layout & headers](#1-file-layout--headers)
2. [Whitespace & indentation](#2-whitespace--indentation)
3. [Line layout & continuation](#3-line-layout--continuation)
4. [Brace placement](#4-brace-placement)
5. [Comments](#5-comments)
6. [Naming conventions](#6-naming-conventions)
7. [Quoting & substitution](#7-quoting--substitution)
8. [Control-flow idioms](#8-control-flow-idioms)
9. [Variable & data-structure idioms](#9-variable--data-structure-idioms)
10. [Test-framework conventions](#10-test-framework-conventions)
11. [Eagle / .NET-specific patterns](#11-eagle--net-specific-patterns)
12. [Module / file organization](#12-module--file-organization)
13. [Package handling](#13-package-handling)
14. [Channel I/O patterns](#14-channel-io-patterns)
15. [File system patterns](#15-file-system-patterns)
16. [Error message conventions](#16-error-message-conventions)
17. [Procedure variants: `proc`, `nproc`, `s_proc`, `f_proc`](#17-procedure-variants-proc-nproc-s_proc-f_proc)
18. [Runtime helpers and idioms](#18-runtime-helpers-and-idioms)
19. [Cross-engine compatibility patterns](#19-cross-engine-compatibility-patterns)
20. [TH8-specific divergences (intentional & to-keep)](#20-th8-specific-divergences-intentional--to-keep)
21. [Anti-patterns (must not do)](#21-anti-patterns-must-not-do)
22. [Tooling](#22-tooling)
23. [Eagle language semantics (Eagle vs. Tcl)](#23-eagle-language-semantics-eagle-vs-tcl)

---

## 1. File layout & headers

### 1.1 File header block (REQUIRED)

Every script begins with a divider line of `#` characters
**padded to column 79** (see §1.3 for the divider-width rule),
a structured header comment, and a closing divider of the same
form. At column 0 the run is 79 `#` characters:

```tcl
###############################################################################
#
# list.eagle --
#
# Extensible Adaptable Generalized Logic Engine (Eagle)
# Eagle List Package File
#
# Copyright (c) 2007-2012 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
# RCS: @(#) $Id: $
#
###############################################################################
```

Reference: `Eagle/lib/Eagle1.0/list.eagle:1-15`.

A public-domain variant exists for SQLite-derived files:

```tcl
###############################################################################
#
# basic.eagle --
#
# Written by Joe Mistachkin.
# Released to the public domain, use at your own risk!
#
###############################################################################
```

Reference: `sqlite/dotnet/Tests/basic.eagle:1-7`.

**Rules:**

- The first line is exactly 79 `#` characters (file-header
  dividers start at column 0).
- Line 3 has the file name followed by ` --` (dash-dash).
- The project description line (e.g. "Extensible Adaptable…")
  is exactly one line; replace with project context.
- The copyright line ends with `All rights reserved.` (or the
  public-domain variant for SQLite).
- The license-pointer line is verbatim:
  `See the file "license.terms" for information on usage and redistribution of`
  followed by:
  `this file, and for a DISCLAIMER OF ALL WARRANTIES.`
- The `RCS: @(#) $Id: $` marker is present even when the file
  is not under RCS — it is a convention, not a real expansion.
- The closing line is again 79 `#` characters at column 0.
- One blank line after the header before any code.

### 1.2 Form-feed (`\x0C`) page breaks between procs

Every pair of adjacent procs **inside the same `namespace eval`
block** is separated by a single form-feed character (ASCII 12,
`\x0C`, `^L`) **alone on its own line**, between the proc that
closes and the block comment that introduces the next proc.

```
  proc lappendArgs { args } {
    ...
  }
^L
  #
  # NOTE: This procedure pseudo-randomly shuffles the specified list value
  #       ...
  #
  proc lshuffle { list } {
    ...
  }
```

(`Eagle/lib/Eagle1.0/list.eagle:32-38` — the byte between line 32's
`}` and line 34's `#` is `^L`.)

**Rules:**

- The form-feed is the **sole character on its own line** —
  no leading whitespace, no following text, just `\x0C\n` (or
  `\x0C\r\n` on Eagle's CRLF files).
- One blank line (containing only the form-feed) separates the
  closing `}` of one proc from the next proc's leading
  `# NOTE:` block.
- Form-feeds appear **between procs**, never:
  - Before the first proc in a namespace.
  - After the last proc in a namespace (the closing `}` of the
    namespace follows directly).
  - Inside a proc body.
  - Between top-level statements outside any namespace.
- Empirical confirmation: `test.eagle` has 388 form-feeds for
  ~390 procs; `list.eagle` has 7 form-feeds for 8 procs.

The convention exists for editor-level navigation: many editors
(Emacs, vi `]]`/`[[`) treat `^L` as a page-break delimiter,
giving rapid proc-to-proc movement. Modern IDEs ignore the byte;
losing it costs no functionality but breaks the convention.

**Display:** Editors that don't render `^L` may show it as a
small graphic (`◆`, `↡`) or hide it; `cat` shows it as the
control byte. `od -c` displays it as `\f`.

### 1.3 Section dividers (the pad-to-column-79 rule)

A divider is a run of `#` characters preceded by enough leading
whitespace to match the indent column of the construct it
separates, and **padded with `#` characters to column 79** —
so the total line width is always 79 characters regardless of
indent level.

**Width by indent column:**

| Leading column | `#` run width | Total width |
|---:|---:|---:|
| 0 (top level) | 79 | 79 |
| 2 (inside `namespace eval`) | 77 | 79 |
| 4 (inside an `if {…} then {` inside namespace) | 75 | 79 |
| 6 (inside a proc body inside namespace) | 73 | 79 |
| 8 (inside a nested block inside that proc) | 71 | 79 |

The intent: every divider terminates at column 79 so the line
fits within an 80-column terminal with one trailing newline,
regardless of how indented the surrounding code is.

#### 1.3.1 Top-level dividers (between sections / tests)

```tcl
###############################################################################

runTest {test array-1.1 {array set} -setup {
  unset -nocomplain x
} -body {
  array set x [list a 1 b 2 c 3]
} -result {}}

###############################################################################

runTest {test array-1.2 {array get} -body {
  testArrayGet x
} -constraints {eagle} -result {a 1 b 2 c 3}}
```

Reference: `Eagle/Library/Tests/array.eagle:18-37`.

#### 1.3.2 Namespace-level dividers (inside `namespace eval`)

When dividing groups of procs inside a namespace, the divider
sits at the namespace's body indent — column 2 for a single
namespace level, deeper when nested (see the references below):

```tcl
namespace eval ::Eagle {
  proc foo {} { ... }

  #############################################################################

  proc bar {} { ... }
}
```

Reference: `Eagle/lib/Eagle1.0/test.eagle:6753-6757` (indent 4,
75 `#` chars), `:7027-7034` (indent 6, 73 `#` chars).

#### 1.3.3 In-proc dividers (between phases of a complex procedure)

Procedures with multiple distinct phases — initialization,
validation, the main work, cleanup — may use dividers **inside
the proc body** to separate phases visually. The divider sits
at the proc body's indent (typically column 4 inside a
namespace, column 6 if doubly nested), again padded to column
79:

```tcl
  proc loadTetrisViaTclTk { {security ""} {path ""} {waitType Tcl} } {
    if {![isEagle] && ![info exists ::env(EAGLE)]} then {
      error "the EAGLE environment variable is not set"
    }

    ###########################################################################

    if {[isEagle] && [string length $security] > 0} then {
      ...
    }

    ###########################################################################

    set script(try) {
      ...
    }
  }
```

Reference: `Eagle/lib/Eagle1.0/test.eagle:1138+` (`loadTetrisViaTclTk`).

**When to use in-proc dividers:**

- The proc is genuinely multi-phase (≥3 distinct phases) and a
  reader will benefit from seeing the seams.
- Each phase is at least ~5 lines of code; smaller phases stay
  unseparated.
- The divider replaces what would otherwise be a paragraph
  comment ("Phase 1:" / "Phase 2:") — it is visual punctuation,
  not redundant.

Do **not** use in-proc dividers inside short procs or inside
control-flow bodies — they should appear at the **top level of
the proc body** only.

#### 1.3.4 Common rules for all dividers

- One blank line above and one blank line below each divider.
- The divider is the separator; the blank lines are the
  breathing room. Don't merge them (no "blank line + divider +
  code" without the second blank line).
- The line is **only `#` and the leading indent** — no other
  text, no trailing comment, no embedded text inside the run.
- In test files: one divider between every adjacent test (the
  rhythm is `divider / test / divider / test / …`).
- Dividers separate **logical groups** (test groups, proc
  groups, proc phases) — not individual items unless the items
  are themselves substantial.

### 1.4 Sourcing prologue / epilogue

Test files source a per-suite `prologue.eagle` and `epilogue.eagle`:

```tcl
source [file join [file normalize [file dirname [info script]]] prologue.eagle]

###############################################################################

# ... tests here ...

###############################################################################

source [file join [file normalize [file dirname [info script]]] epilogue.eagle]
```

Reference: `Eagle/Library/Tests/basic.eagle:16` and `:101+`.

The full `[file normalize [file dirname [info script]]]` chain is
deliberate: it works inside `[source ...]`, `interp eval`, and across
working-directory changes.

### 1.5 Trailing blank lines

End the file with **exactly one** newline after the last
non-blank line (no trailing blank lines).

---

## 2. Whitespace & indentation

### 2.1 Indent unit: 2 spaces, never tabs

Reference: every file in the corpus uses 2-space indentation.
Verify:

```tcl
namespace eval ::Eagle {
  proc lappendArgs { args } {
    #
    # NOTE: This should work properly in both Tcl and Eagle.
    #
    set result [list]; eval lappend result $args
  }
}
```

(`Eagle/lib/Eagle1.0/list.eagle:22-32`)

Column positions:
- `namespace eval` at column 0
- `proc` at column 2 (inside namespace)
- `set ...` (proc body) at column 4
- nested control body at column 6
- doubly-nested at column 8

**Rules:**

- Every nesting level adds **exactly 2 spaces**.
- **No tab characters** appear in any file. (If you find one, it
  is a defect.)
- This applies to library code, test code, comment continuation,
  AND content inside `runTest { ... -body { ... } }` blocks.

### 2.2 Trailing whitespace

Lines have **no trailing whitespace** (except the file's final
newline). Inspect with `cat -E` or `git diff --check` before
committing.

### 2.3 Blank lines

- **Inside a proc body:** one blank line between logical sections
  (e.g. between a `# NOTE:` block and the code it describes is
  *one* blank line; between two phases of a multi-phase proc is
  *one* blank line).
- **Between top-level constructs in a file:** one blank line
  *plus* a divider line padded to column 79 (see §1.3) plus
  another blank line — the divider is the separator, not the
  blank lines themselves.
- **Never two consecutive blank lines** in the body of a proc.
- **At most three** consecutive blank lines anywhere (rare and
  almost always around major file sections). One is the typical
  case.

### 2.4 Line endings

- **Eagle** source: CRLF (Windows-style). Verified via `od -c`
  on `Eagle/lib/Eagle1.0/list.eagle`.
- **TH8** source (existing): LF.
- When editing, **match the file you are editing** — never mix
  styles within one file.

### 2.5 Code stanzas

A proc body is written as a sequence of **stanzas** — visual paragraphs, each a
single logical step, separated by one blank line (§2.3). The reader follows a
proc paragraph by paragraph.

**Anatomy.** A stanza opens with the variable initialization it needs (`set`,
`array set`, `variable`, `lassign`), then — after **one blank line** — the code
that uses those variables:

```tcl
  set count [llength $items]
  set result [list]

  foreach item $items {
    lappend result [transform $item]
  }
```

The blank line after an initialization block is the boundary between the stanza's
set-up and its work.

**A Tcl reason this matters *more*, not less.** In a braced language a local's
lifetime is bounded by its braces; in Tcl a variable `set` anywhere lives for the
**whole proc**, no matter where the `set` appears. So stanza placement is the
*only* signal of a variable's intended scope and lifetime — the language will not
enforce it for you. A `set` right before the stanza that uses it says "local to
here"; a `set` hoisted to the top of the proc says "shared."

**Placement — scope of use, then taste.**

- A variable used by **one** stanza is `set` **just in time**, at the head of
  that stanza.
- A variable **reused across stanzas**, an **accumulator** (`set result [list]`
  ahead of a loop that `lappend`s to it), or one that must survive a
  `catch` / `try`, is initialized in a **block at the top of the proc**, followed
  by a blank line.
- Borderline cases follow taste and anticipated direction; the corpus uses both.

**Separators.** One blank line separates ordinary stanzas (§2.3); a multi-phase
proc may separate its major phases with an in-proc divider padded to column 79
(§1.3.3). Never two consecutive blank lines in a body (§2.3).

---

## 3. Line layout & continuation

### 3.1 Working line length

The corpus targets ~80 columns. Most lines are well under; lines
genuinely longer than ~80 columns wrap with `\` continuation. The
column limit is *soft* — readability beats column count, especially
in test result strings and long literal data.

### 3.2 Backslash continuation

Long statements break with `\` at the end of the line. The
continuation line is indented **+4 spaces** from the original
statement's column (i.e. one continuation level beyond the
statement's own indent).

```tcl
    return [expr {[info exists ::tcl_platform(engine)] && \
        [string compare -nocase eagle $::tcl_platform(engine)] == 0}]
```

(`Eagle/lib/Eagle1.0/init.eagle:40-41` style)

```tcl
    set directory [file normalize [file join \
        [info base] bin $platform1 [appendArgs \
        $configuration Dll $suffix]]]
```

(`Eagle/lib/Eagle1.0/init.eagle:101-103`)

**Rules:**

- The `\` at end-of-line has **one space before it** when the
  preceding token is a word; no space when preceded by `]` or `}`.
  (Inspect existing lines to settle ambiguous cases.)
- The break point is logical: at a token boundary, after an
  operator (`&&`, `||`, `==`), after a `,` in arg lists, before
  the `\` ending a continued shell-like option list. Avoid
  breaking inside a quoted string when possible.
- Continuation indent is **+4 spaces**, not aligned to the open
  bracket. This preserves visual hierarchy and survives indent-
  width changes.
- **Exception (double-indent):** when the broken construct is
  immediately followed by a body block at +4 — a long condition
  in `if {...} then {...}` / `switch`, or a broken proc parameter
  list — its continuation goes to **+8** to clear that body
  (see §17.5).

### 3.3 Wrapping `for { init } { test } { incr } {`

Long `for` headers wrap with `\`:

```tcl
    for {set length [llength $result]} \
        {$length > 1} {lset result $index $element} {
      set index [expr {int(rand() * $length)}]
      ...
    }
```

(`Eagle/lib/Eagle1.0/list.eagle:46-51`)

The opening `{` of the body is on the line with the last
`for` clause.

### 3.4 Multi-line argument lists

When a command has arguments that don't fit, break BEFORE each
significant argument and continue at +4 spaces:

```tcl
    testClrExec $testExeFile [list -eventflags Wait -directory \
        [file dirname $testExeFile] -stdout output -success Success] \
        -autoRun -fileName [appendArgs \" [file nativename $fileName] \"]
```

(`sqlite/dotnet/Tests/basic.eagle:36-38`)

### 3.5 Long test `-result {…}` strings

When a literal result string overflows, break with `\` *inside* the
braces (Tcl preserves the line continuation):

```tcl
} -constraints {eagle SQLite file_System.Data.SQLite.dll file_test.exe\
testExec winForms} -result {0 {}}}
```

(`sqlite/dotnet/Tests/basic.eagle:61-62`)

The continuation is flush left (no leading whitespace) inside
the brace literal — adding indent would change the test result.

---

## 4. Brace placement

### 4.1 Universal rule: opening brace on the same line

Every block construct opens on the same line as the keyword:

```tcl
proc name { args } {                    # proc
  body
}

if {cond} then {                        # if (note: `then` keyword)
  body
} elseif {cond2} then {
  body
} else {
  body
}

foreach element $list {                 # foreach
  body
}

while {cond} {                          # while
  body
}

for {init} {test} {incr} {              # for
  body
}

switch -exact -- $value {               # switch
  pattern1 {
    body
  }
  default {
    body
  }
}

namespace eval ::Eagle {                # namespace eval
  body
}

catch {                                 # catch
  risky
} result
```

References: `Eagle/lib/Eagle1.0/list.eagle:27-32`, `:62-69`,
`:46-51`; `Eagle/lib/Eagle1.0/object.eagle:100-113`.

### 4.2 Closing brace placement

The closing `}` is **flush left at the indentation column of the
line that opened the block** — never indented further, never on
the same line as the last statement (with one exception, below).

```tcl
proc isEagle {} {
  return [expr {...}]
}                                       # } at col 0 -- matches `proc`
```

The exception: short forms with no body content,
`if {!$flag} then { return }`, are written with the closing `}`
on the same line. Use sparingly; multi-line bodies always use
the standard layout.

### 4.3 Spaces inside braces

The corpus uses **two distinct conventions** by context:

- **Argument lists in `proc`:** `proc name { args }` — spaces.
  Reference: `Eagle/lib/Eagle1.0/list.eagle:27`.
- **Default-argument forms inside the arglist:** `{ name {default} }`
  — outer spaces, no inner spaces. Reference:
  `Eagle/lib/Eagle1.0/test.eagle:60`:
  `proc s_proc { name {args ""} {command ""} } { ... }`
- **Conditions in control flow:** `if {cond} then {` — **no
  space** inside the condition braces. Reference: every
  `if`/`while`/`foreach` in the corpus.
- **Static lists:** `{a b c}` — no spaces. Reference:
  `Eagle/Library/Tests/foreach.eagle:21`.
- **Code-block braces (e.g. `-body { … }` arguments):** the space
  after `{` is present when the block contains content; the
  closing `}` is on its own line (or alone on a continuation
  line for tight tests).

These conventions are **stable in the corpus**; do not "fix" them
to a single uniform rule.

### 4.4 The `then` keyword (REQUIRED)

`if` always uses `then`:

```tcl
if {[isEagle]} then {
  ...
} elseif {[isMono]} then {
  ...
} else {
  ...
}
```

Reference: `Eagle/lib/Eagle1.0/exec.eagle:32-40`,
`Eagle/lib/Eagle1.0/init.eagle:21-22`, and pervasive throughout.

This is **the single most distinctive rule** in the corpus.
Standard Tcl allows omitting `then`, but this style **requires** it
for all `if` and `elseif` clauses (the `else` clause never takes a
`then`, of course).

### 4.5 Bare-command bodies (BANNED)

Tcl accepts a bare command as the body of an `if` / `elseif` /
`else`:

```tcl
if {$flag} continue                    ;# Tcl-legal but BANNED
if {$x == 0} return                    ;# BANNED
if {$count > $limit} break             ;# BANNED
} else return                          ;# BANNED
```

The project style **always wraps** the body in `{ ... }` with
the `then` keyword (or `else { ... }` form):

```tcl
if {$flag} then { continue }           ;# RIGHT
if {$x == 0} then { return }           ;# RIGHT
if {$count > $limit} then { break }    ;# RIGHT
} else { return }                      ;# RIGHT
```

**Why:**

- Visual consistency with multi-line `if`/`else` bodies — every
  body is `{ ... }`-wrapped, so the reader's eye doesn't have to
  context-switch.
- `tools/format_scripts.tcl --brace-bodies` (default ON) auto-
  wraps existing bare-body forms, so authoring code in either
  form is safe; the formatter normalizes.
- The wrapped form composes cleanly with later edits (adding a
  second statement to the body just requires a new line inside
  the braces — no need to re-add the braces).

This rule applies to bare bodies of `continue`, `return`,
`break`, `exit`, and any single-command body. Multi-line bodies
were already braced; the rule only adds the wrap to the common
single-statement shorthand.

---

## 5. Comments

### 5.1 Comment marker

Every comment uses `#`. Tcl does not support `//` or `/* */`.

### 5.2 Block comments (the dominant form)

A "block comment" is a multi-line `#`-comment that documents a
proc, a section, or a non-trivial decision. The structure:

```tcl
  #
  # NOTE: This procedure returns non-zero if the specified database
  #       column is within the specified database row value.
  #
  proc haveColumnValue { row column } {
    #
    # NOTE: Locate the index of the named column we are interested
    #       in.  This requires Tcl 8.5 (or higher) or Eagle.
    #
    set index [lsearch -exact -index 0 $row $column]

    #
    # NOTE: Did we find the column name in the row?
    #
    return [expr {$index != -1}]
  }
```

(`Eagle/lib/Eagle1.0/database.eagle:23-44`)

**Rules:**

- The first line of the block is `#` alone (a "blank" `#` line)
  followed by `# NOTE: …` (or another marker) on the next line.
- The last line of the block is again `#` alone.
- Continuation lines are aligned under the **text** after the
  marker (`# NOTE: ` is 8 chars, so continuation text aligns
  7 spaces after the `#` — i.e. `#       continuation`).
- The block is placed **directly above the code it describes**
  (no blank line between block and code) when the block describes
  a single statement, or with one blank line when it introduces
  the *whole* proc.
- Inside a proc body, block comments are indented to the proc
  body's column.

### 5.3 Comment markers (semantic)

- **`# NOTE:`** — explanatory commentary, the most common form.
- **`# WARNING:`** — load-bearing constraint; reader must heed.
- **`# TODO:`** — known incomplete work.
- **`# HACK:`** — pragmatic workaround that is intentionally
  imperfect; explain why a clean solution is not used.
- **`# MONO:`** — Mono-specific quirk or workaround.
- **`# BUGBUG:`** — known defect; mark for follow-up.
- **`# BUGFIX:`** — note about a fix; explain the bug it addresses.

References: `Eagle/lib/Eagle1.0/test.eagle:22` (WARNING),
`Eagle/lib/Eagle1.0/test.eagle:26` (TODO),
`scratch/eagle/startup/startup.eagle:35` (HACK),
`Eagle/Library/Tests/basic.eagle:19` (MONO).

### 5.4 Why-comments, not what-comments

Comments explain **intent, rationale, and constraints** — not what
the code literally does. Read the corpus and you will rarely find
"increment counter" above `incr count`. You will find
"NOTE: This requires Tcl 8.5 (or higher) or Eagle" above a
`lsearch -index` call.

### 5.5 Trailing comments

End-of-line trailing comments use `;` to terminate the statement
followed by `# `:

```tcl
        return [info nameofexecutable]; # native Tcl
```

```tcl
      lappend skipNames namespace-7.3; # NOTE: Needs [trace].
```

References: `Eagle/lib/Eagle1.0/exec.eagle:39`,
`Eagle/Library/Tests/namespace.eagle:35`.

**Rules:**

- The `;` terminates the Tcl statement (so the comment does not
  run into the next line).
- Exactly one space before `#`, exactly one space after.
- Use sparingly — multi-line block comments are preferred for
  anything more than a 4-7 word remark.

### 5.6 Trailing comments on `proc { args }` declarations

When a proc takes args with non-obvious ordering or conventions,
a trailing comment on the `{` line is permitted:

```tcl
  proc s_proc { name {args ""} {command ""} } {; # NOTE: "args" not last.
```

(`Eagle/lib/Eagle1.0/test.eagle:60`)

Note the `;` before the `#` even though there is nothing to
terminate (the `{` already opens the body). This is idiomatic.

### 5.7 Documenting procs

There is **no formal docstring format** (no `@param`, no
`@return`). The block comment above the proc explains what the
proc does and any constraint the caller must meet. Tcl's
dynamic typing makes parameter-type docs less useful than they
are in C# / Java; emphasize **purpose and constraints**.

The canonical shape:

```tcl
#
# procName --
#
#   One-paragraph description of what the proc computes or
#   does, written in declarative present tense.  Mention what
#   it returns, any side effects (variable mutations, file
#   I/O, network calls, namespace changes), preconditions
#   the caller must satisfy, and the engines / contexts where
#   it works.  Tie back to the caller(s) when that adds
#   information the reader cannot derive locally.
#
proc procName { args } { ... }
```

Reference: `lib/Standard1.0/test.tcl:46-72` (`ldifferences`)
and `:74-90` (`isAdministrator`).

**Rules:**

- The block is one or more `#`-prefixed lines wrapped in
  blank `#` lines top and bottom.  The proc declaration
  follows on the very next line — no blank line between the
  closing `#` and the `proc`.
- Line 1 is `# procName --` (no space between `--` and the
  preceding name; one space after the leading `#`).
- The description body indents two spaces past the leading
  `#` (i.e. `#   …`), not three or four.  This indent matches
  the C-comment convention used elsewhere in the project and
  keeps the marker columns aligned for tooling that scans
  comment blocks.
- Wrap to ~72 columns inside the comment so the rendered
  doc fits in an 80-column terminal even with the leading
  `#   ` and any tree-view indentation.
- Describe **return value** in prose, not a `Returns:` field:
  `… returns the symmetric difference as …`.  When the
  proc returns nothing, say so (`… invoked for its side
  effect of …`).  Do not annotate type — Tcl values are
  strings; the prose names the *meaning*.
- Describe **side effects** explicitly: variable writes
  (`… records the failure in ::th8test::failed`), file
  writes (`… appends to the test log`), platform calls
  (`… invokes the OS hostname syscall`).  The block
  comment is the only mention of side effects most readers
  will see; missing them invites surprise.
- Describe **engine constraints** when relevant: `…
  Eagle-only.  Falls through to TH8's built-in foo when
  not present.`.  This is more informative than the
  per-call `[isEagle]` guard and surfaces dispatch
  decisions that callers make based on this proc.
- Do **not** include parameter-type tables or `# var: type
  description` lines.  Tcl has no static types; a per-arg
  table just duplicates the prose and rots when the
  signature changes.  When a parameter's accepted shape is
  non-obvious (e.g. "a list of two-element sub-lists"),
  describe the shape inline in the prose.
- Tag-comment annotations (`# <public>`, `# <bootstrap>`,
  etc.; see §5.8) go **between** the description block and
  the `proc` declaration, not inside the block.  The block
  is for humans; the tags are for tooling.

### 5.8 Tag-comment annotations (HIGHEST-VALUE BEST PRACTICE)

The Eagle codebase uses **XML-style tag comments** to attach
machine-readable metadata to procs. There are two distinct
syntaxes:

**Single-angle tags** — placed immediately above a `proc` or
`nproc` definition, **between** the prose `# NOTE:` block and the
proc itself. Each is a single line `#` comment:

| Tag                       | Meaning                                          |
|---------------------------|--------------------------------------------------|
| `# <public>`              | Public API (intended to be called from outside the namespace). |
| `# <bootstrap>`           | Must run during interpreter bootstrap; cannot rely on infrastructure not yet loaded. |
| `# <experimental>`        | Subject to change or removal without notice.    |
| `# <demonstration>`       | Illustrative example, not for production use.   |
| `# <callback>`            | Invoked indirectly by .NET / web / external callback machinery. |
| `# <create>`              | Constructor or factory: returns a new object/handle. |
| `# <th8Incompatible>`     | Uses Eagle/Tcl features unavailable under TH8, or relies on non-TH8 semantics — e.g. `[object invoke]`, or code that depends on OS-absolute `pwd`/`cd` (which work under TH8 but are base-relative, §15.5). |
| `# <help>` ... `# </help>` | Multi-line help block (see below).             |

**Double-angle inline annotations** — placed **inside** the proc
body (typically the first lines), recognised by Eagle's
`annotateProcedure` mechanism:

| Annotation                | Meaning                                          |
|---------------------------|--------------------------------------------------|
| `# <<inline>>`            | Body should be evaluated in the caller's frame (no new call frame). |
| `# <<nonCaching>>`        | Result must not be cached.                      |
| `# <<...>>` (other)       | Project-extensible; see `annotateProcedure`.    |

#### 5.8.1 The `# <public>` tag

Marks a proc as part of the namespace's public API. Place it on
its own line **after** the `# NOTE:` block describing the proc
and **before** the `proc` line:

```tcl
  #
  # NOTE: This procedure helps to declare procedures to be annotated as
  #       "private", which means they cannot be called outside of their
  #       own namespace.  This will not work on procedures in the global
  #       namespace.
  #
  # <public>
  proc private { command name arguments body } {
    # <<inline>>
    # <<nonCaching>>

    return [annotateProcedure \
        private true $command $name $arguments $body]
  }
```

(`Eagle/lib/Eagle1.0/test.eagle:644-657`)

Procs that are namespace-internal helpers receive **no tag**.
Tooling and IDEs can use the tag to gate completion lists or
warn on cross-namespace calls to untagged procs.

#### 5.8.2 The `# <bootstrap>` tag

Procs that must work **before** the rest of the library is
loaded — typically `isEagle`, `isMono`, language-engine probes,
and library-loading helpers. The tag signals to maintainers
that these procs must:

- Avoid calling other library procs that may not yet be loaded.
- Avoid features that depend on platform initialization.
- Be defined as early as possible in the load order.

```tcl
  #
  # NOTE: This procedure can be used by callers wishing to write code that
  #       is portable between Tcl and Eagle.  This procedure must exist
  #       and must return non-zero only when running in Eagle.  This
  #       procedure must be defined in this script file because it is
  #       needed while this script file is being evaluated.  The same
  #       procedure is also defined in the "platform.eagle" file.
  #
  # <bootstrap>
  proc isEagle {} {
    return [expr {[info exists ::tcl_platform(engine)] && \
        [string compare -nocase eagle $::tcl_platform(engine)] == 0}]
  }
```

(`Eagle/lib/Eagle1.0/init.eagle:21-42`)

#### 5.8.3 The `# <help>` ... `# </help>` block

A multi-line help comment retrievable at runtime by a `[help
PROC]` command. Place the block **as the first thing in the
proc body**, before any code:

```tcl
  proc tryToLoadZeus { {quiet false} } {
    # <help>
    # This procedure attempts to load the Zeus plugin, which is part
    # of Eagle Enterprise Edition.  It allows encrypted scripts to be
    # encrypted, decrypted, and evaluated.  Since it is a commercial
    # product, some kind of license certificate for it must be available
    # on the local computer (e.g. a trial license certificate from the
    # associated NuGet package, etc) -OR- it will fail to load.  Upon
    # success, this procedure will return non-zero.
    # </help>

    if {[catch {
      ...
```

(`Eagle/lib/Eagle1.0/test.eagle:1107-1135`)

**Rules:**

- The opening `# <help>` and closing `# </help>` are each on
  their own line, indented to the proc body's column.
- Each content line begins with `# ` (single space) at the
  body's indent column.
- One blank line separates the closing `# </help>` from the
  first line of code.
- Help text is **prose for end users**, not internal commentary;
  use `# NOTE:` blocks for the latter.
- Help text should describe what the proc does, why a caller
  would invoke it, and any non-obvious constraints / failure
  modes — typically 5-15 lines.

#### 5.8.4 The `# <callback>` tag

Marks a proc that is invoked **indirectly** — typically by .NET
infrastructure (web client callbacks, scheduled timers,
event-loop dispatchers). The tag is a hint to maintainers:

- Don't rename this proc (external code references the name).
- Don't change its signature.
- Treat its execution context as caller-supplied (it may run
  in any thread / safe-interp / restricted context).

```tcl
    #
    # NOTE: This procedure may be used by the _Tests.Default.ScriptWebClient
    #       class methods when it has been setup for an interpreter.  This
    #       procedure is designed to be used with the [openAI] procedure (below).
    #       This procedure must reside in the global namespace.
    #
    # <callback>
    nproc openAI_scriptWebClient { args } {
      ...
```

(`Eagle/lib/Eagle1.0/test.eagle:11944-11955`)

#### 5.8.5 The `# <create>` tag

Marks a constructor / factory proc — one that allocates and
returns a new object handle, file handle, channel, etc. The tag
helps tooling identify ownership transfer (caller must clean
up).

#### 5.8.6 The `# <experimental>` tag

Marks features that are not yet stable. Maintainers may change
behaviour, signature, or remove the proc entirely without
deprecation. Callers should expect to update their code.

```tcl
    #
    # NOTE: This procedure marks a procedure for "fast" execution; for now,
    #       this means disabling anything that makes variable access slower
    #       while the target procedure is executing.
    #
    # <experimental>
    proc makeProcedureFast { name fast } {
      ...
```

(`Eagle/lib/Eagle1.0/init.eagle:608-622`)

#### 5.8.7 The `# <demonstration>` tag

Marks code present for didactic purposes (tutorial, regression
test foundation). Not maintained as production-quality;
maintenance bar is "still illustrates the point."

#### 5.8.8 Inline annotations: `# <<inline>>`, `# <<nonCaching>>`, ...

These are **first-line comments inside the proc body** that
Eagle's `annotateProcedure` mechanism reads via regex and turns
into per-proc behavioural attributes:

```tcl
  proc private { command name arguments body } {
    # <<inline>>
    # <<nonCaching>>

    return [annotateProcedure ...]
  }
```

(`Eagle/lib/Eagle1.0/test.eagle:651-657`)

**Rules:**

- Each annotation is on its own line, indented to the proc
  body's column.
- They are the **first non-whitespace lines in the body** — no
  code precedes them.
- One blank line separates the annotation block from the first
  line of executable code.
- Multiple annotations are stacked, one per line.
- The double-angle form `<<name>>` is distinct from the
  single-angle `<name>` outer tags — do not conflate.

#### 5.8.9 Tag ordering convention

The full ordering for a documented public proc with all the
trimmings:

```tcl
  #
  # NOTE: <free-form prose explaining purpose, rationale, constraints>
  #
  # <public>                          ;# (or <bootstrap>, <experimental>, etc.)
  proc procName { args } {
    # <help>
    # <End-user-visible help text describing what to call and why.>
    # </help>

    # <<inline>>                      ;# (zero or more inline annotations)
    # <<nonCaching>>

    body
  }
```

The order is fixed: prose → outer tag → proc → help block →
inline annotations → blank line → body. Each step is optional
but the **order must hold** when present.

---

## 6. Naming conventions

### 6.1 Procs: camelCase

All procs are camelCase: lowercase first word, capitalized
subsequent words.

```tcl
proc lappendArgs { args }
proc getDictionaryValue { dict name {default ""} {wrap ""} }
proc isEagle {}
proc isAdministrator {}
proc haveColumnValue { row column }
proc exportAndImportPackageCommands { namespace exports forget force }
```

Common verbal prefixes:

- `is`* — boolean predicates (`isEagle`, `isWindows`,
  `isInteractive`).
- `have`* — capability predicates (`haveColumnValue`,
  `haveSQLiteCompileOption`).
- `get`* — accessors (`getRuntimeCommandLine`, `getBuildYear`).
- `make`*, `build`*, `create`* — constructors.
- `check`* — assertion / validation.

### 6.2 Variables: camelCase

Local variables are camelCase: `result`, `index`, `length`,
`fileName`, `pairName`, `pairValue`, `methodFlags`. Global
variables are camelCase too, and are addressed by the `::`
prefix when accessed from a namespace:

```tcl
$::tcl_platform(engine)
$::eagle_platform(configuration)
$::env(NoStartupRunCommands)
$::test_year
```

### 6.3 Loop variables

Use **descriptive names** for non-trivial loops (`element`,
`flag`, `pairName`, `entry`); use **single letters** (`i`, `j`,
`k`) only for simple counters or destructured-foreach indices,
and only when the meaning is unambiguous from context.

```tcl
foreach element $list1 { ... }            # GOOD: descriptive
foreach {pairName pairValue} $dict { ... }  # GOOD: descriptive pairs
for {set index 0} {$index < $length} {incr index} { ... }
foreach {i j} {a b c} {k l} {d e f g} { ... }  # OK in tight test code
```

### 6.4 Namespaces

Library code lives inside a namespace:

```tcl
namespace eval ::Eagle {
  # all procs go here
}

namespace eval ::th8 {
  # ...
}

namespace eval ::Licensing {
  # ...
}
```

References: `Eagle/lib/Eagle1.0/list.eagle:22`,
`th8/lib/th8/init.th8:14`,
`scratch/eagle/startup/startup.eagle:72`.

**Casing rule** depends on the engine that owns the namespace:

| Engine | Casing       | Examples                                           |
|--------|--------------|----------------------------------------------------|
| Eagle  | **PascalCase** | `::Eagle`, `::Licensing`, `::Test`               |
| TH8    | **lowercase**  | `::th8`, `::th8test`, `::th8testlib`             |

The lowercase TH8 form mirrors the project's `th8` C identifier
prefix (`Th8_`, `th8Whatever`).  Mixing PascalCase namespace
names into TH8 code is incorrect; mixing lowercase into Eagle
code is incorrect.  See §20.4 for the full TH8 namespace table
including the `::th8test` (script-side) vs `::th8testlib`
(C-side) split.

### 6.5 Test names

`prefix-N.M` (group-major.minor):

```tcl
runTest {test array-1.1 {array set} ...}
runTest {test array-1.2 {array get} ...}
runTest {test list-1.1.1 {build lists, pre-TIP #148} ...}
runTest {test list-1.1.2 {build lists, post-TIP #148} ...}
```

References: `Eagle/Library/Tests/array.eagle:20+`,
`Eagle/Library/Tests/list.eagle:20-42`.

**Rules:**

- `prefix` matches the file name (typically): `array.eagle` →
  `array-*`, `expr.eagle` → `expr-*`.
- `N.M` is the standard form; `N.M.K` is used when a single
  conceptual test has Tcl-vs-Eagle variants or other paired
  variants.
- TH8 augments this with R-marker comments inside the test
  description; see §20.

### 6.6 Magic constants

Bind magic numbers to named variables when the meaning is not
obvious from context. The corpus uses inline hex (`0x7E`,
`0x40`) when:

1. The value is part of a self-contained algorithm (character-
   range processing).
2. The constant is reused in the immediate vicinity.

Otherwise, name it (`set methodFlags 0x40`).

---

## 7. Quoting & substitution

### 7.1 Three kinds of "string" quoting

| Form        | Substitution? | When to use                                        |
|-------------|---------------|----------------------------------------------------|
| `{…}`       | No            | Literal strings, code blocks, static lists, expr   |
| `"…"`       | Yes           | Strings that need `$var` or `[cmd]` substitution   |
| bare word   | Yes (variables) | Single-token literals (option flags, names)       |

#### 7.1.1 Prefer bare words — quote ONLY when necessary

**Tcl is not C.** A double-quoted or brace-wrapped string that
contains nothing but plain identifier characters is a **style
defect**, not a stylistic preference. The Tcl parser treats
each whitespace-separated token as a string already, so the
quoting adds no semantic value and only adds noise.

**Wrong:**

```tcl
set x "hello"                  ;# unnecessary quotes
lappend args "value"           ;# unnecessary quotes
puts "$msg"                    ;# unnecessary quotes around a bare $var
return "$result"               ;# unnecessary quotes
```

**Right:**

```tcl
set x hello
lappend args value
puts $msg
return $result
```

The wrap-only-when-necessary rule:

- **Use `"..."`** when the string contains:
  - whitespace (`"hello world"`)
  - characters that would otherwise be parsed specially (`"["`, `"]"`, `"$"`, `"\""`)
  - a substitution that needs to be embedded in a longer string (`"file=$name"`)
  - empty content (`""`)
- **Use `{...}`** when the string is:
  - a literal that contains `$` or `[...]` you do NOT want substituted
  - a code block (proc body, control-flow body, `eval` argument)
  - a static list with multiple words: `{a b c}`
  - the entire argument to `expr`: `expr {$x + 1}`
- **Otherwise: bare word.** Single-token identifiers, file
  names without spaces, option flags (`-foo`), command names,
  and bare variable references (`$name`) all stand on their
  own.

**Comparison-context exception:** the `eq` / `ne` operators
inside `expr` traditionally compare against a quoted RHS for
readability (`$mode eq "fast"`). Both `$mode eq "fast"` and
`$mode eq fast` are legal; the corpus uses both. When the RHS
is a single bare-word literal, prefer the bare form;
double-quote only when the literal contains whitespace or
special characters.

**Why the rule matters:**

- Spurious quotes obscure the read: a reader cannot tell at a
  glance whether the author *meant* the quotes (forcing string
  rather than list interpretation) or wrote them out of habit.
- Spurious quotes waste columns: long signatures hit the
  79-column limit faster.
- Spurious quotes can introduce subtle bugs: `"$x"` differs
  from `$x` when `$x` itself is a list — the quoted form forces
  list-element-to-string flattening, which is rarely desired.

**Examples:**

```tcl
set result [list]                       # bare-word `list` for the cmd
foreach element $list1 { ... }          # bare-word `element` (var name)
error "wrong # args: should be \"...\""  # double-quoted: needs substitution? no, but escaping
error [appendArgs \" $a "\" isn't an array"]  # double-quoted: appendArgs builds it
expr {$x + 1}                            # braced: expr always braced
runTest {test foo-1.1 { ... }}           # braced: literal (runTest does its own subst)
```

### 7.2 List literals: `{a b c}` vs `[list a b c]`

- **`{a b c}`** for static literals known at parse time, with no
  substitutions or special characters.
- **`[list ...]`** to compose a list from variable contents
  (`[list $name $value]`) or to start an empty list
  (`set result [list]`).

```tcl
foreach {i j} {a b c} {k l} {d e f g} { ... }   # static literals
set result [list]                                 # empty list start
lappend result [list MISSING $element]            # list of named pair
```

### 7.3 Variable substitution: `$name` vs `${name}`

Use `$name` by default. Use `${name}` only when the syntax is
otherwise ambiguous (e.g. immediately followed by a letter that
would otherwise be parsed as part of the variable name):

```tcl
set greeting "Hello, $userName!"           # standard
set padded "[${prefix}_default]"           # ${} disambiguates
```

The corpus uses `${name}` rarely; always have a reason.

### 7.4 Escapes

Standard Tcl escapes: `\t`, `\n`, `\r`, `\\`, `\"`, `\[`, `\]`,
`\xHH`. Prefer the escape form to literal embedded control
characters in source.

```tcl
set stringMap [list \b " " \t " " \r \xB6 \n \xB6]
```

(`Eagle/lib/Eagle1.0/compat.eagle:65`)

### 7.5 String composition: `format` vs `appendArgs` vs `subst`

- **`format`** for fixed-width or printf-style composition.
  Reference: `Eagle/lib/Eagle1.0/compat.eagle:99`.
- **`appendArgs`** (an Eagle/Tcl-shared utility) for
  side-effect-free concatenation that avoids unwanted
  substitution. Reference: ubiquitous; see
  `Eagle/lib/Eagle1.0/list.eagle:39`.
- **`subst`** for templated-string composition with deliberate
  substitution.

`appendArgs` is the Eagle idiom equivalent to:

```tcl
proc appendArgs { args } {
  set result ""; eval append result $args
}
```

(`Eagle/lib/Eagle1.0/auxiliary.eagle:39-41`)

Use it for messages that mix variables and literals without
needing positional formatting.

### 7.5.1 `subst` safety

`[subst $template]` performs THREE kinds of substitution by
default:

1. `$variable` substitution.
2. `[command]` substitution — **this evaluates arbitrary
   commands embedded in the template**.
3. `\backslash` escape processing.

For trusted templates (literals you wrote), this is fine.
For ANYTHING that originated from outside the script — user
input, file contents, network responses, environment
variables — bare `subst` is a code-injection vector.

**Rule:** when subst-ing untrusted text, disable command
substitution explicitly:

```tcl
# Untrusted template: turn off [...] command subst.
set safe [subst -nocommands $userTemplate]

# Even safer: only allow $var substitution.
set safer [subst -nocommands -nobackslashes $userTemplate]

# Most paranoid: do nothing at all (use the template as-is).
# At which point [subst] is the wrong tool — just use the
# string directly.
```

The flags compose: `-nocommands -novariables -nobackslashes`
together leaves only the literal text.  Use the most
restrictive combination that still does the substitutions
you actually want.

**Trusted-template idioms in the corpus** (§11.4 has the
canonical example):

```tcl
set code [compileCSharpWith [subst {
  using System;
  public class X { public static int F() { return $count; } }
}]]
```

Here the template is a C# source literal that the test
author wrote.  `$count` is a Tcl variable the author also
controls.  No `-nocommands` is needed because the entire
input is trusted.

**Anti-pattern:** `[subst $line]` where `$line` came from
`[gets $channel]`.  An attacker who controls the file's
content can write `[exec rm -rf /]` and have it executed by
the script's privileges.  Always pass `-nocommands` (and
typically `-nobackslashes`) when the template is not author-
controlled.

### 7.6 String predicates: `string is X -strict`

`string is X` returns 1 when its argument is a well-formed value
of class X.  The `-strict` flag is **mandatory** in this corpus
because the bare form treats empty strings as valid (which is
almost never what code actually wants):

```tcl
string is integer ""           ;# returns 1   (counterintuitive)
string is integer -strict ""   ;# returns 0   (correct)
```

Common classes used in the corpus:

| Class | Purpose |
|---|---|
| `integer` | accepts decimal, hex (`0x…`), octal (`0…`); range checked. |
| `wideinteger` | like `integer` but 64-bit range. |
| `double` | accepts decimal, scientific, optional sign. |
| `boolean` | accepts `0`/`1`, `true`/`false`, `yes`/`no`, `on`/`off` (case-insensitive). |
| `list` | well-formed Tcl list (no unbalanced braces). |
| `xdigit` | every character is a hex digit. |
| `space` | every character is whitespace. |

**Rules:**

- Always use `-strict` unless the empty-string-is-valid
  semantics are exactly what you want (rare; document it
  if so).
- For *output formatting* (e.g. matching FP results in test
  -result), use a regex pattern (§10.8.1), NOT `string is
  double -strict`.  The predicate accepts forms the output
  won't include (`0x1p+3`, `inf`, `nan`).
- For *input validation* at command boundaries, prefer
  `string is integer -strict` over `[catch {expr {…}}]` —
  the predicate is faster and doesn't side-effect the
  interp result.
- `string is boolean -strict` is **Tcl 8.5+ / Eagle / TH8**.
  Under Tcl 8.4 (still in use in some legacy projects),
  drop `-strict` (or gate the call); see §19.3 for the
  cross-version pattern.

**Pitfall: leading-zero integers.**  `string is integer
-strict 010` returns 1 and parses as **8** (octal) under
classic Tcl semantics; TH8 follows the same rule.  When
parsing user input where leading zeros are decimal digits
(zip codes, padded IDs), reject octal explicitly:

```tcl
if {[regexp {^0[0-9]} $value]} then {
  error "leading zero not allowed"
}
if {![string is integer -strict $value]} then {
  error "not an integer"
}
```

### 7.7 Argument expansion: `{*}list` (portability warning)

Tcl 8.5+ and TH8 support the **leading-`{*}` word prefix**
that expands a list value into individual arguments of the
surrounding command:

```tcl
set args [list -nocase -- $pattern $value]
string match {*}$args             ;# -> string match -nocase -- pattern value
```

**Eagle does not currently support `{*}`.**  This is the
single most common portability gotcha when porting Tcl 8.5+
code to Eagle: a script that runs cleanly under TH8 or
upstream Tcl will raise a parse error under Eagle the
moment the parser encounters `{*}`.

**Rule: prefer `eval [list cmd ...]` over `{*}` in
cross-engine code.**  `eval` is universally supported and
the `[list ...]` argument is automatically re-parseable by
the interpreter, so it is just as type-safe as `{*}` for
the common forwarding case:

```tcl
# PORTABLE -- works under Tcl, Eagle, and TH8.
proc wrap { args } {
  eval [list realCmd] $args
}

# NOT PORTABLE -- fails to parse under Eagle.
proc wrap { args } {
  realCmd {*}$args
}
```

The `[list ...]` wrapping defends against re-quoting
hazards (the historical `eval` footgun was concatenating
unquoted strings).  Use it whenever the command name or
fixed-position arguments need to remain literal:

```tcl
eval [list source $fileName]                    ;# good
eval [list lappend result] $args                ;# good
eval source $fileName                           ;# avoid: $fileName re-parsed
```

**When `{*}` is acceptable:**

- Code that is explicitly TH8-only (gated by `[isTh8]` or
  living in `th8/`-rooted source trees that won't ship to
  Eagle).
- Code that is explicitly Tcl 8.5+-only (e.g. uses other
  Tcl 8.5+ features and already cannot run under Eagle).

In every other case, default to `eval [list ...]` --- the
cross-engine corpus is the canonical reference, and it
predates `{*}` by enough years that the idiom is settled.

**TH8 custom expansion operators** of the form `{tag}value`
are a separate TH8-only divergence (see §20.9).  They are
also non-portable; the same gating rules apply.

Reference: Tcl Language Standard §5.7
(`R-12647-27646`, `R-49538-15893`); §8.7 below for the
canonical `eval` / `uplevel` patterns.

---

## 8. Control-flow idioms

### 8.1 `if` always uses `then`

```tcl
if {$x > 0} then {
  ...
} elseif {$x < 0} then {
  ...
} else {
  ...
}
```

(See §4.4.)

### 8.2 `expr` is always braced

```tcl
return [expr {$index != -1}]
set count [expr {[llength $items] + $extra}]
if {[expr {$x > 0 && $y < 100}]} then { ... }
```

The braces:
- Defer substitution to the expression engine (faster, safer).
- Prevent double substitution / injection bugs.
- Match every example in the corpus — there are **zero** unbraced
  `expr` calls.

### 8.3 `string equal` / `string compare` / `eq` / `ne`

- Inside `expr {…}`: use `eq` and `ne`:
  ```tcl
  if {[expr {$engine eq "Tcl"}]} then { ... }
  ```
  Reference: `Eagle/lib/Eagle1.0/platform.eagle:99`.
- Outside `expr`: use `string equal` (default case-sensitive):
  ```tcl
  if {[string equal -nocase $engine "eagle"]} then { ... }
  ```
- For ordered comparison: `string compare`:
  ```tcl
  if {[string compare -nocase eagle $engine] == 0} then { ... }
  ```

### 8.4 Glob matching: `string match`

`[string match]` evaluates a Tcl glob pattern against a
string and returns 1 on match, 0 otherwise.  Use it for
option-name matching, command-name prefixing, and simple
wildcard tests.  Reach for `[regexp]` / `[regsub]` (§18.6)
when the pattern needs alternation, captures, or
quantifiers.

```tcl
if {[string match "tcl*" $cmd]} then { ... }
if {[string match -nocase "*.eagle" $name]} then { ... }
```

**Pattern characters:**

| Pattern   | Matches                                        |
|-----------|------------------------------------------------|
| `*`       | zero or more characters of any kind            |
| `?`       | exactly one character                          |
| `[abc]`   | any one of `a`, `b`, `c`                       |
| `[a-z]`   | any one character in the range `a` through `z` |
| `\X`      | the literal character `X` (escapes the above)  |

`*`, `?`, and `[...]` do **not** anchor — `string match`
matches the *entire* string, not a substring.  To test a
prefix, use `*` at the end (`tcl*`); for "contains", wrap
both ends (`*foo*`).

**Rules:**

- Quote the pattern with `"..."` or `{...}` — do not
  inadvertently let Tcl substitute `$` or `[ ]` inside the
  pattern.  `{*.tcl}` is safer than `"*.tcl"` when the
  pattern is literal; the difference matters when the
  pattern contains `$` or `[`.
- Use `-nocase` for case-insensitive comparison; do **not**
  pre-`string tolower` both sides to fake it.  `-nocase`
  uses Unicode-aware case folding and is faster.
- For *single-character* equality, prefer `eq` /
  `string equal` (§8.3) over `string match`.  Glob
  metacharacter handling has overhead that doesn't pay off
  for `[string match $x $y]` when neither side has
  wildcards.
- The pattern is a glob, **not** a shell glob.  No brace
  expansion (`{a,b}`), no `**` recursive directory match,
  no leading `.` exemption.  For filesystem listings use
  `[glob]` (which is shell-flavoured).
- For matching against a list of patterns, use a `[switch
  -glob]` or a `[foreach pat $patterns]` loop.  There is no
  "match any of these patterns" built-in.
- Bracket character classes (`[abc]`, `[a-z]`) are
  **literal byte ranges**, not Unicode-aware.  For
  Unicode-aware character-class testing use
  `string is X -strict` (§7.6) or `[regexp]` with
  `[:alpha:]`-style classes.
- Do not use `string match` to validate input strictly —
  the pattern language is too loose to express most
  validation rules.  Validate with `[regexp]` against a
  fully-anchored expression.

### 8.5 Existence checks: `info exists`, never `[catch {set …}]`

```tcl
if {[info exists ::tcl_platform(engine)]} then {
  set engine $::tcl_platform(engine)
}
```

`catch` is for **trying things that may fail**, not for testing
existence of variables. The corpus uses `info exists` exclusively
for existence.

### 8.6 Error handling: `catch`, `error`, never `return -code error`

```tcl
if {[catch {risky} result]} then {
  # result holds the error message
  error [appendArgs "risky failed: " $result]
}
```

- **`catch { … } result`** — the standard form. Test the result
  (the catch's exit code) directly: `if {[catch …]} then {…}`.
- **`error msg`** — raise an error. The `error` command is
  preferred over `return -code error msg`; the corpus uses
  `error` ~exclusively.
- For success-paths from a catch, the explicit form
  `if {[catch …] == 0} then {…}` is clearer than `if {![catch …]}`
  — the corpus uses both, but `== 0` is more common when the
  semantics is "ran without error".

### 8.7 `eval` and `uplevel`

- **`eval cmd $args`** — execute a dynamically composed command
  in the **current** scope. Ubiquitous in `appendArgs`-style
  patterns:
  ```tcl
  set result [list]; eval lappend result $args
  ```
- **`uplevel 1 cmd`** — execute in the **caller's** scope; used
  for wrappers (e.g. test wrappers, transactional wrappers).
- **`uplevel #0 cmd`** — execute at the **global** scope; used to
  source files in the global context regardless of the current
  namespace:
  ```tcl
  uplevel #0 [list source $fileName]
  ```
  Reference: `scratch/eagle/startup/startup.eagle:29-30`.

### 8.8 `switch -exact -- $value { … }`

Always specify the matching mode explicitly (`-exact`,
`-glob`, `-regexp`) and use `--` to terminate options before
the value (defends against values starting with `-`):

```tcl
switch -exact -- [$memberInfo MemberType] {
  Field    { return [$memberInfo FieldType.AssemblyQualifiedName] }
  Method   { return [$memberInfo ReturnType.AssemblyQualifiedName] }
  Property { return [$memberInfo PropertyType.AssemblyQualifiedName] }
  default  { return "" }
}
```

(`Eagle/lib/Eagle1.0/object.eagle:100-113`)

Each case body opens with `{` on the case line and closes with
`}` on its own line. Indent case bodies one level (+2 spaces).

### 8.9 Coroutines: `coroutine name body` + `yield ?value?`

Available in Tcl 8.6+ and TH8 (gated by the `coroutine` test
constraint).  A `coroutine` invocation creates a new command
named `name`; the body proc is started immediately, and its
first `yield` value is returned by the original `coroutine`
call.  Subsequent invocations of `name` resume the body with
the argument as the resume value (returned by the in-flight
`yield`).  When the body returns normally, `name` is auto-
deleted.

```tcl
proc producer {} {
  yield "first"           ;# returned by [coroutine producer …]
  yield "second"          ;# returned by next [producer]
  return "done"           ;# returned by next [producer]; coro deletes
}
set v1 [coroutine c producer]   ;# "first"
set v2 [c]                       ;# "second"
set v3 [c]                       ;# "done"
```

(`tests/coroutine.tcl:25-65`.)

**Rules:**

- Always gate coroutine tests with `-constraints { coroutine }`
  (§10.5).  Eagle does not implement them; tests must skip
  cleanly there.
- The body proc and the coroutine command are **distinct
  commands**.  Use a private name for the body proc (a
  leading `_` prefix is conventional in tests, e.g.
  `_coro_body`) so that renaming the body doesn't clobber an
  unrelated public command.
- Cleanup: in `-cleanup { … }`, run **two** renames — one
  for the body proc, one for the coroutine command — each
  under `catch` because the coroutine may have auto-deleted
  itself if it ran to completion:
  ```tcl
  } -cleanup {
    catch {rename _coro_body ""}
    catch {rename _coro      ""}
  }
  ```
- Pass arguments into the coroutine body via the resume value:
  ```tcl
  proc echoer {} {
    while 1 { puts [yield] }
  }
  coroutine c echoer
  c "hello"   ;# yield returns "hello"; body puts it
  c "world"
  ```
- Do NOT call `yield` outside a coroutine — Tcl 8.6 raises an
  error; some implementations may segfault under interpreter
  shutdown.  When in doubt, gate `yield` with an `[info
  coroutine]` check.
- A coroutine body MAY call other procs that internally
  `yield`; the yield propagates through the call stack.  This
  is how generator helpers compose.

### 8.10 Event loop: `[update]` and `[vwait]`

TH8 (and Tcl 8.x) support a minimal event loop driven by an
embedder-managed event queue.  Scripts CANNOT enqueue events
directly — that's a deliberate security boundary.  The
embedder injects work via the public C API `Th8_QueueEvent`
(callable from any thread); scripts can only **drain**
(`[update]`) or **wait** (`[vwait]`).

**Drain pending events:**

```tcl
update                ;# drain everything currently queued
update -limit 5       ;# at most 5 callbacks, then return
```

`[update]` returns the empty string when the queue is empty
or the limit is reached.  Errors propagate (an event
callback's error becomes `[update]`'s error).

**Wait on a variable until signaled:**

```tcl
vwait done                       ;# wait forever
vwait -timeout 5000 done         ;# timeout after 5 seconds
vwait -timeout 5000 myArr(key)   ;# array-element wait
```

A variable is **signaled** when it is **created**, **changed**,
or **unset**.  Each `[vwait]` captures a fresh starting state,
so re-waiting on the same variable after a successful return
correctly waits for the **next** signal.

On `-timeout` expiry, `[vwait]` raises a script error with a
message of the form `vwait: timeout`; on success it returns
the empty string.

**Cancellation:** both `[update]` and `[vwait]` poll
`Th8_Ready` between events (and immediately before each
underlying wait), so a `Th8_CancelEval` from another thread
surfaces as a script error within ~50 ms.

**Rules:**

- The queue is per-interp.  Events queued on one interp can
  only be drained on that interp's owning thread.
- Drained callbacks run on the calling thread synchronously.
  A long-running callback delays the rest of the queue —
  use `[update -limit N]` to bound work per call.
- Embedders MUST quiesce all worker threads (no in-flight
  `Th8_QueueEvent` calls) before deleting the interp; the
  pState `nDeleted` flag is a backstop, not a substitute.
- Nested `[vwait]` inside an event callback is permitted.
  An inner wait drains the same queue as its outer.
- `[vwait]` requires the platform's threading + event-handle
  callbacks (`xMutex*` + `xEvent*`) to be wired.  When
  they're missing, `[update]` and `[vwait]` error with
  `event queue not available: platform threading
  primitives not configured`.

**Anti-pattern: busy-wait via `[update]`.**

```tcl
# BAD: spins burning CPU while waiting for [done].
while {![info exists done]} { update }

# GOOD: blocks in xEventWait until signaled or timeout.
vwait -timeout 5000 done
```

---

## 9. Variable & data-structure idioms

### 9.1 Empty-list initialization

```tcl
set result [list]                       # preferred
set result {}                           # acceptable; less self-documenting
```

`[list]` is preferred — it documents intent.

### 9.2 Multi-value assignment

- **`set var value`** for single assignment.
- **`foreach {a b c} $list { … }`** for unpacking known-shape
  lists at the start of an iteration.
- **`lassign $list a b c`** is **less common** in Eagle but
  acceptable when destructuring outside a loop.

### 9.3 `array set` / `array get` / `array names`

Standard forms:

```tcl
array set x [list a 1 b 2 c 3]
foreach name [lsort [array names x]] {
  puts "$name = $x($name)"
}
set asList [array get x]
```

### 9.3.1 Iterating very large arrays: `array startsearch` / `nextelement` / `anymore` / `donesearch`

`[array names $a]` materialises the entire key list as one Tcl
list value.  For arrays with millions of elements that allocation
dominates iteration cost.  The classical search-iteration API
walks the underlying hash without materialising the key list:

```tcl
set sid [array startsearch big]
while {[array anymore big $sid]} {
  set k [array nextelement big $sid]
  process $k $big($k)
}
array donesearch big $sid
```

**Rules:**

- The search ID returned by `array startsearch` is opaque — do
  not parse, compare, or fabricate it.  It is valid only against
  the array it was opened on, in the interp it was created in.
- **Any element-level write to the array invalidates the
  search.**  Adding an element, removing one, or modifying
  the value of an existing element (`set`, `append`,
  `lappend`, `incr`, `unset`, `array set`) all bump the
  array's epoch; the next `nextelement` / `anymore` after
  the write raises an error.  Read-only access (`array
  names`, `array get`, `info exists arr(k)`, `set arr(k)`
  with no value) does NOT invalidate.  This differs from
  Tcl 8.x, which allows post-startsearch additions to "leak"
  into the iteration AND silently permits in-place value
  updates while iterating.  Capture all keys with `[array
  names]` first if you need to mutate during the walk —
  the classic iterate-and-update idiom does NOT work under
  this API.
- `array unset $a` (the whole array) also invalidates every
  open search against that array.
- `array donesearch` is mandatory in `-cleanup { … }` blocks
  even on the error path — the search holds a reference into
  the per-interp search registry until released.  Tests can
  enforce zero-leak discipline with a dedicated leak-check
  test at end of file body:
  ```tcl
  runTest {test myfeat-zz.zz {
    no array searches leaked across this file's tests
  } -body {
    ::th8testlib::array_searches
  } -result {}}
  ```
  The test command returns a flat list of every pending
  `{arrayName searchId}` pair in the current interp, sorted
  by array name then search id.
- For arrays with **fewer than ~10 000 elements**, prefer
  `[array names]` — the snapshot is cheap and the code is
  shorter.  The search API is for the cases where the snapshot
  itself is the cost you are trying to avoid.
- Never nest `array startsearch` calls inside a long-running
  callback that may invoke user script — user script can mutate
  the array and invalidate your search.  Hold the search across
  pure leaf code only.

### 9.3.2 `array statistics` for diagnostics

`[array statistics $name]` returns a human-readable string
describing the underlying hash distribution (entry count, bucket
count, longest probe sequence, etc.).  It is **not** parseable
output — its format is implementation-defined and intended for
diagnostics, log scrapes, and bug reports.  Do not regex-match
the contents in production code.

```tcl
proc dumpHashHealth { arrayName } {
  if {![array exists $arrayName]} then { return "" }
  return [array statistics $arrayName]
}
```

### 9.4 Globals: `global` declaration + `$::name`

Inside a proc, declare globals at the top with `global env x y`,
or address them through the global qualifier `$::env(NAME)`,
`$::tcl_platform(engine)`. The qualifier form is preferred for
**read-only** access to a few globals; the `global` declaration
is preferred when the proc reads/writes multiple globals.

```tcl
proc getEnvironmentVariable { name } {
  global env
  return [expr {[info exists env($name)] ? $env($name) : ""}]
}
```

(`Eagle/lib/Eagle1.0/auxiliary.eagle:27-31`)

### 9.5 Default proc arguments

```tcl
proc getDictionaryValue { dictionary name {default ""} {wrap ""} } { ... }
```

The default value is in `{ … }` even when it is an empty
string `""`. Required positional args come first; defaulted args
come after.

### 9.6 `upvar` — passing variables by name

A proc receives values, not references.  To read or write a
caller's variable, take the **name** as a parameter and link it
locally with `upvar`:

```tcl
proc addOne { varName } {
  upvar 1 $varName v
  set v [expr {$v + 1}]
}

set x 5
addOne x   ;# x is now 6
```

(`tests/variable.tcl:87-102`.)

**Rules:**

- The level argument is **mandatory** in TH8.  Always write
  `upvar 1 $varName v`, not `upvar $varName v`.  The explicit
  `1` makes the call-frame relationship visible at the call
  site and matches the pattern used everywhere in the test
  suite.  Numeric levels (`upvar 2 …`) reach further up the
  call stack; absolute levels use `#` (`upvar #0 …` for the
  global frame).
- The local alias name (`v` above) is conventionally short —
  one or two letters — because it is a binding, not a fresh
  variable.  Longer names (`localValue`) read like
  shadow-state and should be avoided.
- `upvar` works for **scalar variables, array elements, and
  whole arrays**.  An array element is linked the same way as
  a scalar:
  ```tcl
  proc setElem { varName value } {
    upvar 1 $varName v
    set v $value
  }
  setElem arr(key) "new"
  ```
  A whole array is linked by passing the bare array name —
  the local alias then behaves like an array.
- `upvar` to a **non-existent variable** is legal and creates
  the variable in the caller on first write:
  ```tcl
  proc createVar { varName value } {
    upvar 1 $varName v
    set v $value     ;# creates the caller's variable
  }
  createVar newvar "created"   ;# newvar now exists in caller
  ```
  This is the standard pattern for "out-parameter" procs
  (e.g. `[scan]`-style helpers that fill in named result
  variables).
- A bad level (e.g. `upvar 99 …` from frame depth 1) raises a
  script error.  Never trust untrusted input as the level
  argument.
- `upvar` is **not** the same as `[global]`.  `[global x]`
  attaches a *global* variable to the local frame; `upvar`
  attaches a variable in a *specified* frame.  Use `[global]`
  for genuine globals; reserve `upvar` for caller-frame
  reach-through.
- Do not use `upvar` to fake "pass by reference" when a return
  value would do.  Returning a new value composes; mutating
  the caller's frame surprises readers.  The legitimate uses
  are: out-parameters where multiple values must escape, and
  iteration helpers (`foreachLine`, `foreachKey`) that hand
  the body a binding rather than a copy.

### 9.7 `lassign` for fixed-shape destructuring

`lassign $list a b c` destructures the first three list
elements into named variables, returning any leftover
elements as a flat list.  Missing elements are bound to the
empty string.

```tcl
set parts [split $line :]
lassign $parts user host port
if {$port eq ""} then { set port 22 }
```

**Rules:**

- Use `[lassign]` when the list shape is fixed and the
  variables would otherwise be filled by repeated `[lindex]`
  calls.  It reads better and avoids re-traversing the list.
- For known-shape lists at the start of a `[foreach]` body,
  prefer the destructuring form
  `foreach { a b c } $list { … }` — it's idiomatic and avoids
  an extra command per iteration.
- The leftover-element return value of `[lassign]` is rarely
  useful in style-conformant code.  When the list is shorter
  than the variable list, expect empty strings and check for
  them explicitly — do not rely on "missing" being
  distinguishable from "empty".
- Eagle accepts `[lassign]`; both engines produce identical
  results for the same input.  No engine guard is required.

---

## 10. Test-framework conventions

### 10.1 The `runTest` wrapper

All tests are wrapped in `runTest { test … }`:

```tcl
runTest {test array-1.1 {array set} -setup {
  unset -nocomplain x
} -body {
  array set x [list a 1 b 2 c 3]
} -result {}}
```

(`Eagle/Library/Tests/array.eagle:20-24`)

The `runTest` wrapper exists in:

- `Eagle/lib/Eagle1.0/test.eagle` — the canonical implementation.
- `th8/lib/Standard1.0/test.tcl` — the TH8 reimplementation,
  designed to run the **same** test scripts under both Tcl and
  Eagle.

**Rules:**

- The argument to `runTest` is a single brace-delimited string
  containing a complete `test` invocation.
- The closing `}` of the `runTest` argument is on the same line
  as the closing `}` of the `-result` value: `... -result {…}}`.

### 10.2 Test invocation form

```tcl
runTest {test NAME {DESCRIPTION
  optional multi-line desc with reason / R-marker
} -setup {
  setup body
} -body {
  body
} -cleanup {
  cleanup body
} -constraints { CONSTRAINTS } -result { RESULT }}
```

The conventional order of arg pairs after the description is: `-setup`, `-body`,
`-cleanup`, `-constraints`, `-match`, `-returnCodes`, `-result`.
This is a convention, not a hard rule — a test MAY place a pair
elsewhere with a good reason (e.g. a constraints-focused example
that leads with `-constraints`).
Not all are required; omit what does not apply.

### 10.3 `-body { … }` content

Standard 2-space indent:

```tcl
} -body {
  set x "hello"
  append x " world"
  set x
} ...
```

### 10.4 `-result { … }` content

For simple results:

```tcl
} -result {hello world}}
```

For complex results (with embedded braces, lists, errors), use
braces to avoid double substitution:

```tcl
} -result {1 {this is an error.}}}
```

(`Eagle/Library/Tests/catch.eagle:24`)

For long results, use `\` continuation **inside the braces**,
flush-left:

```tcl
} -result {0x1 0x2 0x3 0x4 0x5\
0x6 0x7 0x8 0x9 0xa\
...}}
```

### 10.5 `-constraints { list }`

A space-separated list of constraint names. Common names:

- **Engine:** `tcl`, `eagle`, `tcl84`, `tcl85`, `tcl86`,
  `tcl85Feature`, `eagleFeature`.
- **Platform:** `windows`, `unix`, `linux`, `macos`.
- **Mono:** `mono`, `monoBug`, `monoToDo`.
- **TH8 (project-specific):** `th8` — gates TH8-only tests; see
  `th8/lib/Standard1.0/test.tcl`.

```tcl
} -constraints {tcl tcl84} -result {...}}
} -constraints {eagle SQLite file_test.exe} -result {...}}
```

#### 10.5.1 TH8-specific constraint categories

`lib/Standard1.0/test.tcl` registers a large set of
constraints for the TH8 test corpus.  Group them mentally
into the categories below; combine multiple constraints in
the same `-constraints { ... }` list when a test needs more
than one to be true.

**Engine / language identity.**

| Constraint | True when                                              |
|------------|--------------------------------------------------------|
| `tcl`      | always (every interp implements Tcl)                   |
| `eagle`    | running under Eagle                                    |
| `th8`      | running under TH8                                      |
| `standard` | always (Tcl Language Standard v1 baseline)             |
| `tip440`   | `::tcl_platform(engine)` exists (TIP-440 introspection)|

**Crypto / signing policy** (the most error-prone group --- get this wrong and tests pass for the wrong reason):

| Constraint        | Meaning                                                                                         |
|-------------------|-------------------------------------------------------------------------------------------------|
| `crypto`          | The interp links a working crypto backend (key APIs callable).                                  |
| `crypto_testlib`  | `::th8testlib::key_token` works --- the test C library is loaded and exposes key introspection. |
| `crypto_disabled` | `::th8testlib::signed_only query` returns `0` (or errors) --- the policy is OFF.                |
| `crypto_enabled`  | `::th8testlib::signed_only query` returns `1` --- the policy is ON.                             |

The `crypto_disabled` / `crypto_enabled` pair is **mutually
exclusive** and exactly one is true on any given build.
A test that exercises behaviour gated by the signing
policy MUST gate on the appropriate one --- never assume
the build state.

**Test-only injection / sandboxing:**

| Constraint        | Meaning                                                          |
|-------------------|------------------------------------------------------------------|
| `fault_injection` | `::th8testlib::fault` is callable.                               |
| `secure_persist`  | The persistent-secure-variable extension is present.             |
| `kv_sqlite`       | `kv` command is wired to SQLite (vs the in-memory back-end).     |

**Optional commands (presence probes).**  Constraints
named after a command (`json`, `regexp`, `regsub`, `scan`,
`coroutine`, `nproc`, `napply`, `concat`, `close`, …) are
true iff `[info commands $name]` is non-empty.  Use these
when a test exercises a command that may be compiled out
in stripped builds.

**Pattern:**

```tcl
runTest {test crypto-7.4 {
  R-NNNNN-NNNNN: secure variable round-trip under signed-only
} -constraints {th8 crypto_enabled crypto_testlib} -setup {
  unset -nocomplain token
} -body {
  secure create token "abc"
  set token
} -cleanup {
  if {[info exists token] && [secure exists token]} then {
    secure delete token
  }
  unset -nocomplain token
} -result {abc}}
```

The constraint list is short, declarative, and order-
independent.  A new reader sees `th8 crypto_enabled
crypto_testlib` and immediately knows which build modes
this test runs under.

**Anti-pattern: probing inside `-body`.**

```tcl
# BAD: silently passes on builds without signed_only.
} -body {
  if {![::th8testlib::signed_only query]} then {
    return abc        ;# ← test "passes" without exercising anything
  }
  secure create token "abc"
  set token
} -result {abc}}
```

Encode the build requirement as a `-constraints` entry, not
as an in-body branch.  Skipped tests are visible (the
runner reports them); silently-stubbed tests are not.

### 10.6 `-setup` and `-cleanup`

Both blocks are optional; use them whenever a test mutates
shared state:

```tcl
runTest {test append-1.1 {append-1.1 description} -setup {
  unset -nocomplain x
} -body {
  set x "hello"
  append x " world"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello world}}
```

**Rules:**

- `unset -nocomplain` (with the flag) is **mandatory** for
  cleanup unsets — the variable may not exist if the body
  errored.
- Cleanup unsets every variable the body created.
- Cleanup runs even on failure.

### 10.7 Test description content

The description (the brace-string immediately after the test
name) is a short noun phrase. Multi-line descriptions are
permitted when there is a reason / requirement marker / ticket
reference to record:

```tcl
runTest {test append-1.1 {
  R-06409-32483: append to existing variable
} -setup {
  ...
```

(TH8 form; references the standard's R-marker — see §20.)

### 10.8 `-match regexp` and `-returnCodes`

- **`-match regexp`** — match the result against a regex
  pattern (used when the body produces output with
  environment-dependent parts).
- **`-returnCodes 1`** — assert the body raises an error;
  `-result` is then the expected error text.

```tcl
runTest {test basic-1.0.1 {::tcl_platform(engine) for Tcl} -body {
  expr {[info exists ::tcl_platform(engine)] ? $::tcl_platform(engine) : ""}
} -constraints {tcl} -match regexp -result {^|Tcl$}}
```

```tcl
runTest {test basic-1.2 {unbalanced brace in source file} -setup {
  unset -nocomplain a
} -body {
  source [file join $test_data_path unbalanced_brace.eagle]
} -cleanup {
  unset -nocomplain a
} -constraints {file_unbalanced_brace.eagle} -returnCodes 1 -result \
{missing close-brace: possible unbalanced brace in comment}}
```

References: `Eagle/Library/Tests/basic.eagle:23-28`, `:51-58`.

### 10.8.1 Regex `-result` for floating-point output

Conformance tests that exercise math (sqrt, sin, log, etc.)
MUST use `-match regexp` rather than literal `-result` strings.
Different libcs and compiler flag combinations produce slightly
different last-digit values; a literal match would make the test
suite fragile across platforms.

The project's canonical regex for an IEEE-754 double is:

```regexp
^[-+]?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?$
```

Wrapped at the test level:

```tcl
runTest {test math-sqrt-1.1 {
  R-NNNNN-NNNNN: sqrt(2.0) returns a finite double
} -body {
  expr {sqrt(2.0)}
} -match regexp \
  -result {^[-+]?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?$}}
```

**Rules:**

- Use the regex form for ANY result that crosses through a libm
  function or otherwise depends on FP rounding mode.  Even simple
  cases like `expr {1.0 / 3.0}` produce different trailing digits
  on different platforms.
- The regex matches finite doubles only.  For tests that should
  produce `Inf` or `NaN`, use a literal result (those tokens are
  spelled the same way across platforms).
- Anchor with `^` and `$` so the match doesn't leak through to
  surrounding text (e.g. when the result is part of a list).
- Do NOT use `string is double -strict` as a substitute — it
  accepts hex floats, integers, and other forms the literal
  output won't include.  Match the *output format*, not the
  *value type*.

The conformance suite's intent is that an implementation may
produce any well-formed double result; the regex is the contract,
not the bit pattern.

### 10.9 Named accessors for sandbox / fault-injection results

When the body of a test invokes a list-returning helper whose
positional layout is part of an internal contract (`[sandbox]`
returns `{rc result steps allocCount}`; `[fault]` returns
`{rc result allocCount triggered}`), use the **named accessor
procs** defined in `lib/Standard1.0/test.tcl` rather than bare
`[lindex $result N]`:

```tcl
# GOOD: self-documenting; insulated from layout changes.
} -body {
  set r [::th8testlib::fault someExpression]
  list [faultRc $r] [faultResult $r]
} -result {0 42}

# AVOID: positional indices are opaque and brittle.
} -body {
  set r [::th8testlib::fault someExpression]
  list [lindex $r 0] [lindex $r 1]
} -result {0 42}
```

(`th8/tests/fault/fault1.tcl:34, :48-49, :65, :85-86, :118`)

The accessor families:

| Helper / result shape                            | Accessor procs                                              |
|--------------------------------------------------|-------------------------------------------------------------|
| `sandbox` &rarr; `{rc result steps allocCount}`     | `sandboxRc`, `sandboxResult`, `sandboxSteps`, `sandboxAllocCount` |
| `fault` &rarr; `{rc result allocCount triggered}`   | `faultRc`, `faultResult`, `faultAllocCount`, `faultTriggered` |

(`th8/lib/Standard1.0/test.tcl:1220-1264`)

Both families exist for the same reason: keep tests readable
and resilient to changes in the result layout.  If a fifth
field becomes meaningful, only the accessor procs need updating
--- no test rewrites.  When you write a new helper in this
style, provide the named accessors at the same time and document
the positional contract in their docstring.

### 10.10 Test-data files via `$test_data_path`

When a test needs an external data file (e.g. a script to be
sourced via the `source` command, an unbalanced-brace fixture,
a binary blob to read), place it under `tests/data/` and
resolve its path inside the test body via the standard idiom:

```tcl
} -body {
  source [file join $test_data_path unbalanced_brace.eagle]
} ...
```

(`Eagle/Library/Tests/basic.eagle:51-58`)

The variable `$test_data_path` is set by `prologue.tcl` to the
absolute path of the test-data directory and is available
inside every test `-body`.  Never hard-code a relative path
(`tests/data/file`) --- a test runner that changes the current
directory will break.  Never hard-code an absolute path ---
that breaks portability across checkout locations.

If the data file is itself a script that must be signed
(see §20.7), the same `tools/signScript.sh` workflow applies;
sourced data scripts pass through the signing-policy gate just
like any other script load.

### 10.11 Resource-leak detection

The `runTest` harness in `lib/Standard1.0/test.tcl` snapshots
a number of process-scoped resource lists before each test,
takes a second snapshot after, and reports any non-empty
diff under a `MUTATED <category>` line in the test output.
A `MUTATED` line counts the test as one of the run's
`Mutated:` totals (separate from `Failed:`) — the run can
end with `0 Failed:` and still flag a leaker.

The full list of monitored categories:

| Category | Probe | Notes |
|---|---|---|
| `loaded` | `[info loaded]` | dynamic-library loads |
| `procedures` | `[info procs]` | top-level procs |
| `variables` | `[info globals]` | global variables |
| `commands` | `[info commands]` | command names |
| `namespaces` | `[namespace children ::]` | top-level namespaces |
| `channels` | `[file channels]` | open channels |
| `packages` | `[package names]` | loaded packages |
| `functions` | `[info functions]` | math functions |
| `expansions` | `[info expansions]` | TH8-only |
| `breakpoints` | `[info breakpoints]` | TH8-only |
| `arraySearches` | `[::th8testlib::array_searches]` | TH8 + testlib |

Tests that allocate one of these resources MUST release it
in `-cleanup { … }` even on the error path; otherwise the
harness flags the test as having leaked.

Test authors **do not** have to add per-file leak-check
tests — the harness covers every test automatically.  The
job in test code is just to clean up:

```tcl
runTest {test myfeat-1.1 {
  ...
} -setup {
  set sid [array startsearch a]
} -body {
  ...
} -cleanup {
  catch {array donesearch a $sid}     ;# release the search
  unset -nocomplain a sid
} -result ...}
```

Note the `catch` around `donesearch` — the test body may
have invalidated the search (e.g. by mutating the array),
in which case `donesearch` itself errors.  The cleanup must
not let that propagate; the goal is to release whatever is
still releasable and let the harness report the rest.

When extending the harness with a new resource, follow the
existing snapshot-and-diff pattern in `runTest` rather than
adding per-test or per-file boilerplate.

### 10.12 Run summary at end of suite

In addition to the per-file `STATUS:` line and the cumulative
`Files: / Total: / Passed: / Failed: / Mutated: / Skipped:`
totals, the suite driver appends an itemized block for any
non-success outcome.  Each block names every individual test
that contributed to the count, with the reason or category:

```
============================================
Tcl Language Standard Conformance Test Suite
Tcl Language Implementation: TH8 v1.0.0

	Files:		109
	Total:		2870
	Passed:		2863
	Failed:		0
	Mutated:	0
	Skipped:	7

SKIPPED:
	constraint overflow_check coverage3-8.1
	constraint crypto_disabled crypto-1.2a
	...
OVERALL STATUS: SUCCESS
============================================
```

Three blocks are emitted when their counts are non-zero:

| Block | Per-line content |
|-------|------------------|
| `FAILED:` | failing test ID + the `-result` mismatch summary |
| `MUTATED:` | mutating test ID + the leaked-resource category |
| `SKIPPED:` | skipped test ID + the constraint name that failed |

The intent is **debug from the tail alone**: a reader can
`tail -50` the run output and immediately see which tests
need attention, without scrolling thousands of `++++ PASSED`
lines.  When you add new constraints (§10.5.1) or new
resource categories (§10.11), the harness picks them up
automatically — no per-test wiring required.

### 10.13 Save and restore interpreter/engine flags; leave state pristine

A test MUST leave the interpreter exactly as it found it. When a test needs an
interpreter or engine flag toggled, **capture the prior value and restore that
exact value** in cleanup — never force a fixed `true`/`false` in `-setup` /
`-cleanup`, which clobbers whatever state the suite was actually in.

```tcl
-setup {
  #
  # NOTE: Save the current interpreter flags; enable only what this test
  #       needs.  Cleanup restores the SAVED value, not a hard-coded one.
  #
  set savedFlags [enableInterpreterFlags]
  # ... enable this test's flag(s) ...
} -body {
  # ... exercise the behavior under the flag ...
} -cleanup {
  enableInterpreterFlags $savedFlags
  unset -nocomplain savedFlags
}
```

Self-cleaning rules (a test is a first-class deliverable — §5.7, §10):

- **Unset every variable** the test created, including helper variables such as
  `savedFlags` and loop/scratch vars; `rename` away any proc it defined.
- **Restore every piece of state** it touched — flags, channels, `env`, cwd,
  registered commands — back to the pre-test value.
- **Release every resource.** The suite reports `MUTATED:` for anything left open
  (§10.11); a leak is a test failure, not a warning.

This is a direct consequence of Eagle's strict semantics (§23): state is not
magically reset between tests — the test owns its own teardown.

---

## 11. Eagle / .NET-specific patterns

The patterns in this section span three engines with different
.NET reach:

| Pattern              | Tcl 8.x | Eagle | TH8 |
|----------------------|:-:|:-:|:-:|
| §11.1 Cross-engine guards | yes | yes | yes |
| §11.2 `object invoke` (.NET reflection) | no | **yes** | no |
| §11.3 `BEGIN Eagle ONLY` blocks | yes (no-op) | yes | yes (no-op) |
| §11.4 `compileCSharpWith` | no | **yes** | no |

§11.2 and §11.4 are **Eagle-only**: they depend on Eagle's CLR
gateway (`[object]`) and its in-process C# compiler, neither of
which exists in canonical Tcl or TH8.  Scripts that must run
under TH8 (or any non-Eagle interpreter) MUST guard those
patterns with `[isEagle]` and provide a fallback path or fail
cleanly.  The §11.1 / §11.3 patterns work everywhere; they are
the canonical way to mark and gate the Eagle-only code so the
rest of the script remains portable.

When a script targets only Eagle (no TH8 or stock-Tcl
deployment), the `[isEagle]` guard is still required so that
accidentally running the file under another engine raises a
clean error rather than producing a stack trace at the first
`[object]` call.

### 11.1 Cross-engine guards: `[isEagle]`, `[isMono]`, `[isDotNetCore]`

Code that runs under both Tcl and Eagle uses runtime guards:

```tcl
if {[isEagle]} then {
  if {[isMono] || [isDotNetCore]} then {
    return [lindex [info assembly true] 1]
  } else {
    return [info nameofexecutable]
  }
} else {
  return [info nameofexecutable]    ;# native Tcl
}
```

(`Eagle/lib/Eagle1.0/exec.eagle:32-40`)

`isEagle` is defined as:

```tcl
proc isEagle {} {
  return [expr {[info exists ::tcl_platform(engine)] && \
      [string compare -nocase eagle $::tcl_platform(engine)] == 0}]
}
```

(`Eagle/lib/Eagle1.0/init.eagle:33-42`)

### 11.2 `object invoke ClassName Method args`

```tcl
set savedEncoding [object invoke Console OutputEncoding]
set encoding [object invoke System.Text.Encoding GetEncoding utf-8]
object invoke Console OutputEncoding $encoding
```

Use full namespace-qualified type names. The `object` command is
Eagle's gateway into the .NET CLR.

### 11.3 `BEGIN Eagle ONLY` / `END Eagle ONLY` blocks

When a library file mixes engine-shared and Eagle-only code,
mark the Eagle-only sections explicitly:

```tcl
if {[isEagle]} then {
  #############################################################################
  ############################# BEGIN Eagle ONLY ##############################
  #############################################################################

  proc getSQLiteDefineConstantPrefix {} { ... }
  ...

  #############################################################################
  ############################## END Eagle ONLY ###############################
  #############################################################################
}
```

(`sqlite/dotnet/lib/System.Data.SQLite/common.eagle:16-19`)

### 11.4 `compileCSharpWith [subst { … }]`

Embedded C# is composed via `subst` and compiled at test time:

```tcl
set code [compileCSharpWith [subst {
  using System;
  using System.Data.SQLite;

  namespace _Dynamic${id}
  {
    \[SQLiteFunction(Name = "sumint")\]
    public class Test${id} : SQLiteFunction { ... }
  }
}] true true true results errors]
```

(`sqlite/dotnet/Tests/function.eagle:34+`)

`\[`/`\]` escapes `[`/`]` inside the templated body so they
reach C# verbatim. `${id}` substitutes a unique-per-run
identifier.

---

## 12. Module / file organization

### 12.1 Order of declarations inside `namespace eval`

A library file's contents inside `namespace eval ::Ns { ... }`
follow a fixed order:

1. File-header `# NOTE:` / `# WARNING:` / `# TODO:` block (if any).
2. `variable` declarations (no initializer, or with simple
   default).
3. Helper procs (low-level utilities, called by other procs).
4. Mid-level procs (built on the helpers).
5. Public-API procs (called from outside the namespace).
6. Engine-conditional initialization block
   (`if {[isEagle]} then { ... }` etc.) when needed.
7. `package provide Ns VERSION` — at the very end.

Reference: `Eagle/lib/Eagle1.0/auxiliary.eagle:22-127` shows
the helpers-then-public progression;
`Eagle/lib/Eagle1.0/file1.eagle:72-74` shows the closing
`package provide` line.

### 12.2 Variable declarations at namespace level

Use `variable NAME` (no value) to declare module-private state;
defer the actual initialization to the first proc that needs
it, or to a conditional block at namespace-eval time. This
keeps the file's top-level free of incidental side effects:

```tcl
namespace eval ::Eagle {
  variable baseUri        ; # NOTE: Default value set in helper
  variable unzipUrn       ; # DEFAULT: unzip
  variable unzipUri       ; # DEFAULT: computed lazily
  variable unzipInstalledCommand
  ...
```

(`Eagle/lib/Eagle1.0/unzip.eagle:37-100`)

The trailing `; # NOTE: ...` / `; # DEFAULT: ...` comment on
each `variable` line documents how/where the value is set.

### 12.3 Engine-gating at namespace level

When code must branch for Eagle vs. native Tcl, use
`if {[isEagle]} then { ... } else { ... }` blocks **at namespace
eval time** to define engine-specific helpers or set defaults.
This keeps proc bodies focused on the algorithm rather than on
engine detection:

```tcl
namespace eval ::Eagle {
  if {[isEagle]} then {
    # Eagle-only initialization code
    proc getSQLiteDefineConstantPrefix {} { ... }
    proc getSQLiteCompileOptionPrefix {} { ... }
  }
}
```

(`sqlite/dotnet/lib/System.Data.SQLite/common.eagle:16-19`)

For larger engine-specific sections, mark the boundaries with
`BEGIN Eagle ONLY` / `END Eagle ONLY` divider blocks (see
§11.3).

### 12.4 `package provide` placement

`package provide` lives **at the end** of the namespace eval
block. The version may be conditional:

```tcl
  package provide Eagle.File \
      [expr {[isEagle] ? [info engine PatchLevel] : "1.0"}]
}
```

(`Eagle/lib/Eagle1.0/file1.eagle:72-74`)

`[info engine PatchLevel]` is Eagle-only; the ternary picks a
constant `1.0` for native Tcl so the version comparison
machinery still works.

### 12.5 The `pkgIndex.{eagle,tcl,th8}` pattern

A package's `pkgIndex.*` is the index file the host interpreter
reads to discover what packages a directory provides.  TH8
projects ship up to **three** variants of this file in a single
package directory, one per engine, each with a mutually-
exclusive early-exit guard so only the right one runs:

| File              | Loaded by | Guard logic                                       |
|-------------------|-----------|---------------------------------------------------|
| `pkgIndex.eagle`  | Eagle     | requires Eagle 1.0+; native Tcl/TH8 ignore the file |
| `pkgIndex.tcl`    | Tcl 8.x   | requires Tcl 8.4+; bails if Eagle or TH8 is loaded   |
| `pkgIndex.th8`    | TH8       | requires TH8 1.0+; bails if Eagle is loaded         |

**TH8 variant** (canonical form for the project; excludes Eagle
but allows the TH8-augmented Tcl 8.4 base):

```tcl
###############################################################################
#
# pkgIndex.th8 --
#
# ... header ...
#
###############################################################################

if {![package vsatisfies [package provide Tcl] 8.4]} then {return}
if {[string length [package provide Eagle]] > 0} then {return}
if {![package vsatisfies [package provide TH8] 1.0]} then {return}

package ifneeded th8test 1.0 \
    [list source [file join $dir test.tcl]]
```

(`th8/lib/Standard1.0/pkgIndex.th8:14-16`)

**Tcl variant** (excludes Eagle and TH8 by checking that
neither package is provided):

```tcl
if {![package vsatisfies [package provide Tcl] 8.4]} then {return}
if {[string length [package provide Eagle]] > 0} then {return}
if {[string length [package provide TH8]] > 0} then {return}

package ifneeded th8test 1.0 \
    [list source [file join $dir test.tcl]]
```

(`th8/lib/Standard1.0/pkgIndex.tcl:14-16`)

**Eagle variant** (Eagle's `sourceWithInfo` records package-
provenance metadata; the TH8 codebase has updated its
`pkgIndex.eagle` files to use `then` consistently, but legacy
Eagle pkgIndex files in the wider Eagle corpus may still appear
without `then` --- see the note at the end of this section):

```tcl
if {![package vsatisfies [package provide Tcl] 8.4]} then {return}
if {![package vsatisfies [package provide Eagle] 1.0]} then {return}
if {[string length [package provide TH8]] > 0} then {return}

package ifneeded th8test 1.0 \
    [list sourceWithInfo -library true -- [file join $dir test.tcl]]
```

(`th8/lib/Standard1.0/pkgIndex.eagle:14-16`)

**Rules:**

- Each variant begins with engine-discriminating early-exit
  guards.  The pattern is *positive-allow plus negative-block*:
  require the host package, AND block any *other* engine's
  package presence.  Without this, a single directory's three
  pkgIndex files would all run under TH8 (which provides Tcl
  back-compat) and clobber each other's `package ifneeded`
  registrations.
- `sourceWithInfo -library true --` is the Eagle-aware variant
  of `source` that records package-provenance metadata; the Tcl
  and TH8 variants use plain `source`.
- One `package ifneeded` line per file the package includes.
- `$dir` is provided by the host's `pkg_mkIndex` machinery ---
  never use `[file dirname [info script]]` here.
- The same `package ifneeded NAME VERSION` declarations appear
  in all three files; only the loader command (`source` vs.
  `sourceWithInfo`) varies.  Keep them in sync ---
  `tools/audit_patterns.tcl`-style checkers can flag drift.
- A package that targets only one engine ships only one
  pkgIndex variant.  Cross-engine library packages
  (`Standard1.0` is the canonical example) ship all three.
- **`then` keyword in early-exit guards:** every pkgIndex
  file in the TH8 codebase uses `if {cond} then {return}`
  consistently, matching anti-pattern #3 / §8.1.  Some
  pkgIndex files in the wider Eagle ecosystem (and Tcl's
  default `pkg_mkIndex` output) still use the legacy
  `if {cond} {return}` form without `then`; those files
  pre-date the project's `then`-required convention and are
  expected to be updated over time.  New pkgIndex files
  authored under this style guide MUST use `then`.

---

## 13. Package handling

### 13.1 `package require X` — when and how

For required dependencies, use bare `package require` with no
catch:

```tcl
package require Eagle
package require Eagle.Library
package require Eagle.Test

runTestPrologue
```

(`sqlite/dotnet/Tests/basic.eagle:10-14`)

Version negotiation happens at the `package ifneeded`
registration site (in pkgIndex.eagle), not in the consumer. A
consumer that requires a specific version uses
`package require X 1.0`.

**Engine-naming conventions for package names** mirror the
namespace casing rule in §6.4:

| Engine | Package-name casing | Examples                                    |
|--------|---------------------|---------------------------------------------|
| Eagle  | **PascalCase**      | `Eagle`, `Eagle.Library`, `Eagle.Test`      |
| TH8    | **lowercase**       | `th8`, `th8test`, `th8test_exec`, `th8sqlite3` |
| Tcl    | mixed (per upstream)| `Tcl`, `Tk`, `sqlite3`, `tdom`               |

A TH8 test prologue therefore looks like:

```tcl
package require th8
package require th8test
package require th8test_exec
package require th8test_load
package require th8sqlite3
```

(`th8/tests/prologue.tcl:25-29`)

The leading `package require th8` is **always required** in TH8
scripts that depend on the engine's bootstrap library
(`lib/th8/init.th8`); without it, helpers like `[isTh8]`,
`[isEagle]`, `[appendArgs]`, and `[isWindows]` are not in scope
(see §19.1).

### 13.2 `catch {package require X}` — for optional dependencies

When a package is OPTIONAL (the script has a fall-back path
when it's missing), wrap the require in `catch`:

```tcl
if {[catch {
  package require Garuda
} error]} then {
  # Garuda not available; carry on without it.
}
```

The catch idiom is used **only** when the script genuinely has
a degraded mode without the package. Required packages must
fail loudly.

### 13.3 Conditional loading via `::no(...)` and `::env(...)` flags

A common gating pattern is to skip a package load when either
a `::no(NoX)` array element OR a `NoX` environment variable is
set:

```tcl
if {![info exists ::no(NoStartupRunCommands)] && \
    ![info exists ::env(NoStartupRunCommands)]} then {
  uplevel #0 [list \
      source [file join [file dirname [info script]] tclshrc.tcl]]
}
```

(`scratch/eagle/startup/startup.eagle:27-31`)

The `::no(...)` array is the project-wide opt-out registry;
the parallel `::env(NoX)` lets users disable a feature from the
shell without modifying any script.

---

## 14. Channel I/O patterns

### 14.1 The standard open / configure / use / close pattern

```tcl
proc readFile { fileName } {
  set channel [open $fileName RDONLY]
  makeBinaryChannel $channel
  set result [read $channel]
  close $channel
  return $result
}
```

(`Eagle/lib/Eagle1.0/file1.eagle:35-41`)

**Rules:**

- The mode argument to `open` is the symbolic form (`RDONLY`,
  `WRONLY CREAT APPEND`, etc.), not the legacy
  `r`/`w`/`a`/`r+`/etc. tokens — except in trivial scripts.
  The symbolic form is more readable and matches POSIX
  conventions.
- `fconfigure` is called immediately after `open` via a small
  helper proc (`makeBinaryChannel`, `makeAsciiChannel`,
  `makeLogChannel`) — never inlined in every reader.
- `close` is the LAST line before `return`. There is no
  `try`/`finally` wrapper for the simple read-and-close case;
  see §14.4 for the cleanup-required variant.

### 14.2 The `fconfigure` helper procs

The library defines small per-content-type helpers so every
caller gets the same encoding/translation pair:

```tcl
proc makeBinaryChannel { channel } {
  fconfigure $channel -encoding binary -translation binary; # BINARY DATA
}

proc makeAsciiChannel { channel } {
  fconfigure $channel -encoding ascii -translation auto; # ASCII TEXT
}

proc makeLogChannel { channel } {
  set translation [expr {[isEagle] ? "protocol" : "auto"}]
  fconfigure $channel -encoding binary -translation $translation; # LOG DATA
}
```

(`Eagle/lib/Eagle1.0/file1.eagle:27-29`,
`Eagle/lib/Eagle1.0/file2.eagle:27-29` and `:60-63`)

The trailing `; # BINARY DATA` / `# ASCII TEXT` / `# LOG DATA`
comment is the canonical way to label a channel's role at the
configuration site so a grep for "BINARY DATA" finds every
binary-channel call.

### 14.3 `puts -nonewline` for binary writes

Binary writes use `-nonewline` to prevent translation of an
implicit trailing newline:

```tcl
puts -nonewline $channel $data
```

(`Eagle/lib/Eagle1.0/file1.eagle:51`)

Text writes that intentionally include a newline use plain
`puts $channel $line`.

### 14.4 Eagle-only shared-access flag and explicit `flush`

When two processes may write the same log file simultaneously,
the Eagle-only `-share readWrite` mode is added to `open`:

```tcl
proc appendSharedLogFile { fileName data } {
  set command [list open $fileName {WRONLY CREAT APPEND}]
  if {[isEagle]} then {
    lappend command 0 file -share readWrite
  }
  set channel [eval $command]
  makeLogChannel $channel
  puts -nonewline $channel $data; flush $channel
  close $channel
  return ""
}
```

(`Eagle/lib/Eagle1.0/file2.eagle:83-104`)

The explicit `flush $channel` between `puts` and `close`
guarantees the data hits the disk before the close — useful
for log files where another process may read concurrently.

### 14.5 Ad-hoc test diagnostic output

Tests use the `tputs $test_channel ...` and `tlog ...`
helpers (see §18.3) for diagnostic output, NOT bare
`puts stdout`. The helpers route output through the test
framework's logging channel so it appears in the right log
file and respects suite-level verbosity flags.

---

## 15. File system patterns

### 15.1 `file join` is mandatory

**Never** build a path by string concatenation. Use `file
join` even for trivially obvious paths:

```tcl
# RIGHT
set f [file join $dir tests prologue.eagle]

# WRONG
set f "$dir/tests/prologue.eagle"
```

`file join` handles platform-specific path separators (`/`
vs `\`) and absolute-path-as-component shortcuts (Tcl's
documented "second argument that is absolute replaces the
first" rule).

### 15.2 The "directory of current script" idiom

The canonical way to anchor relative paths to the script's own
directory:

```tcl
source [file join [file normalize [file dirname [info script]]] prologue.eagle]
```

(`Eagle/Library/Tests/catch.eagle:16`)

**Why all four pieces:**

- `[info script]` — the path of the currently-executing source
  file.
- `[file dirname ...]` — strip the file name, leaving the
  directory.
- `[file normalize ...]` — collapse `..` and resolve to an
  absolute path; works inside `interp eval` and survives
  later `cd` calls.
- `[file join ... NAME]` — combine with the target file name.

The whole expression is a single line because the project
treats it as a literal idiom; reformatting it line-by-line
loses the recognition value.

### 15.3 File existence / type checks

Use `file exists`, `file isfile`, `file isdirectory` to gate
operations:

```tcl
if {[file exists [file join $path prologue.eagle]]} then {
  source [file join $path prologue.eagle]
}
```

(`Eagle/Library/Tests/epilogue.eagle:42`)

```tcl
if {![file isdirectory $directory]} then {
  set directory [file normalize [file join \
      [info base] bin $platform2 [appendArgs \
      $configuration Dll $suffix]]]
}
```

(`Eagle/lib/Eagle1.0/init.eagle:105-108`)

**Rules:**

- Re-evaluate `file join` at the check site rather than
  storing a path and reusing the variable; clarifies what is
  being tested.
- `file isdirectory` is preferred over `file exists` followed
  by `file isdirectory` — the single call is atomic and
  faster.

### 15.4 Other `file` sub-commands

Common usages observed:

- `file extension`, `file rootname`, `file tail` — split a
  path into components.
- `file nativename` — convert to OS-native form for shell
  invocation (Win32: backslashes; POSIX: unchanged).
- `file delete -force` — forced removal in test cleanup.
- `file mkdir` — create directory tree.

### 15.5 TH8 base-path security model

TH8 imposes a *base-path* security model on the working-
directory commands that does not exist in Tcl or Eagle.  At
interpreter creation the embedder configures a base path (see
*TH8 Public C API Specification* §31.3); the script's working
directory is **confined to that subtree**.  Within the subtree,
`pwd` and `cd` work normally.  Any operation that would escape
the subtree (e.g. an absolute path outside it, or a `..`
traversal that would rise above it) is rejected.

| Command           | Tcl / Eagle behavior              | TH8 behavior                                                                                  |
|-------------------|------------------------------------|-----------------------------------------------------------------------------------------------|
| `pwd`             | Returns the OS-absolute current dir | Returns the current directory **as a path relative to the base**: `.` at the base, `./lib` after `cd lib`, etc. |
| `cd <subdir>`     | Changes the OS cwd                  | Changes within the base-path subtree.  Accepts any path that resolves to a location inside the base; rejects paths that escape it (absolute paths outside the base, or `..` traversals beyond the root). |
| `cd ..`           | Goes up one level                   | Works inside the subtree; rejected if it would rise above the base.                            |
| `cd /absolute`    | Goes to that absolute path          | Rejected unless `/absolute` is itself inside the base.                                        |

The base path is fixed at interpreter creation; scripts cannot
change or escape it.

**Implications for portable script code:**

- **`pwd` is meaningful, but its return value differs between
  engines.**  Under Tcl/Eagle it is an OS-absolute path; under
  TH8 it is a base-relative path.  Code that captures `[pwd]`
  and later treats it as an absolute filesystem path will fail
  under TH8.  When you need an OS-absolute anchor, use
  `[file normalize [file dirname [info script]]]` (§15.2);
  when you need a within-base relative anchor, use `[pwd]`
  directly --- it works under all three engines, the value is
  just *relative* under TH8.
- **`cd <subdir>` is fine for relative navigation within the
  base subtree** under all three engines.  TH8 simply enforces
  that the destination remains inside the base; Tcl/Eagle have
  no such constraint.  Code that uses `cd subdir` for a small
  scoped traversal works portably.
- **`cd <absolutePathOutsideBase>` and `cd ../..`-style
  escapes are TH8-rejected.**  Code that does either is
  Tcl/Eagle-only and SHOULD be marked with the
  `# <th8Incompatible>` outer tag (cf. §5.8) so cross-engine
  porting can find it.
- **Test-cleanup file removals are easier with absolute paths**
  (`file delete -force [file join $test_data_path scratch.tmp]`)
  because they do not depend on the current directory --- but
  the `cd $test_data_path; file delete scratch.tmp` form is
  also portable provided `$test_data_path` is inside the base
  (which it always is under the standard test prologue).

A library that *intentionally* targets only Eagle or stock Tcl
(no TH8 deployment expected) MAY use `pwd`/`cd` freely; mark
such code with a `# <th8Incompatible>` outer tag (cf. §5.8) so
later cross-engine porting is easy to find.

---

## 16. Error message conventions

### 16.1 Phrasing patterns

The corpus uses a stable set of opening verbs:

| Form               | When                                         |
|--------------------|----------------------------------------------|
| `cannot ...`       | Operation could not complete                 |
| `must be ...`      | Argument validation failure                  |
| `invalid ...`      | Structural / format error                    |
| `wrong # args: should be "..."` | Arity error                     |
| `the X ... is not set` | Required environment / context missing  |

Examples:

```tcl
error "cannot use UnZip: unknown or unsupported version"
error "procedure command must be \[proc\] or \[nproc\]"
error "invalid directory list"
error "the EAGLE environment variable is not set"
error "wrong # args: should be \"parray a ?pattern?\""
```

(References: `Eagle/lib/Eagle1.0/unzip.eagle:148`;
`Eagle/lib/Eagle1.0/test.eagle:67, :405, :1147`;
`Eagle/lib/Eagle1.0/compat.eagle:46`)

### 16.2 No trailing punctuation

Error messages end at the final word — **no period, no colon,
no exclamation**. The Tcl error-reporting machinery prepends
its own context, and a trailing punctuation mark looks awkward
when the framework appends "    while executing ..." after it.

### 16.3 Multi-part messages with `appendArgs`

When a message must include a runtime value (a path, a count,
an offending token), build it with `appendArgs` rather than
double-quote interpolation:

```tcl
error [appendArgs \
    "unexpected trailing characters at index " $index]
```

(`scratch/eagle/startup/LoadOnStartup/Public/json.eagle:74-76`)

`appendArgs` is preferred over `"...$index"` because:

- It avoids accidental list-flatten if the value happens to
  contain whitespace.
- It documents the boundary between literal and variable.
- It composes cleanly under continuation lines.

### 16.4 Quoting of bad input in messages

Backslash-quote the input value when it could be confused with
literal text:

```tcl
error "wrong # args: should be \"parray a ?pattern?\""
```

The `\"` puts the suggested usage in literal double quotes so
the user sees `should be "parray a ?pattern?"` in the printed
message.

### 16.5 `error msg ?errorInfo? ?errorCode?`

`[error]` raises a script error that propagates up the call
stack until caught by `[catch]` (§8.6) or `[try]` (§18.4), or
reaches the top level and aborts the eval.  The single-arg
form is the dominant idiom:

```tcl
if {$count <= 0} then {
  error "iteration count must be positive"
}
```

The two extra positional arguments (`errorInfo`, `errorCode`)
are present for completeness with native Tcl but **rarely**
used in the corpus.  When they are:

- **`errorInfo`** is the multi-line stack trace prepended to
  the in-flight `::errorInfo` global.  Usually you want Tcl
  to build this itself; pass `errorInfo` only when wrapping a
  lower-level error and you need the higher-level frame to
  appear:
  ```tcl
  if {[catch {parseConfig $path} msg opts]} then {
    error "config error in $path: $msg" \
        [dict get $opts -errorinfo]
  }
  ```
  Passing the inner `errorInfo` keeps the original stack
  trace intact while letting the outer message be the one the
  user reads.
- **`errorCode`** is a Tcl list whose first element is a
  domain identifier (typically the command name or library
  name) and remaining elements are domain-specific tokens.
  The convention used in this codebase is
  `[list ARGV0 specific-token ...]` where `ARGV0` is the
  proc's own current name (see §16.6 for why that matters
  under `[rename]`):
  ```tcl
  proc parseColor { value } {
    if {![regexp {^#[0-9a-fA-F]{6}$} $value]} then {
      error "bad color: $value" "" \
          [list [lindex [info level 0] 0] BAD_HEX $value]
    }
  }
  ```
  Callers can then route on the structured token without
  parsing the message string:
  ```tcl
  if {[catch {parseColor $c} msg opts] != 0} then {
    set code [dict get $opts -errorcode]
    if {[lindex $code 1] eq "BAD_HEX"} then {
      # specific recovery path
    }
  }
  ```

**Rules:**

- Default to `error msg` (one arg).  `errorInfo` and
  `errorCode` are for cases where the *caller's* error
  handling needs more than a string.
- Never raise an error with an empty `msg` argument:
  `error ""` produces an unreadable trace and gives `[catch]`
  no string to log.  Pick the briefest description that
  identifies what was wrong.
- Pass `errorInfo` only when you have a meaningful inner
  trace to forward (i.e. when wrapping a caught error), and
  pass it through `[dict get $opts -errorinfo]` rather than
  reading the global `::errorInfo`.  The global is mutated by
  any subsequent `error` and is not safe to read across
  command boundaries.
- When you do pass `errorCode`, follow `ARGV0`-as-domain (so
  `rename` survives) and uppercase the constant tokens
  (`BAD_HEX`, `NOT_FOUND`, `OUT_OF_RANGE`).  Lowercased
  tokens are reserved for native Tcl built-ins.
- Do **not** confuse `[error]` with `[return -code error]`:
  the latter exists for re-raising a caught error from
  inside a wrapper (where the wrapper's own frame should
  not appear in the trace) and is banned for new code in
  this project (see §8.6).
- A proc that intentionally fails the test framework via
  `error` should match its `-result` with the framework's
  `error` return code constraint, not by accepting the
  message as a string.  See §10.8 for the
  `-returnCodes error` pattern.

### 16.6 Rename-safe command names in error text

A proc that names *itself* in its error messages should use
`[lindex [info level 0] 0]` rather than a hardcoded literal so
that a `[rename]`d copy reports its *current* name, not the
original:

```tcl
# GOOD: reflects rename
proc myCommand { name {default ""} } {
  if {[string length $name] == 0} then {
    error [appendArgs \
        "wrong # args: should be \"" \
        [lindex [info level 0] 0] " name ?default?\""]
  }
  ...
}

# BAD: stale after rename
proc myCommand { name {default ""} } {
  if {[string length $name] == 0} then {
    error "wrong # args: should be \"myCommand name ?default?\""
  }
  ...
}
```

The C-side equivalent of this rule is *use `argv[0]` in
diagnostics so renamed commands show the current name*; the
Tcl-script form is the same idea, just spelled differently.
Apply it whenever the proc could plausibly be renamed (most
test-fixture wrappers, helper installers, and namespace-imported
helpers can be).  For one-off internal procs that nothing else
ever touches, a hardcoded name is acceptable.

---

## 17. Procedure variants: `proc`, `nproc`, `s_proc`, `f_proc`

The corpus exposes four procedure-defining forms.  **`nproc`
exists in both Eagle and TH8, but with different semantics ---
see §17.2 below.**

| Form    | Engine          | Purpose                                                         |
|---------|-----------------|-----------------------------------------------------------------|
| `proc`  | Tcl/Eagle/TH8   | Standard procedure definition                                   |
| `nproc` | Eagle           | Native-frame proc (host-callable from .NET)                     |
| `nproc` | TH8             | Procedure with named (keyword) arguments                        |
| `s_proc`| Eagle (and Tcl) | Stub proc --- body is a `# <help>` block only                  |
| `f_proc`| Eagle (and Tcl) | Flexible proc --- uses Eagle's `nproc` if available, else `proc` |

### 17.1 `proc` — the default

Use `proc` for almost everything. The default form is
universally portable across all three engines (Tcl, Eagle, TH8).

### 17.2 `nproc` — engine-specific semantics

The single command name `nproc` resolves to **two unrelated
features** depending on the engine:

- **Eagle's `nproc`** defines a procedure visible to the .NET
  host (callable from C# via the Eagle interpreter API) without
  going through the regular `proc` evaluation path.  It is the
  Eagle equivalent of marking a method `[Cmdlet]`-callable.

  ```tcl
  nproc openAI_scriptWebClient { args } {
    ...
  }
  ```

  (`Eagle/lib/Eagle1.0/test.eagle:11955`)

  Eagle `nproc` procs typically receive the `# <callback>`
  outer tag (see §5.8.4) so the maintenance contract --- never
  rename, never change signature --- is visible.

- **TH8's `nproc`** defines a procedure where every parameter
  is bound by **name** at the call site, not by position.
  Parameters are declared as either `paramName` (required) or
  `{paramName defaultValue}` (optional with a default), and
  callers pass alternating *name* *value* pairs in any order:

  ```tcl
  nproc ::nptest {x {y 10}} {expr {$x + $y}}
  ::nptest x 5 y 20         ;# returns 25, named binding
  ::nptest x 5              ;# returns 15, default for y
  ```

  (`th8/tests/lambda.tcl:280-281`,
  Standard §13.2 / `R-64857-12453`, `R-56848-64742`,
  `R-22328-03699`, `R-21440-63588`)

  Parameter names are arbitrary identifiers; a leading hyphen
  on the identifier (`{-port 80}`, called as `... -port 8080`)
  is a TH8 *convention* that mirrors Tcl-style switches but is
  not interpreted specially by the runtime.  See Standard
  §13.2 for the full normative semantics.

**Cross-engine code that needs to be portable across Eagle and
TH8 must NOT rely on the `nproc` name** unless the surrounding
context makes the engine unambiguous.  When in doubt, gate with
`[isEagle]` (Eagle native-frame use case) or `[isTh8]` (named-
arg use case) and avoid the bare `[info commands nproc]`
existence check, which returns 1 in either engine for entirely
different reasons.

The Eagle helpers `s_proc` and `f_proc` (§17.3, §17.4) are
defined in Eagle's library and reference Eagle's `nproc`
semantics; they do not exist in the TH8 corpus.  Avoid using
them in scripts that must run under TH8.

### 17.3 `s_proc` — stub

`s_proc` defines a procedure whose body is just a `# <help>`
block. Used when the function name needs to exist for
introspection (`info commands`, `package present`) but the
implementation is a no-op or is provided elsewhere:

```tcl
proc s_proc { name {args ""} {command ""} } {; # NOTE: "args" not last.
  if {[string length $command] == 0} then {
    set command proc
  }
  ...
}
```

(`Eagle/lib/Eagle1.0/test.eagle:60-85`)

### 17.4 `f_proc` — flexible

`f_proc` chooses `nproc` if the host is Eagle AND `nproc` is
available, else falls back to `proc`:

```tcl
proc f_proc { name args body {command ""} } {; # NOTE: "args" not last.
  # flexible_procedure
  if {[string length $command] == 0} then {
    if {[isEagle] && \
        [llength [info commands nproc]] > 0} then {
      set command nproc
    } else {
      set command proc
    }
  }
  ...
  return [uplevel 1 [list $command $name $args $body]]
}
```

(`Eagle/lib/Eagle1.0/test.eagle:87-107`)

Use `f_proc` for procs that **prefer** the native frame when
it's available (so .NET callers can find them) but must still
work under native Tcl. The portable case wins; the optimisation
is opportunistic.

### 17.5 Default-argument syntax

All four forms share Tcl's `{ name {default} }` syntax for
optional positional arguments. Spaces go inside the **outer** parameter-list braces but not
inside each `{name default}` pair (see §4.3):

```tcl
f_proc promptForAndGetTextInput {
        {prompt ""} {title ""} {default ""} {noDots true}
        {canceledVarName ""} } {
  ...
```

(`Eagle/lib/Eagle1.0/test.eagle:109-111`)

Note the **double-indent** continuation (8 spaces on the second
line of the parameter list). The deeper indent is used **because
the parameter list is immediately followed by the proc body at
+4** — the same situation as a long condition inside
`if {...} then {...}` or `switch`. A plain command-call
continuation with no trailing body uses the normal **+4**
(§3.2, §3.4).

---

## 18. Runtime helpers and idioms

### 18.1 `appendArgs` — the canonical concatenator

```tcl
proc appendArgs { args } {
  set result ""; eval append result $args
}
```

(`Eagle/lib/Eagle1.0/auxiliary.eagle:39-41`)

Use it instead of `"$a$b$c"` when concatenating mixed
literal-and-variable text. Returns the joined string. Differs
from `concat` (which adds a space between items) and `join`
(which interpolates a separator) — `appendArgs` joins with
**no separator**.

### 18.2 `getDictionaryValue` — list-as-dict accessor

```tcl
proc getDictionaryValue { dictionary name {default ""} {wrap ""} } {
  foreach {pairName pairValue} $dictionary {
    if {$pairName eq $name} then {
      return [appendArgs $wrap $pairValue $wrap]
    }
  }
  return $default
}
```

(`Eagle/lib/Eagle1.0/auxiliary.eagle:50-67`)

The `wrap` argument optionally surrounds the returned value
with a literal (e.g. quotes). Used when `dict get` is not
available (Tcl 8.4) or when the dictionary is stored as a flat
list.

### 18.3 Test helpers: `tputs`, `tlog`, `cleanupFile`, `cleanupDb`

- **`tputs $test_channel "msg"`** — write to the per-test
  diagnostic channel; appears in the test log only when
  verbose mode is on.
- **`tlog "msg"`** — append to the test log file; always
  written.
- **`cleanupFile path`** — remove a file in `-cleanup`,
  ignoring errors.
- **`cleanupDb name`** — remove a SQLite test database in
  `-cleanup`.

These are project-supplied helpers (Eagle.Test /
System.Data.SQLite.Test); do not redefine them in test files.

### 18.4 `try { ... } finally { ... }`

For cleanup that MUST run regardless of body success:

```tcl
try {
  uplevel 1 [list source [file join $::env(EAGLE) \
      Plugins Commercial Enterprise Harpy Tests all.eagle]]
} finally {
  unset -nocomplain ::no(timeIntensive)
  array set ::no [array get savedNo]
  unset -nocomplain savedNo
}
```

(`scratch/eagle/startup/LoadOnStartup/Public/helpers.eagle:91-99`)

`try`/`finally` is preferred over the older `catch` + manual
re-raise pattern when the cleanup is unconditional. Use plain
`catch` when the body's failure should be observed (and
optionally suppressed), not just propagated.

### 18.5 `apply [list [list] { ... }]` — one-shot lambda

For namespace-eval-time initialization that wants its own
local scope (so temporary variables do not leak):

```tcl
apply [list [list] {
  # <help>
  # This procedure removes all cached information ...
  # </help>
  array unset ::eagle_debugger processOwner,*
}]
```

(`scratch/eagle/startup/testPrologue.eagle:18-26`)

The `[list]` first argument is the empty parameter list; the
second list element is the body. Tcl's `apply` runs the body
in a fresh frame.

### 18.6 Regular expressions

**Always use `--` to terminate option processing** before the
pattern, even when the pattern starts with a non-`-` character:

```tcl
regexp -inline -- {\w(\w)} " inlined "
regexp -all -inline -- {\w(\w)} " inlined "
regexp -start 1 -all -inline -- {\w(\w)} " inlined "
regexp -all -indices -inline -- {\w(\w)} " inlined "
```

(`Eagle/Library/Tests/regexp.eagle:20-22, :26-28, :32-34, :62-64`)

**Common flags:**

- `-all` — return every non-overlapping match.
- `-inline` — return matched substrings instead of `0`/`1` and
  populating match-vars.
- `-start N` — start matching at byte offset N.
- `-indices` — return `{start end}` index pairs instead of the
  matched text.
- `-nocase` — case-insensitive match.
- `-line` — anchor `^` and `$` per line.

**Pattern quoting:** Use `{regex}` (braces, no substitution)
unless the pattern itself contains `$VAR` interpolation that
must happen at parse time. Double-quoted patterns force the
caller to escape every `$` and `[`, which is error-prone.

### 18.7 `interp` and engine-conditional blocks

`interp create`, `interp eval`, `interp issafe`, `interp
issdk` are engine-portable. The `issafe`/`issdk` predicates
are commonly used at the top of startup files to avoid running
heavy initialization in a safe interpreter:

```tcl
if {![interp issafe] && ![interp issdk]} then {
  # heavy initialization
}
```

(`scratch/eagle/startup/startup.eagle:21+`)

### 18.8 `hasSubCommand` — cross-engine sub-command probe

When a test or library needs to know whether a particular
ensemble has a particular sub-command (e.g., does this `string`
implementation have `string totitle`?), use the
`hasSubCommand` helper from `lib/Standard1.0/test.tcl`:

```tcl
if {[hasSubCommand string totitle]} then {
  set result [string totitle $word]
} else {
  set result "[string toupper [string index $word 0]][string range $word 1 end]"
}
```

The helper handles three engine scenarios:

1. **TH8 / Tcl 8.6+**: `info subcommands $cmd` returns the
   list of valid sub-commands; `hasSubCommand` greps it.
2. **Older Tcl** (no `info subcommands`): falls back to
   invoking the ensemble with an invalid sub-command and
   checking whether the real sub-command appears in the error
   text (which lists valid sub-commands).
3. **Command not registered at all**: returns 0.

(`th8/lib/Standard1.0/test.tcl:277-309`)

Use this rather than guarding with `[isEagle]` / `[isTh8]` /
`[isTcl]` when the actual concern is *capability presence*
rather than *engine identity*.  A future Tcl release that adds
the missing sub-command will be picked up automatically; an
engine guard would not.

### 18.9 `ldifferences` — symmetric list diff

`ldifferences { list1 list2 }` returns the elements that appear
in exactly one of the two input lists (their symmetric
difference).  Use it in test bodies where the order or
duplication of list elements isn't meaningful and you only
care that the *set* of elements matches:

```tcl
} -body {
  set actual [::th8::someListReturningCommand]
  ldifferences $actual {expected1 expected2 expected3}
} -result {}
```

(`th8/lib/Standard1.0/test.tcl:56-82`)

A non-empty result identifies the elements that disagree ---
the test message points at the actual delta rather than the
whole list.

### 18.10 `clock` patterns: timestamps, ISO formatting, monotonic time

The `clock` command's surface is large; the corpus uses a
narrow slice of it consistently:

| Form | Use |
|---|---|
| `[clock seconds]` | wall-clock Unix epoch seconds (whole seconds). |
| `[clock milliseconds]` | wall-clock ms since epoch (Tcl 8.5+). |
| `[clock microseconds]` | wall-clock µs since epoch (Tcl 8.5+). |
| `[clock format $sec -format ... -gmt 1]` | format an epoch into a string. |
| `[clock scan $str -format ... -gmt 1]` | parse a string into epoch seconds. |

**Rules:**

- **Always pass `-gmt 1`** when formatting or scanning
  timestamps that cross machine boundaries (logs, signed
  artefacts, network protocols, file metadata).  The
  default uses the host's local timezone, which makes the
  output non-portable.  Use local time only for end-user
  display.
- **Use ISO 8601 (`%Y-%m-%dT%H:%M:%SZ`)** as the canonical
  format for inter-process / inter-machine timestamps.
  Round-trippable through `[clock scan]` with the same
  `-format`, sorts lexicographically in chronological
  order, and matches RFC 3339 / harpy `<<notBefore:>>` /
  `<<notAfter:>>` annotation requirements.
- **Do not use `[clock seconds]` for elapsed-time
  measurement.**  Wall-clock time can jump backward (NTP
  sync, manual adjustment, DST in some configurations).
  For "how long did this take", use `[clock milliseconds]`
  or `[clock microseconds]` and take the *difference* on
  the same machine — those are still wall-clock-based,
  but the ms/µs precision lets you detect the jump.
  TH8's preferred elapsed-time path is the embedder-side
  `xTimeMs` / `xTimeUs` callback, which the platform may
  back with a monotonic clock (`CLOCK_MONOTONIC` on POSIX,
  `QueryPerformanceCounter` on Win32) — exposed to scripts
  via `[clock milliseconds]` only when the platform wires
  it up.

**Example: round-trip an annotation timestamp.**

```tcl
# Format "now" for a notBefore annotation.
set notBefore [clock format [clock seconds] \
    -format {%Y-%m-%dT%H:%M:%SZ} -gmt 1]

# ... later, parse it back.
set epoch [clock scan $notBefore \
    -format {%Y-%m-%dT%H:%M:%SZ} -gmt 1]
```

**Anti-pattern: `[clock format $t]` with no `-gmt 1`.**
Produces output that depends on `$::env(TZ)` (or worse,
the host's `/etc/localtime`) — non-reproducible, breaks
signed-artefact comparisons.  Always opt in to UTC.

**TH8-specific divergence (informative).**  TH8 may add
`[clock ntp]` and `[clock https]` sub-commands for
external-time-source verification (see
`th8_language_extensions.md`).  These are out
of scope for portable scripts.

---

## 19. Cross-engine compatibility patterns

### 19.1 Engine probes: `[isEagle]`, `[isTh8]`, `[isTcl]`

Three parallel helpers identify which interpreter is currently
hosting the script.  All three follow the same shape: check
`::tcl_platform(engine)` against a known engine-name string,
case-insensitive:

```tcl
proc isEagle {} {
  return [expr {[info exists ::tcl_platform(engine)] && \
      [string compare -nocase eagle $::tcl_platform(engine)] == 0}]
}

proc isTh8 {} {
  return [expr {[info exists ::tcl_platform(engine)] && \
      [string compare -nocase th8 $::tcl_platform(engine)] == 0}]
}

proc isTcl {} {
  if {[isEagle]} then {return false}
  return [expr {![info exists ::tcl_platform(engine)] || \
      [string compare -nocase tcl $::tcl_platform(engine)] == 0}]
}
```

(`Eagle/lib/Eagle1.0/platform.eagle:30-38`,
`th8/lib/th8/init.th8:30-52`)

**Engine-name string** populated in `::tcl_platform(engine)`:

| Interpreter            | Value                |
|------------------------|----------------------|
| Tcl 8.4                | absent (no `engine` element) |
| Tcl 8.5+               | `"Tcl"`              |
| Eagle                  | `"Eagle"`            |
| TH8                    | `"TH8"`              |

**Rules:**

- Use the helper procs (`[isEagle]`, `[isTh8]`, `[isTcl]`) from
  any code that runs after the engine's bootstrap library has
  loaded.  In Eagle that is `init.eagle`; in TH8 it is
  `init.th8`.
- The fall-back inline expansion (`[info exists
  ::tcl_platform(engine)] && [string compare …] == 0`) is needed
  only inside the bootstrap files themselves, where the helper
  is not yet defined.
- `[isTcl]` deliberately treats Tcl 8.4 (which lacks
  `tcl_platform(engine)` entirely) as Tcl, by accepting the
  absent-element case as well as the `"Tcl"` value.
- Never `string equal` against a literal:
  `$::tcl_platform(engine) eq "Eagle"` fails on Tcl 8.4 because
  the array element does not exist (the `info exists` guard is
  what makes the helper safe).
- Use `[isTh8]` only when a TH8-specific feature is actually
  being gated.  For *broad* "is it Eagle or Tcl" branches,
  `[isEagle]` is still the right probe --- TH8 follows the
  Tcl-not-Eagle path by design.

### 19.2 Platform predicates: `isWindows`, `isMacOS`, etc.

Parallel helpers exist for OS-level checks:

```tcl
proc isWindows {} {
  return [expr {[info exists ::tcl_platform(platform)] && \
      $::tcl_platform(platform) eq "windows"}]
}

proc isMacOS {} {
  return [expr {[info exists ::tcl_platform(os)] && \
      $::tcl_platform(os) eq "Darwin"}]
}
```

(`Eagle/lib/Eagle1.0/platform.eagle:92-100`)

### 19.3 Tcl-version gating with `string is X -strict`

`string is boolean -strict` (and the other `-strict` variants)
exists in Tcl 8.5+ and Eagle. To run under Tcl 8.4 too, gate
the call with `[isEagle]` or accept the `-strict`-absent
behaviour (which treats empty strings as valid).

### 19.4 Version detection: `::tcl_version` and `::tcl_patchLevel`

Two top-level globals carry the language version the
interpreter implements:

| Global              | Form               | Example value |
|---------------------|--------------------|---------------|
| `::tcl_version`     | `major.minor`      | `8.6`         |
| `::tcl_patchLevel`  | `major.minor.patch`| `8.6.19`      |

Under TH8 these are hard-coded to `8.6` and `8.6.19`
respectively (the maximum anticipated patch level of the
native Tcl 8.6.x line, plus one).  `::tcl_platform(patchLevel)`
mirrors `::tcl_patchLevel` for compatibility with code that
reads the per-platform array form.

**Use cases:**

```tcl
# Gate a feature that exists in Tcl 8.5+ but not 8.4.
if {[package vsatisfies $::tcl_version 8.5]} then {
  # use the 8.5+ feature
} else {
  # fall back
}

# Identify the TH8 build in a log line.
puts stdout "running under TH8 $::tcl_patchLevel"
```

**Rules:**

- Prefer `[package vsatisfies $::tcl_version X.Y]` over manual
  `string compare` against `::tcl_version` --- it handles the
  ordering correctly and reads better.
- Use `::tcl_patchLevel` for log lines and diagnostic output;
  it identifies the exact build.  Use `::tcl_version` for
  conditional feature checks; the patch level is too narrow.
- Do not parse `::tcl_patchLevel` by `split . `: the format is
  `M.m.p` exactly, but a future engine MAY append extra
  components (`8.6.19a1`, `8.6.19+ts`).  Match on the leading
  `^\d+\.\d+\.\d+` prefix instead.
- These globals are present on every conforming engine
  (TH8, Tcl 8.5+, Eagle).  No `info exists` guard is needed
  for `::tcl_version` --- it is always set by interp init.

#### Detecting debug builds: `::tcl_platform(debug)`

TH8 sets `::tcl_platform(debug)` to the string `"1"` when the
interpreter was compiled with `TH8_DEBUG` defined; on release
builds the element is **absent** (not "0", not the empty
string --- absent).  Test for either condition explicitly:

```tcl
proc isDebugBuild {} {
  return [expr {[info exists ::tcl_platform(debug)]
                && $::tcl_platform(debug) eq "1"}]
}
```

Use this in tests to gate diagnostics that only make sense on
a debug build (assertion-rich error messages, internal
counter dumps), or to skip performance-sensitive timing tests
that would fail on a debug build's slower path.

### 19.5 Inline ternary for engine-specific values

For one-off engine differences in a constant value, use the
inline ternary inside `expr {...}`:

```tcl
set translation [expr {[isEagle] ? "protocol" : "auto"}]
```

(`Eagle/lib/Eagle1.0/file2.eagle:60-63`)

Reserve the explicit `if {[isEagle]} then { ... }` form for
multi-line bodies; the ternary is cleaner for single-value
selection.

---

## 20. TH8-specific divergences (intentional & to-keep)

TH8 adopts the Eagle scripting style as the **default** but has
a few intentional divergences. Each is justified; do not
"normalize" these without explicit user request.

### 20.1 R-marker comments inside test descriptions

TH8 binds every test to one or more requirement markers from
the Tcl Language Standard:

```tcl
runTest {test append-1.1 {
  R-06409-32483: append to existing variable
} -setup {
    unset -nocomplain x
} -body {
    ...
```

(`th8/tests/append.tcl:23-33`)

The marker `R-NNNNN-NNNNN` is MD5-derived from requirement text
(see `tools/mkreq.tcl`). Every test that exists must reference
at least one R-marker; every R-marker in the standard must be
referenced by at least one test. This is a **TH8 contract** that
extends Eagle's test framework, not a divergence to fix.

#### 20.1.1 Multi-line test descriptions

When the test description (R-marker prefix + summary text)
would exceed column 79, wrap onto continuation lines inside the
description block.  The R-marker tooling
(`tools/mkreq.tcl --check-tests`) concatenates every line
between the opening `{` (after `test NAME `) and the matching
closing `}` (before any `-OPTION` clause), normalises
whitespace to single spaces, and then extracts the leading
`R-NNNNN-NNNNN:` prefix:

```tcl
runTest {test foo-1.1 {
  R-29508-16704: a longer description that wraps onto continuation
                 lines because the original is too verbose to fit
                 in a single line under the 79-column limit
} -setup {
    ...
```

**Rules:**

- The R-marker must appear at the **start** of the first
  description line (after the opening `{` and any indent).
- Continuation lines may be indented to any column; the parser
  collapses interior whitespace before the prefix is read.
- The closing `}` of the description must be on its own line
  (or on a line followed only by whitespace and a `-OPTION`
  keyword) so the parser can locate it.
- Single-line forms remain valid:
  `runTest {test foo-1.1 {R-29508-16704: short description} -body { ... }}`.
- Use multi-line wrapping **only when** the single-line form
  would exceed column 79.  Don't wrap merely for stylistic
  preference; readers expect the rhythm of one-line
  descriptions for typical tests.

#### 20.1.2 Multi-line `:   ` text in the standard

The same wrap-friendly behaviour is supported by the standard's
own R-marker block format.  When a requirement's text exceeds
column 79, break it into multiple `:   ` continuation lines —
each starts with `:   ` (colon + three spaces) at the same
column as the R-marker line:

```
R-29508-16704
:   The maximum byte length of any string value is 100 MB
:   (104,857,600 bytes).
```

`mkreq.tcl` (in `--verify`, `--refresh`, and `--check-tests`
modes) concatenates the continuation lines with single-space
joins before computing the MD5, so the wrapped form hashes
identically to the single-line form.  Likewise, the input
marker `^  ` accepts multi-line runs:

```
^  Long new requirement text that wraps for
^  readability and remains a single requirement.
```

`mkreq.tcl` will coalesce the run into one R-marker and emit
one `:   ` line per input line in the rewritten file.

### 20.2 (FORMERLY: 4-space test-body divergence — RESCINDED)

Earlier revisions of this guide carried a TH8-specific
divergence allowing 4-space indent inside `-setup`/`-body`/
`-cleanup` blocks while keeping 2-space everywhere else.

**That divergence is rescinded as of 2026-04-28.** All Tcl /
TH8 / Eagle script code uses **2-space indent** at every
nesting level, with **continuation lines indented +4 spaces
deeper** than their parent line — see §2.1 and §3.2.

The unified canonical form for a TH8 test:

```tcl
runTest {test append-1.1 {
  R-06409-32483: append to existing variable
} -setup {
  unset -nocomplain x
} -body {
  set x "hello"
  append x " world"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello world}}
```

Test files written under the old divergence are reformatted
en masse by `tools/reformat_tests.tcl --write tests/`; new tests
should be written in the unified form from the outset.

### 20.3 LF line endings (vs Eagle's CRLF)

TH8 source files use LF line endings; Eagle uses CRLF. When
editing within TH8, **preserve LF**.

### 20.4 Namespace names: `::th8test` and `::th8testlib`

Eagle's library namespaces are PascalCase (`::Eagle`,
`::Licensing`).  TH8 uses **lowercase** namespace names to
match the project's `th8` lowercase convention (the C
identifier prefix is also lowercase).

Two related test namespaces exist; do not conflate them:

| Namespace        | Source side | Where defined                                | Purpose                                                          |
|------------------|-------------|----------------------------------------------|------------------------------------------------------------------|
| `::th8test`      | Tcl/script  | `lib/Standard1.0/test.tcl`, `exec.tcl`, `load.tcl` | Test-framework procs that are themselves written in Tcl: `runTest`, `test`, `setupCommandConstraints`, the `sandboxRc` / `faultRc` accessor families, etc. |
| `::th8testlib`   | C           | `src/test/th8_testlib.c`                     | Test-only commands implemented in C: `::th8testlib::fault`, `::th8testlib::sandbox`, `::th8testlib::preeval`, `::th8testlib::preload`, `::th8testlib::isadmin`, `::th8testlib::key_token`, `::th8testlib::signed_only`. |

**Rules:**

- Test scripts call `::th8testlib::*` for any operation that
  needs to inspect or perturb the C-level interpreter (fault
  injection, sandboxed eval, pre-eval / pre-load hooks).
- Tcl-level wrappers around `::th8testlib::*` (e.g.
  `isAdministrator` in §20.4 example, the `*Rc` / `*Result`
  accessors in §10.9) live in `::th8test`.
- When `::th8testlib::cmd` is unavailable (e.g. running under
  upstream Tcl or Eagle, where the C test library is not
  present), `::th8test` wrappers should detect with
  `[llength [info commands ::th8testlib::cmd]] > 0` and
  degrade gracefully:

  ```tcl
  proc isAdministrator {} {
    if {[llength [info commands ::th8testlib::isadmin]] > 0} then {
      return [::th8testlib::isadmin]
    }
    # … fall back to engine-specific probes …
  }
  ```

  (`th8/lib/Standard1.0/test.tcl:84-86`)
- Never expose `::th8testlib::*` as the script-visible API for
  a feature that is meant to ship to end users; the `lib`
  suffix is a TH8 convention for "test-internal C glue".

### 20.5 `then` keyword: same as Eagle

TH8 follows Eagle's `if {…} then {…}` convention. Keep it.

### 20.6 Section dividers in `lib/Standard1.0/`

Top-level dividers in TH8 lib files use the same pad-to-column-79
form as Eagle (see §1.3). No divergence.

### 20.7 Script signing and `.b64sig` files

TH8 supports signed-only script execution: when the
runtime is configured for signed-only mode (the default in
production builds), every script loaded from disk must have
a companion `.b64sig` sidecar file containing a base64-encoded
RSA detached signature over the script bytes.  (The signature
length follows the signing key: the bundled test key is
RSA-2048 — a 256-byte raw signature — while a production key
may be larger.)

**Workflow:**

```bash
# Sign a freshly-edited script using the test signing key.
bash tools/signScript.sh tests/hello.tcl
#   -> writes tests/hello.tcl.b64sig
```

The shell wrapper drives `signScript.th8` (a TH8 program
that performs the RSA sign).  The test build
(`make ENABLE_TEST_KEY=1 fresh`) embeds a known test key so
local development does not require the production CA.

**Rules:**

- After **every** content change to a `.tcl`, `.eagle`, or
  `.th8` file that ships under signed-only policy, re-sign
  via `tools/signScript.sh` before running tests against
  the file.
- **Sign only files you actually changed.**  Do NOT mass-sign
  the test tree as a precaution --- it churns the diff with
  signature-only updates that hide real edits in code review,
  and it can mask the absence of a content change that
  *should* have happened.  If a check-in arrives with stale
  signatures, the maintainer signs them in a follow-up; do not
  pre-emptively re-sign on their behalf.
- `.b64sig` files are committed to the repository but are
  **never** edited by hand --- they are the output of the
  signing tool and are treated as build artifacts that must
  remain in sync with their parent script.
- Linters and formatters MUST exclude `**/*.b64sig` from
  any rewrite pass.  Reformatting a `.b64sig` file silently
  invalidates the signature.
- Add the following exclude line to `tclint.toml` and any
  hand-rolled checker:
  ```toml
  exclude = ["**/*.b64sig", ...]
  ```
- When a checker iterates over the script tree, it should
  flag any `*.tcl` file that lacks a `*.tcl.b64sig`
  companion (or vice versa) under a build that ships under
  signed-only policy.  An orphaned signature file is just
  as bad as a missing one --- both indicate the pair has
  drifted.

This is a TH8 contract that does not exist in Eagle or
upstream Tcl; it is documented here because every
contributor will encounter it the first time they edit a
script and run the test suite.

### 20.8 The `secure` command (TH8)

TH8 provides a `secure` command that creates a variable
whose value is held encrypted (AES-256-GCM) on a memory
page that's `mlock`'d against paging and zeroed on delete.
The variable behaves like an ordinary scalar at the script
level — `set`, `append`, `incr`, command substitution, and
expression context all work — but the cleartext is never
present in the heap longer than required to satisfy the
read.

```tcl
secure create token             ;# create empty secure var
secure create token "abc123"    ;# create with initial value
set token "rotated"             ;# update encrypted value
puts $token                     ;# decrypts on read; do not log!
secure exists token             ;# 1 if token is a secure var
secure delete token             ;# zero + free; var no longer exists
```

**Rules:**

- Use `secure` for **short-lived secrets** that pass
  through script: signing keys, OAuth bearers, just-derived
  passphrase material.  It is NOT a long-term key store —
  the encryption key is interp-scoped and dies with the
  interp.
- The variable created by `secure create` cannot be the
  element of an array; `secure create arr(key) ...` errors.
  If you need keyed slots, use a flat naming convention
  (`secret_token_db`, `secret_token_api`) and one secure
  scalar per slot.
- Re-creating an existing **plain** variable as secure
  succeeds and adopts the prior value, encrypting it in
  place.  Re-creating an existing **secure** variable is a
  no-op (does not re-key); use `secure delete` first if you
  truly need fresh keying.
- `secure delete` errors if the named variable is not a
  secure variable.  Use `secure exists` to probe before
  deletion in cleanup code.
- Do NOT log, `puts`, or persist a secure variable's
  cleartext.  The corpus convention is to read into a
  same-scope plain variable only at the call site that
  needs the cleartext, and `unset` it on the next line.
- Secure-variable slots are recycled: after `secure
  delete`, a subsequent `secure create` may re-use the same
  internal slot.  Do not rely on slot identity for any
  observable behaviour.

Reference: Tcl Language Extensions §29.8.

### 20.9 Custom argument-expansion operators (TH8)

Tcl 8.5+ defines exactly one expansion operator: `{*}` (see
§7.7).  TH8 extends this with **custom operators** of the
form `{tag}value`, where `tag` is a name registered via the
public C API `Th8_RegisterExpansion` (the matching teardown
API is `Th8_UnregisterExpansion`):

```tcl
# Hypothetical registered operator "json" that parses a JSON
# array into Tcl list elements.
puts [list {json}{"a","b","c"}]    ;# -> a b c
```

**Rules:**

- **Do not register custom operators in script.**  The
  operator registry is C-level; scripts can only USE
  registered operators.  This is the same security boundary
  as `Th8_QueueEvent` / `[update]` (see §8.10).
- **Registration is namespace-scoped.**  An operator
  registered in `::Foo` is visible only to commands
  evaluated in `::Foo` (or a child namespace that has
  imported it).  Embedders that want a global operator
  must register it in the global namespace at startup
  --- and then accept that the operator becomes part of
  the platform contract for every script the interp
  evaluates.
- An unregistered operator name is a syntax error at parse
  time, not a runtime error: `{noSuch}value` errors before
  the surrounding command begins to execute.
- Custom operators are NOT portable to Tcl or Eagle.  Code
  that uses anything other than `{*}` MUST be guarded with
  `[isTh8]` (or `[info commands ::th8::*]` for a more
  feature-specific probe) so it doesn't break under
  upstream Tcl.
- The operator namespace is shared with `*` (the standard
  expansion operator); embedders MUST NOT register a
  conflicting `*` handler.  Other reserved names are
  documented in the C API spec.

Reference: Tcl Language Extensions §20.0l.

### 20.10 R-marker maintenance discipline

R-markers in the standard documents are MD5-derived from
the canonical-form requirement text (§20.1).  This means
**editing requirement text changes its R-marker.**  The
discipline that follows from this is:

**When you edit requirement text in the standard:**

1. Run `tools/mkreq.tcl --refresh docs/public/<file>.md`.
   The tool prints an audit trail of `R-OLD -> R-NEW`
   renames for any text that changed enough to recompute
   to a different ID.
2. For every renamed marker, update **all** test files
   that reference it.  `tools/mkreq.tcl --check-tests`
   reports orphans (test references with no current
   marker) and uncovered markers (markers with no test
   reference); both must be **0** before the change is
   considered complete.
3. Re-sign the affected test files (and only those — see
   §20.7 on not mass-signing).

**When you add a new requirement:**

1. Insert the new text under a leading `^  ` marker — the
   `--refresh` mode coalesces multi-line `^  ` runs into a
   single requirement and emits the canonical
   `R-NNNNN-NNNNN` line plus `:   ` continuation lines.
2. Add at least one test that references the new marker
   in its description block.
3. `--check-tests` MUST report **0 uncovered**.

**When you remove a requirement:**

1. Delete the test(s) referencing it (or repoint them to
   an existing related marker if the test still exercises
   covered behaviour).
2. Delete the requirement text from the standard.
3. `--check-tests` MUST report **0 orphans**.

**Cross-doc coverage.**  R-markers live in the standard
(`tcl_language_standard_v1.md`), the C API spec
(`th8_public_c_api_specification.md`), and the extensions
doc (`th8_language_extensions.md`).  `--check-tests`
must be run against the **set** of doc files, not just
one — historical regressions occurred when a doc was
moved between files and only one file was scanned.

**Rule of thumb.**  R-markers are part of the source of
truth; treat them with the same care as exported C
identifiers.  Renaming a public API costs a coordinated
edit across header, impl, stub table, doc, and tests; a
requirement-text edit costs the same coordinated edit
across standard and tests.  Do not skip steps.

Reference: `tools/mkreq.tcl` (especially `--verify`,
`--refresh`, `--check-tests` modes); §20.1 above.

### 20.11 TH8 testlib command reference

The C-level test library `::th8testlib::*` (defined in
`src/test/th8_testlib.c`; see also §20.4) exposes
introspection and perturbation surfaces that script-level
TH8 deliberately doesn't expose.  Use these commands ONLY
inside test files; they are not part of the shipping API.

| Command                                | Purpose                                                                                                           | Typical use                                                              |
|----------------------------------------|-------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------|
| `::th8testlib::fault EXPR`             | Evaluate EXPR with allocation-failure injection enabled; returns `{rc result allocCount triggered}`.              | Exhaustive OOM coverage of a code path; see §10.9 for the accessor procs.|
| `::th8testlib::sandbox EXPR`           | Evaluate EXPR in a sandboxed sub-interp; returns `{rc result steps allocCount}`.                                  | Step-bounded eval; see §10.9.                                            |
| `::th8testlib::preeval install ?MODE?` | Install a pre-eval hook (runs before every `Th8_Eval`).  MODE is `allow` (default) or `deny`.                     | Verifying hook fires the expected number of times; testing deny-mode rejection. |
| `::th8testlib::preeval uninstall`      | Remove the pre-eval hook.                                                                                         | `-cleanup` block paired with `install`.                                  |
| `::th8testlib::preeval count`          | Number of times the installed pre-eval hook has fired.                                                            | Asserting fire-count after a workload.                                   |
| `::th8testlib::preeval mode`           | Current mode (`allow` or `deny`); errors if no hook is installed.                                                 | Verifying install-with-mode took effect.                                 |
| `::th8testlib::preload install ?MODE?` | Install a pre-load hook (runs before every `[source]` or extension load).  Same MODE values as `preeval`.         | Verifying signed-only file admission logic.                              |
| `::th8testlib::preload uninstall`      | Remove the pre-load hook.                                                                                         | `-cleanup` pairing.                                                      |
| `::th8testlib::preload count`          | Number of times the installed pre-load hook has fired.                                                            | Same shape as `preeval count`.                                           |
| `::th8testlib::preload mode`           | Current pre-load mode.                                                                                            | Same shape as `preeval mode`.                                            |
| `::th8testlib::isadmin`                | 1 if process is administrator/root; cross-platform.                                                               | Skipping or gating tests that require elevated privileges.               |
| `::th8testlib::key_token NAME`         | Returns 16-hex-char fingerprint for a registered key NAME (e.g. `keyRoot`, `key0`).                               | Asserting that the embedded key matches what a build claims.             |
| `::th8testlib::signed_only query`      | Returns `1` if the runtime is in signed-only mode, `0` otherwise.                                                 | Driving the `crypto_enabled` / `crypto_disabled` constraints in §10.5.1. |
| `::th8testlib::load_snk PATH`          | Load an Eagle-format strong-name (.snk) key file; returns an opaque token.                                        | Harpy-certificate test setup (see `tests/harpy.tcl`).                    |
| `::th8testlib::load_key_file PATH`     | Load an RSA key from PATH; returns 1 on success, errors on failure.                                               | Round-trip key-file parser tests.                                        |
| `::th8testlib::kv set/get/exists/unset KEY ?VAL?` | Test-side accessor for the persistent KV store (SQLite-backed when `kv_sqlite` is true).                | Round-trip persistence tests; cleanup helper for stress runs.            |

**Rules:**

- **Probe presence before calling.**  Run-time builds that
  omit the test C library (e.g. shipped non-test builds)
  will lack `::th8testlib::*`.  In `lib/Standard1.0/test.tcl`,
  the constraint definitions already do this; in test
  bodies, rely on the constraint instead of probing again.
- **Never wrap testlib commands in production scripts.**
  The `::th8testlib::*` namespace is not a stable public
  surface --- entries may be added, removed, or have their
  return shapes adjusted between releases without
  deprecation.  Production scripts that need
  similar introspection must use the public `Th8_*` C API
  via a script-level command added to a shipping plugin.
- **Don't shadow.**  Do not `proc ::th8testlib::xyz {} { … }`
  in test code --- the shadowing semantics differ between
  TH8 and Eagle in subtle ways and the C-level command's
  behaviour is what the test should be exercising.
- **Always pair `install` with `uninstall` in `-cleanup`.**
  These hooks are interp-scoped state; if a test installs
  one but does not uninstall, subsequent unrelated tests
  inherit the hook and behave unexpectedly.  Wrap the
  uninstall in `catch` so a failed install in `-setup` /
  `-body` doesn't leave the cleanup itself erroring:

  ```tcl
  } -setup {
    ::th8testlib::preeval install
  } -body {
    set before [::th8testlib::preeval count]
    eval {set x 1}
    expr {[::th8testlib::preeval count] - $before}
  } -cleanup {
    catch {::th8testlib::preeval uninstall}
  } -result {1}}
  ```

  For `deny`-mode tests, prefer the dedicated
  `::th8testlib::preeval_denytest` C-level helper --- it
  performs the install / trigger / uninstall sequence as
  one atomic operation, so a deny that erroneously rejects
  the *uninstall* itself can't strand the hook.

Reference: `src/test/th8_testlib.c`;
`lib/Standard1.0/test.tcl` constraint definitions;
`tests/fault/`, `tests/crypto.tcl`, `tests/harpy.tcl` for
canonical usage examples.

### 20.12 Signed-eval workflow (`signed_only`, `verify_sig`, `sig_hashes`)

When TH8 ships under signed-only policy, the test surface
exposes the policy machinery via three top-level commands:
`::th8testlib::signed_only` (with the sub-commands `query` and
`eval_signed`), `::th8testlib::verify_sig`, and
`::th8testlib::sig_hashes` — so tests can exercise both admission
paths and policy persistence.

**Query the policy:**

```tcl
::th8testlib::signed_only query     ;# 0 or 1
```

`query` is non-mutating; `crypto_enabled` and
`crypto_disabled` constraints (§10.5.1) are computed from
this on `prologue.tcl` startup.

**Verify a signature without executing:**

```tcl
::th8testlib::verify_sig SCRIPT.tcl SCRIPT.tcl.b64sig
#   -> 1 if valid, 0 if invalid, error if files missing
```

`verify_sig` reads the signature sidecar (§20.7) and the
script bytes, then validates the RSA signature without
sourcing or evaluating.  Use this to assert signature
correctness independently of the loader.

**Eval a signed script under signed-only policy (persistent — see below):**

```tcl
::th8testlib::signed_only eval_signed SCRIPT.tcl
#   -> {rc result}
```

`eval_signed` activates signed-only mode (admitting only
signed scripts via `[source]`) and evaluates SCRIPT.tcl under
that policy.

**Critical semantics — `eval_signed` does NOT revert.**
After `eval_signed` returns, the policy state persists
(`::th8_security(policy)` remains `1`).  This is
intentional: once a script has executed under signed-only
mode, the interp has been exposed to its trust assumptions
and cannot safely return to a less-privileged state.  Tests
that exercise this property:

```tcl
runTest {test crypto-6.7 {
  R-58065-24188: The signed_only policy SHALL not revert to off
                 after eval_signed completes.
} -constraints {th8 crypto_testlib crypto_enabled} -body {
  ::th8testlib::signed_only eval_signed tests/data/signed_noop.tcl
  set ::th8_security(policy)            ;# expect 1, not the prior 0
} -result {1}}
```

**Compare hashes (debugging / forensic test only):**

```tcl
::th8testlib::sig_hashes SCRIPT.tcl SCRIPT.tcl.b64sig
#   -> {expectedHash actualHash}
```

`sig_hashes` is for diagnosing why a verification failed
(the two hashes differ when bytes have drifted).  It is NOT
a substitute for `verify_sig` --- never gate admission
logic on raw hash equality at the script level.

**Rules:**

- Tests that mutate `::th8_security(policy)` MUST be
  isolated --- they cannot run before tests that depend on
  the original policy state.  The conformance runner
  isolates by file, so the convention is: **one
  `eval_signed`-using file per build mode**, and never call
  `eval_signed` in shared setup / prologue scripts.
- The script you pass to `eval_signed` MUST be on disk and
  signed; passing a string to be evaluated will fail
  policy admission.  Generate signed test fixtures into
  `tests/data/` and reference them via `$test_data_path`
  (§10.10).
- `verify_sig` and `sig_hashes` accept any path, regardless
  of policy state --- they are pure functions over file
  content and never alter interp state.  Calling them does
  not engage the loader.
- Errors from `eval_signed` propagate as Tcl errors;
  catch with `catch` (§8.6) at the call site and inspect
  the result for `signature verification failed` /
  `unsigned script not permitted` to distinguish failure
  modes if the test needs to.

Reference: Tcl Language Extensions §29.2--§29.4;
`tests/crypto.tcl` for the canonical exercise of every
edge of this surface.

### 20.13 The `flags` command (character-flag strings)

TH8's `flags` plugin command (Harpy-compatible) operates on
**character-flag strings** --- short strings where each
character is an independent boolean attribute.  Two
sub-commands cover the entire surface:

```tcl
flags have ?-all? ?-strict? ?-complex? HAVE FLAGS
flags change ?-sort? ?-strict? ?-complex? ?-key K? HAVE OPS
```

**Membership tests:**

```tcl
flags have abc a              ;# 1   -- 'a' is in "abc"
flags have abc d              ;# 0
flags have -all abc abc       ;# 1   -- all of {a,b,c} present
flags have -all abc abcd      ;# 0   -- 'd' missing
flags have abc {}             ;# 1   -- empty FLAGS is vacuously true
```

**Mutation operators (in OPS string):**

| Op   | Meaning                                |
|------|----------------------------------------|
| `+X` | Add character X if absent              |
| `-X` | Remove character X if present          |
| `=X` | Replace entire string with X (clear+set)|
| `+xy-b` | Multiple ops chained left-to-right  |
| `-*` | Remove all (empty result)              |

```tcl
flags change -sort abc +x     ;# abcx
flags change abc -b           ;# ac
flags change abcdef =xyz      ;# xyz
flags change abcxyz -*        ;# {}     (cleared)
```

**Rules:**

- The result of `flags change` is the new flag string ---
  it does NOT mutate the input variable.  Assign back if
  you want a destructive update:
  `set f [flags change $f +x]`.
- `-sort` produces a **canonical** ordering (alphabetical,
  case-sensitive); use it whenever the output must
  compare equal to a known constant.
- `-strict` rejects characters outside `[A-Za-z0-9]`; off
  by default for compatibility with Harpy's looser
  semantics.
- `-complex` enables the keyed `{N:abc}` form for namespaced
  flag domains (rare; used for Harpy-attribute round-trips).
  Do not combine with non-complex inputs.
- `-key 0xNN` annotates a complex flag set with a key byte;
  meaningful only with `-complex`.  Default is `0`.
- An empty HAVE string is a valid starting point for `flags
  change`: `flags change "" +x` -> `x`; with no ops it yields `{}`.
- Tests gate on `-constraints { th8 flags }`; the command
  is a plugin and may be compiled out of stripped builds.

**Idiomatic uses:**

```tcl
# Boolean attribute set, stored in a single string
set perms [flags change "" =rwx]
flags have $perms w               ;# write?  -> 1
set perms [flags change $perms -w] ;# revoke write
flags have -all $perms rx          ;# read+exec still?  -> 1

# Diff between two flag sets via a difference probe
proc flagDiff { a b } {
  list [flags change $a -[string map {{} ""} $b]] \
       [flags change $b -[string map {{} ""} $a]]
}
```

The character-flag form is intentionally compact: a
canonical ordering plus single-character idents makes
flag strings cheap to log, cheap to compare, and trivial
to express as `-constraints` strings or `-result` literals
in tests.

Reference: `src/plugins/th8_harpy.c` (the `flags`
command is part of the Harpy compatibility plugin);
`tests/flags.tcl` for canonical surface coverage.

### 20.14 The `harpy` command (XML certificates / RSA signing)

The `harpy` command (Tcl Language Extensions §32.3-32.4)
exposes Eagle-compatible Harpy-format script signing.
Unlike `tools/signScript.sh` (which produces a `.b64sig`
sidecar), `harpy sign` returns the signature *value* and
leaves on-disk layout to the caller --- it is the
script-level primitive that the file-oriented signing
tools build on top of.

**The token model.**  All `harpy` operations require a
**key token** --- an opaque handle obtained by loading a
key pair (or public-only key) via the test surface:

```tcl
set token [::th8testlib::load_snk tests/helpers/th8_test_key.snk]
```

The token captures both the key material and the policy
state at load time.  A token that holds only a public key
can verify but cannot sign --- attempting to do so raises
the canonical error `harpy sign rejects public-only key`
(R-01323-22268).

**Sign and verify:**

```tcl
set sig [harpy sign $token "set x 1"]   ;# returns the signature blob
harpy verify $token "set x 1" $sig      ;# ok if valid; errors if not
```

The signature blob is a multi-line string with a header
comment, the encoded RSA signature, and a trailing
checksum.  It is **stable across runs** (deterministic
signing) but treat it as opaque --- never parse the
contents at the script level.

**Tamper detection:**

```tcl
catch {harpy verify $token "puts {tampered}" $sig} msg
#   -> non-zero rc; $msg explains the verification failure
```

Verification distinguishes:

- **Bad signature** (signature blob doesn't match the
  script bytes under the loaded public key) --- raised as
  a script error.
- **Wrong token** (signature was produced under a
  different key) --- raised as a script error with a
  distinct message; the test surface uses regex
  `-result` matching to assert which class of failure
  occurred.

**Policy gating.**

`harpy` is only registered when the runtime is configured
for signed-execution mode AND a Harpy policy is loaded.
Tests that exercise `harpy` MUST gate on:

```tcl
-constraints { th8 harpy_sign crypto_enabled }
```

`harpy_sign` is true iff `harpy sign` is available (some
builds load only `harpy verify`); `crypto_enabled`
guarantees the surrounding policy is engaged (§10.5.1).

**Rules:**

- Treat `harpy` tokens as **per-test resources**.  Load
  in `-setup`, dispose in `-cleanup`; do NOT cache
  globally except via the prologue's
  `::_harpyToken` --- and only when the prologue itself
  loaded it (the prologue's load is conditional on the
  build mode; `[info exists ::_harpyToken]` is the
  authoritative probe).
- `harpy sign` is **deterministic** for a given (token,
  script) pair --- repeated calls return the same signature
  blob.  Use this property in tests that compare two
  signatures: equality means "same key + same input",
  inequality means at least one differs.
- `harpy sign` does NOT include filesystem metadata in the
  signature --- it signs only the script bytes you pass
  in.  This differs from some platform code-signing
  systems and is intentional: it makes the signature
  position-independent.
- The error text from a failed `harpy verify` is part of
  the conformance contract --- the standard pins specific
  R-markers to specific error phrases.  Do not rephrase
  these in plugin code without a coordinated update to
  the standard and tests.

**`harpy` vs `tools/signScript.sh`:**  `harpy sign`
returns the signature value; `signScript.sh` writes the
`.b64sig` sidecar.  The shell script is a thin wrapper
that calls a TH8 program (under `tools/`) which itself
invokes `harpy sign`.  At the script level, prefer the
plugin command for in-memory signing of strings; reach
for the shell tool only when you need a sidecar on disk.

Reference: `src/plugins/th8_harpy.c`; `src/plugins/harpy/`
for the certificate-policy machinery; `tests/harpy.tcl`
for surface coverage.

---

## 21. Anti-patterns (must not do)

These patterns are **absent from the entire corpus** — i.e. their
absence is itself a rule:

1. **Tabs for indentation.** Never. Always 2-space.
2. **`if cond { … }`** (no condition braces). Always
   `if {cond} then { … }`.
3. **`if {cond} { … }`** (no `then`). The `then` keyword is
   required.
4. **`else if`** (two words). Use `elseif`.
5. **`expr $x + 1`** (unbraced expr). Use `expr {$x + 1}`.
6. **`proc name {args} {`** (no spaces inside arglist braces).
   Use `proc name { args } {`.
7. **Opening brace on a new line:**
   ```tcl
   proc name { args }
   {
     ...
   }
   ```
   Always same-line.
8. **`unset varName`** in test cleanup. Use `unset -nocomplain
   varName` (the variable may not exist if the body errored).
9. **`return -code error msg`**. Use `error msg`.
10. **Top-level proc definitions in library files** — wrap in
    `namespace eval ::Ns { … }`.
11. **Two consecutive blank lines inside a proc body.**
12. **Trailing whitespace** on any line.
13. **`puts` with no channel argument when writing diagnostics.**
    Test/diagnostic output uses `tputs $test_channel …` or
    `tlog …`; ordinary I/O uses `puts $channel …` with explicit
    channel.
14. **Mixed indent widths within one file.**
15. **Treating the result of `[pwd]` as an OS-absolute path** in
    cross-engine code.  TH8 returns it relative to the base
    path; Tcl/Eagle return it absolute.  Use `[file normalize
    [file dirname [info script]]]` (§15.2) when an OS-absolute
    anchor is what the code actually needs.
16. **`cd` to a path outside the base-path subtree** (an
    absolute path that doesn't start with the base, or a
    `../..`-style escape).  TH8 rejects these; Tcl/Eagle
    accept.  Either constrain navigation to inside the base, or
    mark the proc with `# <th8Incompatible>` (§5.8, §15.5).
17. **Hand-editing a `*.b64sig` file.**  These are RSA-signed
    artifacts; any byte change invalidates the signature.  Use
    `bash tools/signScript.sh` to regenerate (§20.7).
18. **Reformatting / linting a `*.b64sig` file.**  Even a
    trailing-whitespace strip invalidates it.  Linters MUST
    `exclude = ["**/*.b64sig"]` (§22.3).
19. **Treating `::th8test` and `::th8testlib` as the same
    namespace.**  They are not.  `::th8test` is the Tcl-side
    framework; `::th8testlib` is the C-side test library.  Use
    the right qualifier; `[llength [info commands ::th8testlib::cmd]] > 0`
    is the canonical availability probe (§20.4).
20. **Using `[info commands nproc] > 0` as a TH8/Eagle disambiguator.**
    Both engines have `nproc` --- with completely different
    semantics.  Use `[isEagle]` / `[isTh8]` (§17.2).
21. **`[object invoke ...]` or `[compileCSharpWith ...]` without
    an `[isEagle]` guard.**  These are Eagle-only; under
    TH8 / Tcl they raise an undefined-command error before any
    handler can intercept (§11).
22. **A `package require` line BEFORE `package require th8`** in
    a TH8 script that depends on the bootstrap library.
    `[isEagle]`, `[isTh8]`, `[appendArgs]`, etc. require
    `package require th8` to have run first (§13.1).
23. **Mutating an array between `array startsearch` and the
    matching `donesearch`.**  Adding or removing elements bumps
    the array's epoch and the next `nextelement` raises an
    error.  If the walk needs to mutate, snapshot the keys
    first via `[array names]` (§9.3.1).
24. **Fabricating, parsing, or comparing an array search ID.**
    The token returned by `array startsearch` is opaque and
    valid only against the array it was opened on.  Any other
    value — even one of the right surface format — must error
    (§9.3.1).
25. **Skipping `array donesearch` on the error path.**  An
    open search holds a reference into the per-interp search
    registry until released; tests must call `donesearch`
    inside `-cleanup { … }` even when the body errored.
26. **Regex-matching `array statistics` output.**  The format
    is implementation-defined diagnostic text, not a parseable
    contract (§9.3.2).

---

## 22. Tooling

### 22.1 The state of Tcl style tooling (April 2026)

There is **nothing in the Tcl ecosystem with the maturity of
clang-format, gofmt, or black**. The viable options:

- **`tclint` / `tclfmt`** (Python, 2023-2026, MIT) — the only
  modern, actively maintained Tcl linter+formatter pair.
  Configurable via `tclint.toml`. Knobs include `indent` (number
  of spaces, supports `2`), `line-length`, `max-blank-lines`,
  `spaces-in-braces`. **Cannot encode** brace placement,
  required `then` keyword, test-framework idioms — these need
  a hand-rolled checker.
- **`nagelfar`** (Tcl, actively maintained) — a static analyzer
  and type-checker, not a formatter. Useful for catching
  semantic errors but does not enforce style.

### 22.2 Recommended deployment

A two-tool approach:

1. **`tclint` + `tclfmt`** for the mechanical rules (indent,
   line length, max blank lines, trailing whitespace).
   Install: `pipx install tclint`. Configure via `tclint.toml`
   at the project root.

2. **A project-specific `tclsh` checker** for the rules
   `tclfmt` cannot encode:

   - `then`-keyword presence on every `if`/`elseif`.
   - Brace style (`proc name { args } {` with the spaces).
   - Test-framework idioms (R-marker presence in TH8 test
     descriptions, `runTest { … }` wrapper).
   - File-header divider line (`#` run padded to column 79;
     §1.3.4).
   - `unset -nocomplain` in test `-cleanup` blocks.

   The checker should be ~150 lines of Tcl, modeled on
   `tools/audit_patterns.tcl` (which does similar work for C).

### 22.3 Suggested `tclint.toml`

```toml
[tool.tclint]
indent = 2
line-length = 80
max-blank-lines = 1
spaces-in-braces = false   # list/expr braces only; not proc { args }
exclude = [
  "externals/**",
  "bin/**",
  "**/*.b64sig",
]
```

Tune to taste. Run `tclfmt --check` in CI; `tclfmt --in-place`
to apply.

### 22.4 Hand-rolled checker (recommended structure)

A dedicated Tcl-script auditor (mirroring the `audit_patterns.tcl`
model used for C sources) is planned but not yet shipped.  Its
recommended structure:

```
- For each .tcl / .eagle file:
  - Verify file header (79 # divider, file -- line, copyright,
    license-pointer, RCS marker, closing divider).
  - Verify no tabs.
  - Verify no trailing whitespace.
  - For every `if` / `elseif`: verify `then` keyword present.
  - For every `expr`: verify it is braced.
  - For every `proc`: verify `{ args }` spacing.
  - In test files: verify every `runTest` has a description
    and at least one R-marker (TH8 only).
  - In test cleanup blocks: verify `unset` calls use
    `-nocomplain`.
- Exit non-zero if any violation found; print file:line context.
```

### 22.5 Editor settings

For consistency:

- **VS Code:** install `tclint` extension (LSP-based; uses the
  same `tclint.toml`).
- **`.editorconfig`** at project root:

  ```ini
  root = true

  [*.{tcl,eagle}]
  indent_style = space
  indent_size = 2
  trim_trailing_whitespace = true
  insert_final_newline = true

  # Eagle: CRLF; TH8: LF
  [*.eagle]
  end_of_line = crlf

  [*.tcl]
  end_of_line = lf
  ```

### 22.6 TH8 project-specific tooling

Tools that ship with the TH8 source tree under `tools/` and
that every contributor is expected to know about:

| Tool                     | Purpose                                                        |
|--------------------------|----------------------------------------------------------------|
| `tools/mkreq.tcl`        | Manage R-marker IDs in the standard.  Modes: `--verify` (check IDs match text), `--refresh` (recompute IDs from text), `--check-tests` (verify every R-marker is referenced by at least one test). |
| `tools/mkindex.tcl`      | Generate `tcl_language_standard_command_index.md` from the standard's Part III headings and the `src/plugins/th8_*.c` registration tables.  Modes: default (generate), `--check` (CI gate), `--verbose`. |
| `tools/mkstubs.tcl`      | Regenerate the public C stub table from `th8.h`.  Run by `make fresh`. |
| `tools/mkamal.tcl`       | Build the single-file amalgamation (`th8.c`).                  |
| `tools/mkversion.tcl`    | Stamp the project version number into header files.            |
| `tools/signScript.sh`    | Sign a `.tcl` / `.eagle` script with the active signing key, producing the `.b64sig` sidecar (see §20.7). |
| `tools/audit_patterns.tcl` | Static-analysis pass over the C source --- the canonical model for any hand-rolled `.tcl` checker (see §22.4). |
| `tools/format_scripts.tcl` | Reformat scripts to match the rules in this guide (work-in-progress; treat as advisory until §22.1 tooling matures). |
| `tools/reformat_tests.tcl` | One-shot rewriter that applies the unified test-file conventions (see §20.2 for the rescinded 4-space indent transition). |
| `tools/coverage.tcl`     | Run the coverage harness (MC/DC analysis pass).                 |
| `tools/tcl_detect.tcl`   | Probe for an installed `tclsh` (used by build scripts).         |

**Conventions for tools written in Tcl:**

- Every tool obeys this style guide --- they are themselves
  subject to the rules in §1 through §21.
- Tool-specific options use `--name` long form
  (e.g. `--check`, `--verbose`, `--output=PATH`); short forms
  are only used for `-h` / `--help`.
- Tools that produce derived files MUST support a `--check`
  mode that re-runs the generation and exits non-zero on
  drift, suitable as a CI gate.
- Tools that read or write paths under the project root
  derive the root from the script location:
  ```tcl
  proc resolve_root {} {
    set scriptDir [file dirname [file normalize [info script]]]
    return [file normalize [file join $scriptDir ..]]
  }
  ```
  Never hard-code an absolute path.

---

## 23. Eagle language semantics (Eagle vs. Tcl)

Eagle is **Tcl-compatible, not Tcl-identical**. It targets the **Tcl 8.4**
language baseline (`[info patchlevel]` → `8.4.x`) and runs on .NET's
number/format/culture engines, so several runtime behaviors differ from Tcl
8.5/8.6 *by design*. A script author must know these; the syntactic rules in
§1–§22 assume them.

The unifying principle is **strict-by-default, explicit opt-in**: Eagle does not
silently "do the helpful thing." Where Tcl 8.5+ added implicit magic, Eagle keeps
the strict behavior and offers an explicit escape hatch. Write the explicit form;
never rely on auto-magic. (Every example below was run against the Eagle shell —
per the project doctrine, *verify, don't recall*; §23.7.)

### 23.1 Booleans render as `True` / `False`

A boolean result prints `True`/`False`, **not** `1`/`0`:

```tcl
puts [expr {1 == 1}]   ;# => True   (NOT 1)
puts [expr {5 > 3}]    ;# => True
```

Test booleans by value (`[expr {$x == $y}]`, `string is boolean -strict`), never
by matching the literal text `1`/`0`. Convert explicitly when a script must emit
a machine-readable `1`/`0`.

### 23.2 No big-integer promotion — fixed 64-bit, explicit `entier()`

Integer arithmetic is fixed-width (64-bit) and **does not promote to a big
integer on overflow** (unlike Tcl 8.5+, which auto-promotes):

```tcl
puts [expr {2**63}]           ;# overflows within 64-bit -- NO bignum
puts [expr {entier(2)**63}]   ;# => 9223372036854775808  (explicit opt-in)
```

Use `entier(x)` to opt a value into arbitrary-precision arithmetic. The
non-promotion is **intentional**, not a bug — do not "fix" an overflow by
expecting promotion; add the explicit `entier()`.

### 23.3 Math errors: the `caught math exception:` format

Out-of-range or invalid math surfaces the underlying .NET exception, prefixed
`caught math exception:`. `int()` is **32-bit**, and `int()`/`wide()` **raise**
on an out-of-range double (they do not silently truncate):

```tcl
expr {int(1e300)}
;# => caught math exception: System.OverflowException: Value was either too
;#    large or too small for an Int32. ...
```

This full-exception diagnostic is the intended format. `catch` a computation that
may exceed range.

### 23.4 No variable auto-creation

`incr` and `dict set` do **not** create a missing variable — they error, by
design (an anti-footgun, mirroring the namespace "no creative writing" rule).
Note the contrast: `lappend` and `append` **do** auto-create, as in Tcl:

```tcl
incr noSuchVar    ;# => can't read "noSuchVar": no such variable
lappend other x   ;# => x   (lappend / append DO auto-create)
```

Initialize explicitly first (`set n 0; incr n`); never rely on an operation
conjuring the variable into existence.

### 23.5 `exec` does not error on a non-zero child exit

By default a child process exiting non-zero does **not** raise a Tcl error — a
`catch` around `exec` returns `0` even when the child exited non-zero. Check the
exit status explicitly (or the documented `exec` options) when it matters; do not
assume a non-zero exit propagates as a script error.

### 23.6 Tcl 8.4 is the baseline

Eagle targets **Tcl 8.4** compatibility. A post-8.4 (8.5/8.6+) feature being
absent, or behaving the 8.4 way, is **not a bug** — it is the baseline. Selected
8.5/8.6 features are adopted at the maintainers' discretion; do not assume a
post-8.4 feature is present. When a script must span versions, gate by
*capability* (`hasSubCommand`, feature probes; §18.8, §19) rather than by
guessing the version.

### 23.7 Verify, don't recall — and use a Tcl oracle

Because Eagle is neither "what Tcl does" nor "what .NET does," **run the snippet**
rather than trust memory — especially for `[expr]`, number formatting, `[clock]`,
`[format]`, and regex. Confirm the result in the Eagle shell; and when asserting a
*divergence from Tcl*, cross-check against a real Tcl **8.6** oracle (`tclsh8.6`)
and name the version. A behavior that matches Tcl **8.4** but not 8.6 is expected
(§23.6); a behavior that diverges from Tcl **8.4** is the real bug candidate.

---

## Appendix A — quick visual reference

```tcl
###############################################################################
#
# example.eagle --
#
# Project Description
# Component Description
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
# RCS: @(#) $Id: $
#
###############################################################################

#
# NOTE: Use our own namespace here because even though we do not directly
#       support namespaces ourselves, we do not want to pollute the global
#       namespace if this script actually ends up being evaluated in Tcl.
#
namespace eval ::Example {
  #
  # NOTE: This procedure returns one-line summary of what it does.
  #       Multiple-line description continues on aligned columns.
  #
  # <public>
  proc doSomething { name {default ""} } {
    # <help>
    # Returns the first known item whose name matches NAME (glob,
    # case-insensitive).  Returns DEFAULT if no item matches.
    # </help>

    # <<nonCaching>>

    if {[string length $name] == 0} then {
      return $default
    }

    set result [list]

    foreach item $::Example::knownItems {
      if {[string match -nocase $name $item]} then {
        lappend result $item
      }
    }

    return [expr {[llength $result] > 0 ? \
        [lindex $result 0] : $default}]
  }
}

###############################################################################
```

## Appendix B — quick test-file reference

```tcl
###############################################################################
#
# foo.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
# RCS: @(#) $Id: $
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- foo: basic operations
#
###############################################################################

runTest {test foo-1.1 {
  R-12345-67890: foo with valid input returns the expected value
} -setup {
  unset -nocomplain x
} -body {
  set x [foo 42]
  set x
} -cleanup {
  unset -nocomplain x
} -result {expected}}

###############################################################################

runTest {test foo-1.2 {
  R-12345-67891: foo with empty input returns empty result
} -setup {
  unset -nocomplain x
} -body {
  foo ""
} -cleanup {
  unset -nocomplain x
} -result {}}

###############################################################################

source tests/epilogue.tcl
```
