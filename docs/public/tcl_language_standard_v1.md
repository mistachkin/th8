# Tcl Language Standard

## Version 1.0

**Date:** 2026-07-03 (working text; revision history tracked
in the TH8 repository `CHANGELOG.md`)

### Document Status

This is version 1.0 of the Tcl Language Standard, its initial
normative edition.  Parts I through V are **normative**.  The
appendices are **informative**, except that where an appendix
restates a normative requirement by its identifier the underlying
requirement remains normative.  Every normative requirement carries
a stable `R-`*nnnnn*`-`*nnnnn* identifier (Appendix B) computed from
the requirement text; an identifier does not change between editions
unless the requirement's text itself changes, which allows
conformance claims and test references to remain valid across
revisions.

### Revision History

| Version | Date | Summary |
|---|---|---|
| 1.0 | 2026-07-03 | Initial normative edition: Parts I--V (Foundation, Language Core, Built-in Commands, Semantics, Conformance) and Appendices A (requirements cross-reference), B (identifier algorithm), C (deviations from Tcl 8.x), D (omnibus-TIP submission), E (formal grammar), F (cross-engine compatibility matrix). |

Fine-grained change tracking between working revisions is maintained
in the TH8 repository `CHANGELOG.md`; this table records only
published editions of the standard.

---

## Part I --- Foundation

### 1  Scope

This document specifies the Tcl scripting language as a formal
standard.  It defines the lexical structure, syntax, evaluation
semantics, data model, expression language, built-in commands,
variable system, list representation, namespace system, package
system, and error model of a conforming Tcl implementation.

The language defined herein targets the Tcl 8.4 surface syntax.
Extensions beyond Tcl 8.4 are limited to the evaluation engine
(non-recursive evaluation, tail call optimization), expression
operators (the `**` exponentiation operator), strict UTF-8 string
semantics, and additional procedure definition forms (`nproc`,
`napply`).

**Reference-version policy.**  Statements in this document that
contrast its requirements with prior Tcl behavior (e.g. the
deviation notes in §1, §5.1, §19.4, §19.5, §20.7, §20.8,
§22.2.1 and the index in Appendix C) refer specifically to
**canonical Tcl 8.x**.  Applicability to **Tcl 9.x** has *not*
been systematically verified for this revision of the standard;
where 9.x is known to differ from 8.x in a way that affects a
deviation note, that fact is called out per-section.  A future
revision of this standard MAY widen the scope of those notes
to cover Tcl 9.x once each one has been independently verified
against the Tcl 9.x reference implementation.

This specification defines **observable behavior only**.  It does
not prescribe implementation strategies, data structures, or
algorithms except where the observable behavior of list formatting
(Section 7) requires a specific canonical representation.

The reference implementation of this standard is **TH8**, an
embeddable interpreter.  Other conforming implementations may use
different internal architectures provided they produce identical
observable results for all normative requirements.

TH8 itself implements additional features beyond what this standard
specifies: a script security policy, a cryptographic API, a
C-level platform embedding API, fuzz/fault/debugging
infrastructure, pluggable backends, and a small set of
script-visible commands and `info` sub-commands that are not part
of canonical Tcl.  Those extensions are documented in the
companion *TH8 Language Extensions* (`th8_language_extensions.md`)
and are *not* required for conformance to this standard.

R-29508-16704
:   The maximum byte length of any string value is 100 MB (104,857,600 bytes).

**Deviation from Tcl 8.x:**  Tcl 8.x do not impose a
fixed maximum string length; the practical limit is determined by
available memory and the platform's `int` width (often 2 GB or
4 GB).  This standard imposes a hard 100 MB limit so that
implementations can use 32-bit length fields throughout the data
model and so that pathological inputs cannot exhaust memory.
Scripts that produce or consume strings larger than 100 MB must
be split into smaller chunks.  See Appendix C for the full
deviation index.


### 2  Normative References

The following documents are referred to in such a way that some
or all of their content constitutes requirements of this document.

-   ISO/IEC 9899:1990, *Programming languages --- C* (C89/C90)
-   ISO/IEC 10646, *Information technology --- Universal Coded
    Character Set (UCS)*
-   The Unicode Standard, Version 15.0 or later
-   IEEE 754-2008, *Standard for Floating-Point Arithmetic*
-   RFC 2119, *Key words for use in RFCs to Indicate Requirement
    Levels* (Bradner, 1997)


### 3  Terms and Definitions

For the purposes of this document, the following terms and
definitions apply.

**3.1  byte:**
An octet; an unsigned integer in the range 0 to 255 inclusive.

**3.2  code point:**
A value in the Unicode codespace; an integer in the range
U+0000 to U+10FFFF inclusive.

**3.3  character:**
A single Unicode code point.

**3.4  string:**
A finite, possibly empty, sequence of characters.

**3.5  word:**
A token produced by the lexical analyzer from the source text
of a command.  After substitution, a word is a string.

**3.6  command:**
A string consisting of one or more words separated by whitespace.
The first word names the command; remaining words are arguments.

**3.7  script:**
A string containing zero or more commands separated by newlines
or semicolons.

**3.8  list:**
A string whose content obeys the list formatting rules of
Section 7.  A list represents a finite ordered sequence of
elements, each of which is itself a string.

**3.9  variable:**
A named storage location holding a string value.  A variable
is either a **scalar** (holding one value) or an **array**
(holding a collection of values indexed by string keys called
**elements**).

**3.10  procedure:**
A user-defined command created by `proc` or `nproc`, consisting
of a name, a formal parameter list, and a body script.

**3.11  namespace:**
A named container for commands and variables.  Namespaces form
a tree rooted at the **global namespace** `::`.

**3.12  interpreter:**
The execution context in which scripts are evaluated.  An
interpreter contains a command table, a variable table, a
call stack, and associated state.

**3.13  return code:**
An integer value returned by every command invocation indicating
the disposition of the result.  The named return codes used in
this standard, with their integer values and the script
features they support, are:

| Value | Name        | Tied to                              | Status     |
|-------|-------------|--------------------------------------|------------|
| 0     | `ok`        | normal completion                    | universal  |
| 1     | `error`     | `error` command (§12.10), exceptions | universal  |
| 2     | `return`    | `return` command (§13.6)             | universal  |
| 3     | `break`     | `break` command (§12.6)              | universal  |
| 4     | `continue`  | `continue` command (§12.7)           | universal  |
| 5     | `return2`   | multi-level `return -level N` mechanism | reserved |
| 6     | `suspend`   | interpreter suspension (TH8 ext.)    | reserved (TH8) |
| 7     | `yield`     | coroutine yield                      | reserved   |
| 8     | `cleanup`   | post-evaluation cleanup phase        | reserved (TH8) |

Names appear in this standard *without* prefix (e.g. `ok`, not
`TCL_OK` or `TH8_OK`).  An implementation MAY expose
prefixed C-level constants of any spelling; the prefix is an
implementation choice, not a normative element.

Values 0--4 are common to canonical Tcl 8.x and to this
standard.  Values 5--8 are reserved by this standard for the
features listed above; a conforming implementation that does
not implement a given feature MAY leave the corresponding code
unused, but MUST NOT repurpose the value for an unrelated
mechanism.


### 4  Notation Conventions

**4.1  Requirement identifiers.**
Each normative requirement in this document carries an identifier
of the form **R-*nnnnn*-*nnnnn***, where each group is a five-digit
decimal number derived from the MD5 hash of the normalized
requirement text, following the algorithm described in Appendix B.
A requirement identifier changes whenever its text is modified;
this ensures that test-to-specification traceability is
self-verifying.

**4.2  Synopsis notation.**
In command synopses, *italic* denotes a metavariable (a
placeholder for an actual value).  Text enclosed in `?` marks
denotes an optional element.  An ellipsis `...` denotes
repetition of the preceding element.

**4.3  Normative language.**
This document uses normative language following RFC 2119.
**SHALL** and **SHALL NOT** indicate absolute requirements.
**MAY** indicates permitted but not required behavior.
All normative requirements carry R-marker identifiers.
Non-normative text (examples, rationale, implementation notes)
does not carry R-markers and is informative only.

---

## Part II --- Language Core

### 5  Lexical Structure

#### 5.1  Source Encoding

R-21609-56922
:   A conforming implementation SHALL interpret source text as a sequence of bytes encoded in UTF-8.
R-49723-00529
:   Byte sequences that are not valid UTF-8 SHALL be rejected at input boundaries.

**Deviation from Tcl 8.x:**  Tcl 8.x is permissive about
invalid UTF-8: it stores raw bytes and interprets them
opportunistically, which means malformed input silently becomes
malformed Tcl strings.  This standard requires strict UTF-8 with
rejection at input boundaries (`source`, channel reads, command-
line argument decoding, `[encoding convertfrom utf-8 ...]`).
Scripts that today rely on Tcl's lenient handling of byte
sequences that happen to be valid Latin-1 but invalid UTF-8 must
be re-encoded explicitly.  See Appendix C for the full deviation
index.

#### 5.2  Commands

R-20498-60370
:   A script consists of zero or more commands separated by newline characters or semicolons.
R-09662-29945
:   Each command consists of one or more words separated by whitespace (space or tab characters).
R-29611-36088
:   The first word of a command is the command name; remaining words are arguments passed to the command.

#### 5.3  Substitution Rules

Three forms of substitution are performed on command words before
the command is invoked:

R-58077-09618
:   A dollar sign `$` followed by a variable name triggers variable substitution: the variable's value replaces the `$` and the name.
R-01453-47388
:   An open bracket `[` triggers command substitution: the text up to the matching close bracket `]` is evaluated as a script and its result replaces the bracketed text.
R-60231-25699
:   A backslash `\` triggers backslash substitution according to the table in Section 5.5.
R-09092-04681
:   Substitution is performed exactly once on each word; the result of a substitution is not re-scanned.

#### 5.4  Quoting

R-25821-20104
:   A word enclosed in double quotes `"..."` undergoes variable, command, and backslash substitution, but whitespace within the quotes does not terminate the word.
R-20186-08766
:   A word enclosed in braces `{...}` undergoes no substitution; the content between the outermost matching braces is taken literally.
R-33512-08832
:   Braces nest: a left brace inside braced text SHALL have a matching right brace.

#### 5.5  Backslash Sequences

The following backslash sequences are recognized within
double-quoted words and unquoted words:

| Sequence | Replacement |
|----------|-------------|
| `\a` | Audible alert (U+0007) |
| `\b` | Backspace (U+0008) |
| `\f` | Form feed (U+000C) |
| `\n` | Newline (U+000A) |
| `\r` | Carriage return (U+000D) |
| `\t` | Horizontal tab (U+0009) |
| `\v` | Vertical tab (U+000B) |
| `\\` | Literal backslash |
| `\xHH` | Character with hexadecimal code HH |
| `\uHHHH` | Character with Unicode code point HHHH |
| `\ooo` | Character with octal code ooo (1--3 digits) |
| `\newline` | Space (line continuation) |

R-29551-28259
:   A backslash followed by a character not listed above SHALL produce that character literally.
R-57614-25424
:   A backslash immediately followed by a newline inside a braced expression is treated as whitespace (line continuation).

#### 5.6  Comments

R-61045-20811
:   A `#` character at the position where a command name is expected begins a comment that extends to the end of the line.
R-23450-38612
:   A `#` character in any other position has no special meaning.

#### 5.7  Argument Expansion

A word that begins with the literal prefix `{*}` is treated as an
**expansion word**: after substitution, the resulting string is
parsed as a list and each element of that list becomes a separate
argument to the command, in place of the single expansion word.

R-12647-27646
:   A word whose first three characters are the literal prefix
:   `{*}` SHALL be treated as an argument-expansion word: after
:   the rest of the word undergoes ordinary substitution, the
:   resulting string SHALL be parsed as a list and each element
:   of that list SHALL become a distinct argument to the
:   enclosing command, replacing the single expansion word.

R-49538-15893
:   When an argument-expansion word's substituted value parses as
:   the empty list (zero elements), the expansion word SHALL
:   contribute zero arguments to the command, which therefore
:   sees one fewer argument at that position than the source
:   text appears to specify.


### 6  Data Model

#### 6.1  Everything is a String

R-52995-63998
:   Every value in Tcl is a string.
R-16342-40509
:   There are no distinct integer, floating-point, boolean, or list types at the language level; all values are represented as strings and interpreted in context.

#### 6.2  Numeric Interpretation

R-33487-59232
:   A string that consists of optional leading whitespace, an optional sign (`+` or `-`), and a sequence of decimal digits is interpreted as an integer when used in a numeric context.
R-03620-44716
:   A string containing a decimal point or an exponent (`e` or `E`) is interpreted as a floating-point number when used in a numeric context.
R-08901-48972
:   Hexadecimal integers are indicated by the prefix `0x` or `0X`.
R-18737-56517
:   Octal integers are indicated by the prefix `0o` or `0O`.
R-44656-63369
:   Binary integers are indicated by the prefix `0b` or `0B`.

#### 6.3  Boolean Interpretation

R-00488-11343
:   The strings `0`, `false`, `no`, and `off` (case-insensitive) are boolean false.
R-02588-48107
:   The strings `1`, `true`, `yes`, and `on` (case-insensitive) are boolean true.
R-14317-25004
:   Any nonzero integer is boolean true.


### 7  List Representation

#### 7.1  Canonical List Format

R-50429-33312
:   A list is a string in which elements are separated by whitespace.
R-23074-38273
:   An element that contains whitespace, braces, double quotes, backslashes, or is the empty string SHALL be enclosed in braces in the canonical representation.
R-05534-35124
:   An empty list is represented by the empty string.

#### 7.2  List Parsing

R-45345-25968
:   The `llength`, `lindex`, `lrange`, and other list commands parse their input as a list by splitting on whitespace, respecting brace and quote grouping.
R-32728-09100
:   List operations SHALL correctly handle elements containing multi-byte UTF-8 sequences without corrupting the byte sequences or misidentifying list delimiters within continuation bytes.
R-03175-09390
:   When given a string that is not a well-formed list, such as a quoted or braced element immediately followed by a non-whitespace character, a list command SHALL raise a script error whose diagnostic message identifies the malformed element and is never empty.


### 8  Variable System

#### 8.1  Scalar Variables

R-45859-12418
:   A scalar variable holds a single string value.
R-27171-24066
:   A variable is created when it is first assigned a value.
R-07656-00984
:   Reading a variable that does not exist produces an error.

#### 8.2  Array Variables

R-24298-16451
:   An array variable holds a collection of string values indexed by string keys.
R-07480-05554
:   Array elements are accessed using the syntax `varName(key)`.
R-49650-33951
:   A variable is either a scalar or an array, never both simultaneously.

#### 8.3  Variable Resolution

R-08187-14580
:   An unqualified variable name is resolved in the current local scope (the innermost call frame).
R-29933-20236
:   A fully qualified variable name beginning with `::` is resolved relative to the global namespace.
R-44157-24939
:   A variable name containing `::` separators is resolved by namespace path.


### 9  Expression Language

#### 9.1  Operators

The `expr` command evaluates an expression string that supports
the following operators, listed from highest to lowest precedence:

| Precedence | Operators | Description |
|------------|-----------|-------------|
| 1 | `- + ~ !` | Unary minus, plus, bitwise NOT, logical NOT |
| 2 | `**` | Exponentiation (right-associative) |
| 3 | `* / %` | Multiply, divide, modulo |
| 4 | `+ -` | Add, subtract |
| 5 | `<< >>` | Left shift, right shift |
| 6 | `< > <= >=` | Numeric comparison |
| 7 | `== !=` | Numeric equality |
| 8 | `eq ne` | String equality |
| 9 | `&` | Bitwise AND |
| 10 | `^` | Bitwise XOR |
| 11 | `\|` | Bitwise OR |
| 12 | `&&` | Logical AND (short-circuit) |
| 13 | `\|\|` | Logical OR (short-circuit) |
| 14 | `? :` | Ternary conditional |

R-26525-02176
:   Parentheses override the default precedence.
R-56068-58014
:   The `&&` operator SHALL NOT evaluate its right operand if the left operand is false.
R-53262-03561
:   The `||` operator SHALL NOT evaluate its right operand if the left operand is true.

#### 9.2  Math Functions

R-63118-53739
:   The expression language provides the following core math functions: `abs`, `int`, `double`, `round`, `wide`, `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log10`, `pow`, `fmod`, `ceil`, `floor`, `max`, `min`, `pi`, `rand`, `random`, `srand`.

R-19502-53346
:   Math functions are dynamically registered per-interpreter and may be added, removed, or replaced by the embedder.

#### 9.2.1  Constants and Cryptographic Random

R-64737-26899
:   The `pi()` math function SHALL take no arguments and return the value of Pi to the maximum precision representable in an IEEE 754 64-bit double (at least 15 significant decimal digits).
R-07930-26550
:   The `pi()` math function SHALL return a value greater than 3.14159265358979 and less than 3.14159265358980.
R-56249-19402
:   The `random()` math function SHALL take no arguments and return a cryptographically random signed 64-bit integer obtained from the platform's cryptographic random source.
R-59209-30085
:   The `random()` math function SHALL raise a script error with the message "cryptographic random not available" if no cryptographic random source is available.
R-36032-44824
:   Successive calls to `random()` SHALL return independent values with no observable correlation.
R-22028-56793
:   The `random()` math function is distinct from `rand()`: `random()` returns a cryptographic 64-bit integer, while `rand()` returns a pseudo-random double in [0,1) from a seeded linear congruential generator.

R-30069-08069
:   The `epsilon()` math function SHALL take no arguments and return the IEEE 754 double-precision machine epsilon: the smallest positive double X such that `1.0 + X` is distinguishable from `1.0` under round-to-nearest-even.

R-06656-00761
:   The `epsilon()` math function SHALL return a positive value, and successive calls SHALL return identical values within a single interpreter lifetime.

#### 9.2.2  Type Introspection Functions

R-25277-59247
:   `typeof(x)` returns the type name of the value: `"int"`, `"wide"`, `"entier"` (bigint), `"double"`, or `"string"`.
R-04200-14573
:   `entier(x)` truncates a floating-point value toward zero and returns an integer.  When arbitrary precision integers are enabled, the result may be arbitrarily large.
R-04038-64345
:   `bool(x)` returns 1 if the value is non-zero, 0 otherwise.

#### 9.2.3  C99 Math Functions (TIP #745)

R-21400-10913
:   The following C99 math functions are available when the platform provides them: `acosh`, `asinh`, `atanh`, `cbrt`, `copysign`, `dim`, `erf`, `erfc`, `exp2`, `expm1`, `gamma`, `ldexp`, `lgamma`, `log1p`, `log2`, `logb`, `nextafter`, `remainder`, `signbit`, `trunc`.

R-48581-56034
:   The cbrt() math function SHALL return exact integer results for perfect cubes (e.g., cbrt(27.0) returns exactly 3.0).

#### 9.2.4  Float Classification Functions (TIP #521)

R-10825-53675
:   `isfinite(x)` returns 1 if the value is finite.
R-29218-03840
:   `isinf(x)` returns 1 if the value is infinite.
R-63393-12448
:   `isnan(x)` returns 1 if the value is Not-a-Number.
R-44368-58334
:   `isnormal(x)` returns 1 if the value is a normal floating-point number.
R-19401-43049
:   `issubnormal(x)` returns 1 if the value is subnormal (gradual underflow).
R-04041-22104
:   `isunordered(x, y)` returns 1 if either value is NaN.
R-33716-48519
:   `fpclassify(x)` returns one of: `"zero"`, `"subnormal"`, `"normal"`, `"infinite"`, `"nan"`.

#### 9.2.5  Math Function Introspection

R-13347-32372
:   `info functions ?pattern?` returns a list of registered math function names, optionally filtered by a glob pattern.

R-51583-32353
:   Division of an integer by zero SHALL produce the error message "divide by zero".
R-34938-53648
:   The modulo of an integer by zero SHALL produce the error message "divide by zero".

#### 9.3  Type Promotion

R-58455-48961
:   When an arithmetic operator has one integer operand and one floating-point operand, the integer is promoted to floating-point before the operation.
R-19715-59368
:   Integer arithmetic that would overflow the implementation's integer range SHALL produce an error when overflow checking is enabled.

### 10  Command Evaluation

#### 10.1  The Evaluation Loop

R-18921-24435
:   The interpreter evaluates a script by parsing it into commands and evaluating each command in sequence.
R-27915-59612
:   For each command, the interpreter performs substitutions on each word, looks up the command name in the command table, and invokes the command with the substituted arguments.
R-59159-45407
:   The result of a script is the result of the last command evaluated.

#### 10.2  Return Codes

R-34446-28679
:   Every command invocation produces a return code and a result string.
R-00216-15651
:   A return code of 0 (`ok`) indicates normal completion; the result string is the command's return value.
R-56291-06757
:   A return code of 1 (`error`) indicates an error; the result string is the error message.
R-26834-28099
:   A return code of 2 (`return`) causes the enclosing procedure to return.
R-28638-19737
:   A return code of 3 (`break`) terminates the enclosing loop.
R-27705-58825
:   A return code of 4 (`continue`) advances to the next iteration of the enclosing loop.
R-44834-04438
:   The integer value 5 (`return2`) is reserved for the multi-level return mechanism used by `return -level N` for N greater than 1.

R-05547-21491
:   The integer value 6 (`suspend`) is reserved for interpreter suspension; an implementation that supports suspension SHALL produce this code only via the suspension mechanism.

R-25241-13206
:   The integer value 7 (`yield`) is reserved for coroutine yield; an implementation that supports coroutines SHALL produce this code only via `yield` and `yieldto`.

R-39350-27659
:   The integer value 8 (`cleanup`) is reserved for the post-evaluation cleanup phase; an implementation that exposes a cleanup phase SHALL produce this code only via that phase.

R-05386-26202
:   Reserved return-code values (5 through 8) SHALL NOT be repurposed for unrelated mechanisms by a conforming implementation.


---

## Part III --- Built-in Commands

The following sections specify every command in the minimal
conformance set.  Each command description includes a synopsis,
a description of its behavior, and normative requirements that
a conforming implementation must satisfy.

Requirements are identified by markers of the form
**R-*nnnnn*-*nnnnn*** derived from the text of each requirement
(see Section 4.1 and Appendix B).


### 11  Variable Commands

#### 11.1  set

**Synopsis:** `set` *varName* ?*value*?

R-09798-25882
:   The `set` command with two arguments assigns the value of its second argument to the variable named by its first argument.
R-62364-23287
:   The `set` command with two arguments returns the assigned value.
R-06168-42171
:   The `set` command with one argument returns the current value of the variable named by its argument.
R-26988-54227
:   The `set` command with one argument applied to a nonexistent variable produces an error.
R-47069-65353
:   The `set` command overwrites any existing value when assigning.
R-37037-25374
:   The `set` command with an empty string value assigns the empty string.
R-56696-36447
:   The `set` command with a name of the form `varName(key)` accesses an array element.
R-28379-13828
:   The `set` command with zero arguments produces a "wrong # args" error.
R-51616-42806
:   The `set` command within a procedure accesses local variables unless the variable has been declared with `global`, `variable`, or `upvar`.
R-27266-50469
:   The `set` command with a namespace-qualified name resolves the variable in the specified namespace.

#### 11.2  unset

**Synopsis:** `unset` ?`-nocomplain`? ?`--`? ?*varName* ...?

R-58795-26380
:   The `unset` command removes each named variable from the current scope.
R-41556-65059
:   The `unset` command applied to a nonexistent variable produces an error.
R-60700-26967
:   The `unset` command with no arguments is a no-op and returns the empty string.
R-37085-57680
:   The `unset` command returns the empty string on success.
R-33222-17245
:   The `unset` command removes array elements when given a name of the form `varName(key)`.
R-22182-46738
:   The `unset` command with the `-nocomplain` option suppresses errors for nonexistent variables.
R-44601-21802
:   The `unset` command with the `--` option marks the end of options, allowing variable names that begin with a dash.
R-41523-35137
:   When `unset` encounters an error on one variable, subsequent variables in the argument list are not processed.
R-18157-17712
:   The `unset` command applied to an array name with no parenthesized index removes the entire array.

#### 11.3  append

**Synopsis:** `append` *varName* ?*value* ...?

R-06409-32483
:   The `append` command concatenates each value argument to the end of the variable named by its first argument.
R-29122-45410
:   The `append` command creates the variable with a value equal to the concatenation of its value arguments if the variable does not exist and at least one value argument is provided.
R-39847-37686
:   The `append` command returns the new value of the variable after appending.
R-41306-09002
:   The `append` command with no value arguments returns the current value of the variable.
R-13329-33713
:   The `append` command with no value arguments applied to a nonexistent variable produces an error.
R-23915-20716
:   The `append` command concatenates multiple value arguments in a single operation.

R-01955-43195
:   The `append` command SHALL raise a "wrong # args" error if invoked with no arguments (no varName).

#### 11.4  incr

**Synopsis:** `incr` *varName* ?*increment*?

R-07001-16527
:   The `incr` command adds the integer increment to the integer value of the named variable and stores the result.
R-09737-12855
:   The default increment is 1 when the increment argument is omitted.
R-20499-60888
:   The `incr` command returns the new value of the variable after incrementing.
R-42497-30621
:   The `incr` command creates the variable with an initial value of 0 if it does not exist, then applies the increment.
R-57202-38464
:   The `incr` command produces an error if the variable's current value is not an integer.
R-59857-46589
:   The `incr` command produces an error if the increment argument is not an integer.
R-08027-26429
:   The `incr` command accepts a negative increment, causing the variable's value to decrease.
R-29967-61212
:   The `incr` command with an increment of 0 validates that the variable contains an integer without changing it.

R-63413-21885
:   The `incr` command SHALL raise a "wrong # args" error if invoked with no arguments or more than two arguments.

#### 11.5  array

**Synopsis:** `array exists` *varName* | `array names` *varName*

R-02108-25225
:   The `array exists` command returns 1 if the named variable is an array, 0 otherwise.
R-34461-28486
:   The `array names` command returns a list of all element keys in the named array.
R-65179-47163
:   The `array get` command returns a list of alternating element name and value pairs from the named array.
R-09466-52892
:   The `array get` command with a pattern returns only elements whose names match the glob pattern.
R-20793-16099
:   The `array set` command takes a list of name-value pairs and assigns them as elements of the named array.
R-60323-35069
:   The `array set` command with an odd number of list elements produces an error.
R-28939-42058
:   The `array set` command with an empty list and a nonexistent variable creates an empty array.
R-12247-30649
:   The `array size` command returns the number of elements in the named array as a decimal string.
R-27362-34391
:   The `array size` command returns 0 if the variable is not an array.

R-53941-51133
:   The `array` command SHALL raise a script error with a message identifying the offending sub-command if invoked with an unrecognized sub-command.

R-45874-35574
:   The `array` command SHALL raise a "wrong # args" error if any sub-command is invoked with too few or too many arguments for that sub-command's documented synopsis.

R-07244-26691
:   The `array statistics arrayName` sub-command SHALL return a multi-line text summary describing the array's element-count and element-name / element-value byte usage.  The first line SHALL have the form `N entries`; the second line SHALL have the form `total element name bytes: B`; the third line SHALL have the form `total element value bytes: B`; the fourth line SHALL have the form `average element name length: F` (F formatted to two decimal places); the fifth line SHALL have the form `maximum element name length: M`.

R-00505-09565
:   The `array statistics` sub-command SHALL raise a script error if the named variable does not exist or is not an array, with a message of the form `"VARNAME" isn't an array`.

R-04772-16495
:   The `array startsearch arrayName` sub-command SHALL return a non-empty search-id string that identifies a fresh enumeration of the array's elements at the moment of the call.

R-52780-31332
:   Each successful `array startsearch` SHALL return a search-id distinct from every search-id ever previously returned within the same interpreter, until that interpreter is destroyed.

R-11358-43827
:   The `array startsearch` sub-command SHALL raise a script error if the named variable does not exist or is not an array, with a message of the form `"VARNAME" isn't an array`.

R-61853-11350
:   The `array nextelement arrayName searchId` sub-command SHALL return the name of the next element in the search and advance the search's internal cursor by one position.

R-38073-02154
:   The `array nextelement` sub-command SHALL return the empty string when the search is exhausted (no further elements remain) without raising an error.

R-59231-64546
:   The `array anymore arrayName searchId` sub-command SHALL return 1 if at least one element of the search has not yet been returned by `array nextelement`, and 0 otherwise.

R-40419-26190
:   The `array donesearch arrayName searchId` sub-command SHALL release the search-id and any state associated with it; subsequent `array nextelement`, `array anymore`, or `array donesearch` invocations on the same id SHALL raise a script error with a message of the form `couldn't find search "ID"`.

R-31734-18707
:   Mutating the array after `array startsearch` SHALL invalidate the search: any `set`, `unset`, `append`, `lappend`, `incr`, or other write to any element of the array between `array startsearch` and the matching `array donesearch` SHALL cause the next `array nextelement`, `array anymore`, or `array donesearch` call on that search to raise a script error with a message of the form `couldn't find search "ID"`.  This includes both adding or removing elements and modifying the value of an existing element.  Implementations MAY enforce this via a per-array mutation epoch captured at startsearch time and revalidated on every subsequent call; the script-visible behaviour is the same regardless of mechanism.  Read-only access to elements (`set varName(elem)` with no value, element-form `info exists`, `array get`, `array names`) SHALL NOT invalidate any pending search.

R-55410-62072
:   If the entire array is removed (e.g. via `unset arrayName` or `array unset arrayName`) while one or more searches on it are still pending, every pending search-id on that array SHALL be invalidated; subsequent `array nextelement`, `array anymore`, or `array donesearch` on those ids SHALL raise a script error with a message of the form `couldn't find search "ID"`.

R-15876-06247
:   A search-id passed to `array nextelement`, `array anymore`, or `array donesearch` together with an `arrayName` argument that differs from the array name used at `array startsearch` time SHALL be treated as if the search were unknown, raising a script error with a message of the form `couldn't find search "ID"`.

> **Rationale (informative).**  Live-bucket iteration with
> mutation-detected invalidation (rather than snapshotting
> the element-name list at `startsearch` time) is the whole
> reason the search API exists: snapshotting a multi-million
> entry array's keys defeats the purpose of using the search
> API instead of `[array names]`.  The mutation rule covers
> *any* element-level write — not just adds and removes —
> so test authors do not have to reason about whether a value
> rewrite is "structural enough" to invalidate.  The
> equivalence "if the search-walk would observe anything
> different, the search is broken" is simpler to teach than
> the "structural mutations only" carve-out.  This differs
> from Tcl 8.x, which silently allowed elements added after
> `startsearch` to "leak" into the iteration; TH8 considers
> that semantics a foot-gun.  Code that wants to mutate
> elements during a walk should snapshot the keys first via
> `[array names]` and iterate that list; for most modern
> code, that idiom is preferred over the search API anyway.

#### 11.6  global

**Synopsis:** `global` *varName* ?*varName* ...?

R-64783-55603
:   The `global` command creates a link in the current local scope to the named variable in the global namespace.
R-38546-20769
:   After `global`, reading or writing the local name accesses the global variable.

#### 11.7  upvar

**Synopsis:** `upvar` ?*level*? *otherVar* *myVar* ?*otherVar* *myVar* ...?

R-24299-23982
:   The `upvar` command creates a link between a local variable and a variable in another call frame.
R-17744-44957
:   For `upvar`, the default level is 1, meaning the caller's frame.
R-35760-03398
:   After `upvar`, reading or writing the local name accesses the linked variable in the specified frame.
R-51028-29323
:   The `upvar` command with level `#0` links to a variable in the global frame.
R-20965-07472
:   The `upvar` command returns the empty string.
R-43175-24686
:   The referenced variable need not exist at the time `upvar` is called; it is created when first written through the link.
R-32384-24962
:   The `upvar` command accepts absolute level specifiers of the form `#N`, with `#0` linking directly to a global variable.

R-36062-65259
:   The `upvar` command SHALL raise a script error if the level specifier is malformed (not a non-negative integer or a `#N` form).

R-48068-03373
:   The `upvar` command SHALL raise a script error if the requested call frame does not exist (e.g. a relative level deeper than the current call stack).

R-49140-50744
:   The `upvar` command SHALL raise a "wrong # args" error if invoked without at least one *otherVar* / *myVar* pair.

#### 11.8  variable

**Synopsis:** `variable` *varName* ?*value*? ?*varName* *value* ...?

R-51486-54706
:   The `variable` command declares a namespace variable in the current namespace.
R-63785-46872
:   If a value is provided, the namespace variable is initialized to that value.
R-30531-28192
:   Inside a procedure, `variable` creates a local link to the namespace variable, similar to `global`.
R-19756-08248
:   The [variable] command with a fully qualified namespace name (e.g., variable ::foo::x) SHALL create a local link using the tail portion of the name, matching Tcl 8.x behavior.

R-35010-38419
:   The `variable` command SHALL raise a "wrong # args" error if invoked with no arguments.

**Note:** `variable` is the *only* command that creates persistent
namespace variables.  The `set` command inside `namespace eval` creates
ephemeral frame-local variables (see Section 22.2.1).  This is an
intentional deviation from Tcl 8.x.  Code that requires persistent
namespace state must use `variable`, not `set`.

#### 11.8a  array unset

**Synopsis:** `array unset` *arrayName* ?*pattern*?

R-27569-39885
:   The `array unset` command removes elements from an array whose names match the given glob pattern.
R-59091-42737
:   If no pattern is given, the entire array is removed.


### 12  Control Flow Commands

#### 12.1  if

**Synopsis:** `if` *expr* ?`then`? *body* ?`elseif` *expr* ?`then`? *body*? ... ?`else` *body*?

R-06371-03281
:   The `if` command evaluates its first expression argument as a boolean expression.
R-23357-48390
:   If the expression is true, the corresponding body script is evaluated and its result is returned.
R-31074-26008
:   If the expression is false and an `elseif` clause follows, its expression is evaluated in the same manner.
R-04753-00601
:   If no expression is true and an `else` clause is present, its body is evaluated and its result is returned.
R-55373-08648
:   If no branch is executed, the `if` command returns the empty string.
R-08087-37238
:   The `then` keyword is optional and has no effect on behavior.
R-21109-56507
:   The `if` command evaluates at most one body script per invocation.

R-13333-10367
:   The `if` command SHALL raise a script error if any of its expression arguments cannot be parsed or evaluated as a boolean expression (per Section 9).

R-41570-44797
:   The `if` command SHALL raise a "wrong # args" error if invoked with no expression, with an expression but no body, or with a malformed `elseif`/`else` sequence.

#### 12.2  for

**Synopsis:** `for` *init* *test* *incr* *body*

R-40199-07933
:   The `for` command evaluates the init script once, then repeatedly evaluates the test expression; while it is true, it evaluates the body script followed by the incr script.
R-61272-28849
:   The `for` command returns the empty string.
R-20728-60242
:   A `break` command within the body of `for` terminates the loop.
R-61603-53175
:   When a `break` command terminates a `for` loop, the interpreter result is cleared to the empty string.
R-27601-55226
:   A `continue` command within the body skips to the next evaluation of the incr script.

R-42270-20566
:   The `for` command SHALL raise a "wrong # args" error if invoked with anything other than exactly four arguments.

R-10011-46713
:   The `for` command SHALL raise a script error if its test argument cannot be parsed or evaluated as a boolean expression.

#### 12.3  while

**Synopsis:** `while` *test* *body*

R-01450-54795
:   The `while` command repeatedly evaluates the test expression; while it is true, it evaluates the body script.
R-53705-09312
:   The `while` command returns the empty string.
R-27074-56801
:   A `break` command within the body of `while` terminates the loop.
R-63526-16514
:   When a `break` command terminates a `while` loop, the interpreter result is cleared to the empty string.
R-13074-61812
:   A `continue` command within the body skips to the next evaluation of the test expression.

R-17588-51303
:   The `while` command SHALL raise a "wrong # args" error if invoked with anything other than exactly two arguments.

R-45076-44818
:   The `while` command SHALL raise a script error if its test argument cannot be parsed or evaluated as a boolean expression.

#### 12.4  foreach

**Synopsis:** `foreach` *varList* *list* *body*

R-62928-07465
:   The `foreach` command iterates over the elements of list, assigning groups of elements to the variables named in varList, and evaluates body for each group.
R-16096-42004
:   When the number of remaining list elements is less than the number of variables in varList, the excess variables are set to the empty string for that iteration.
R-02310-09257
:   The `foreach` command returns the empty string.
R-33606-19387
:   A `break` command within the body of `foreach` terminates the loop.
R-03611-50931
:   When a `break` command terminates a `foreach` loop, the interpreter result is cleared to the empty string.
R-15926-59145
:   A `continue` command within the body skips to the next iteration.
R-34479-61035
:   The loop variable retains its last assigned value after the loop completes.
R-55480-36320
:   The `foreach` command with multiple varlist-list pairs iterates until all lists are exhausted.

R-47437-61299
:   The `foreach` command SHALL raise a script error if any varList argument cannot be parsed as a well-formed list.

R-33502-47121
:   The `foreach` command SHALL raise a script error if any list argument cannot be parsed as a well-formed list.

R-59410-38369
:   The `foreach` command SHALL raise a "wrong # args" error if invoked with fewer than three arguments or with an even total number of arguments (the body counts; the varlist/list pairs must each have exactly two arguments).

#### 12.5  switch

**Synopsis:** `switch` ?*options*? *string* *pattern* *body* ... | `switch` ?*options*? *string* `{`*pattern* *body* ...`}`

R-38225-33858
:   The `switch` command compares its string argument against each pattern and evaluates the body associated with the first match.
R-10596-01795
:   The default matching mode is exact string comparison.
R-29227-22697
:   The `-glob` option selects glob-style pattern matching.
R-60822-28178
:   The `-exact` option explicitly selects exact matching.
R-25901-40000
:   The `--` option marks the end of options.
R-39721-05226
:   A body of `-` causes fall-through to the next body.
R-22775-57830
:   If no pattern matches, the `switch` command returns the empty string.
R-43818-50387
:   The `switch` command with the `-regexp` option uses regular expression matching.
R-42835-45040
:   The `default` pattern in a `switch` command matches any string.

R-50019-14903
:   The `switch` command SHALL raise a script error if the pattern/body sequence has an odd number of elements (every pattern must have a paired body).

R-24164-01315
:   The `switch` command SHALL raise a script error if an unrecognized option is given, with a message identifying the offending option.

R-09582-00259
:   The `switch` command SHALL raise a "wrong # args" error if invoked with fewer than three arguments after option processing.

#### 12.6  break

**Synopsis:** `break ?string?`

R-27348-35115
:   The `break` command causes the innermost enclosing loop (`for`, `while`, or `foreach`) to terminate immediately.

R-09845-29637
:   The `break` command SHALL accept an optional `string` argument; when `string` is supplied it becomes the interpreter result, which is observable by a `catch` that traps the break directly, while an enclosing loop discards it and still yields the empty string.

R-45025-64498
:   The `break` command SHALL raise a "wrong # args" error if invoked with more than one argument.

#### 12.7  continue

**Synopsis:** `continue ?string?`

R-46740-18674
:   The `continue` command causes the innermost enclosing loop to skip to its next iteration.

R-20389-43665
:   The `continue` command SHALL accept an optional `string` argument; when `string` is supplied it becomes the interpreter result, which is observable by a `catch` that traps the continue directly, while an enclosing loop discards it.

R-32518-10683
:   The `continue` command SHALL raise a "wrong # args" error if invoked with more than one argument.

#### 12.8  return

**Synopsis:** `return` ?`-code` *code*? ?*value*?

R-55092-57883
:   The `return` command causes the enclosing procedure to return with the specified value.
R-50620-47870
:   The default value is the empty string when no value argument is given.
R-34816-21014
:   The `-code` option specifies the return code; the default is 0 (TCL_OK).
R-35585-00247
:   The `return` command with `-code error` causes the procedure to behave as if it executed the `error` command.
R-36134-44560
:   The `return` command with `-errorinfo` sets the initial stack trace for the `errorInfo` variable.
R-42915-22081
:   The `return` command with `-errorcode` sets the value of the `errorCode` variable.
R-03654-57637
:   The `-errorinfo` and `-errorcode` options are ignored unless the return code is error.
R-32746-05995
:   The `-code` option accepts the symbolic names `ok`, `error`, `return`, `break`, and `continue` in addition to integer codes.

R-31825-54791
:   The `return` command SHALL raise a script error if `-code` is given a value that is neither one of the symbolic names listed in R-32746-05995 nor a valid integer.

R-29682-29334
:   The `return` command SHALL raise a script error if an unrecognized option is given.

#### 12.9  catch

**Synopsis:** `catch` *script* ?*varName*?

R-56831-47631
:   The `catch` command evaluates its script argument and returns the return code as an integer result.
R-31903-30952
:   If a variable name is given, the result string of the script is stored in that variable.
R-59093-15883
:   The `catch` command always returns TCL_OK (0) as its own return code.
R-63972-27573
:   The `catch` command returns 2 when the script executes a `return` command.
R-07360-42944
:   The `catch` command returns 3 when the script executes a `break` command.
R-44545-30306
:   The `catch` command returns 4 when the script executes a `continue` command.

R-62052-44647
:   The `catch` command SHALL raise a "wrong # args" error if invoked with no arguments or more than two arguments.

#### 12.10  error

**Synopsis:** `error` *message* ?*info*? ?*code*?

R-12040-05089
:   The `error` command produces an error with the given message as the result string.
R-29669-65193
:   If the info argument is provided, it is used as the initial value of the `errorInfo` variable.
R-13453-54978
:   If the code argument is provided, it is stored in the `errorCode` variable.

R-63560-50908
:   The `error` command SHALL raise a "wrong # args" error if invoked with no arguments or more than three arguments.

#### 12.11  eval

**Synopsis:** `eval` *arg* ?*arg* ...?

R-10747-51512
:   The `eval` command concatenates its arguments with spaces and evaluates the result as a script.
R-42027-41333
:   The `eval` command returns the result of the evaluated script.
R-27941-12603
:   The `eval` command concatenates its arguments in the same fashion as the `concat` command.

R-56151-09139
:   The `eval` command SHALL raise a "wrong # args" error if invoked with no arguments.

R-62831-00280
:   Any error raised by the evaluated script SHALL propagate out of `eval` with the script's return code, message, errorInfo and errorCode.

#### 12.12  exit

**Synopsis:** `exit` ?*code*?

R-64643-23093
:   The `exit` command sets the interpreter exit flag, causing all subsequent command evaluations to fail immediately until the host clears the flag.
R-48831-15594
:   If a *code* argument is provided, it SHALL be an integer; it is emitted to standard error but does not affect the process exit code.
R-00642-28910
:   The `exit` command clears the interpreter result before setting the exit flag.

R-07524-13714
:   The `exit` command SHALL raise a script error if its optional code argument is provided and is not parseable as a valid integer.

R-48222-15046
:   The `exit` command SHALL raise a "wrong # args" error if invoked with more than one argument.

#### 12.13  concat

**Synopsis:** `concat` ?*arg* ...?

R-56570-29705
:   The `concat` command trims leading and trailing whitespace from each argument, then joins the trimmed arguments with single spaces.
R-17771-43627
:   With no arguments, `concat` returns the empty string.
R-28016-42813
:   Interior whitespace within each argument is not modified by `concat`.
R-58370-15660
:   Arguments that are empty after trimming are silently omitted from the result.


#### 12.14  subst

**Synopsis:** `subst` ?*-nobackslashes*? ?*-nocommands*? ?*-novariables*? *string*

R-00961-13881
:   The `subst` command performs Tcl-style substitutions on *string* and returns the result.
R-45467-07298
:   By default, `subst` performs variable substitution, command substitution, and backslash substitution.
R-24221-26585
:   The `-nobackslashes` flag disables backslash substitution; escape sequences such as `\n` are left as-is.
R-36255-16813
:   The `-nocommands` flag disables command substitution; brackets `[...]` are treated as ordinary characters.
R-29914-10062
:   The `-novariables` flag disables variable substitution; `$varname` is left as literal text.
R-54337-10665
:   Unlike the normal Tcl parser, braces have no special quoting effect inside `subst`; substitution occurs within brace-delimited text.
R-09985-04199
:   If a command substitution within `subst` raises an error, the error propagates out of `subst`.
R-54301-42546
:   If a command substitution returns the `break` exception code, substitution stops immediately; the result is everything substituted up to that point.
R-43408-18524
:   If a command substitution returns the `continue` exception code, that command substitution is replaced with the empty string and substitution continues.
R-39231-13955
:   If a command substitution returns a value via `return`, the returned value is substituted in place and processing continues.

R-57050-61537
:   The `subst` command SHALL raise a "wrong # args" error if invoked with no arguments after option processing.

R-00379-06117
:   The `subst` command SHALL raise a script error if an unrecognized option is given, with a message identifying the offending option.

#### 12.15  coroutine

**Synopsis:** `coroutine` *name* *command* ?*arg ...*?

R-02272-62636
:   The `coroutine` command creates a new coroutine with the given name that evaluates the command.
R-00874-30590
:   The coroutine name becomes a command that, when invoked, resumes the coroutine with a value.
R-48842-57962
:   When the coroutine body completes, the coroutine command is automatically deleted.

R-18099-47953
:   A coroutine body MAY create and resume other coroutines; resuming an
:   inner coroutine SHALL NOT terminate the enclosing coroutine, which MAY
:   itself subsequently `yield` to its own caller after the inner
:   coroutine yields or completes.

R-52153-14373
:   The `coroutine` command SHALL raise a "wrong # args" error if invoked with fewer than two arguments (a name and a command).

R-34122-15052
:   The `coroutine` command SHALL raise a script error if a command with the given name already exists.

#### 12.16  yield

**Synopsis:** `yield` ?*value*?

R-23592-30941
:   The `yield` command suspends the current coroutine and returns the given value to the caller.
R-50081-23615
:   `yield` is an error if called outside a coroutine body.
R-33215-21255
:   When the coroutine is resumed, `yield` returns the value passed by the resume call.

R-36214-28434
:   The `yield` command SHALL raise a "wrong # args" error if invoked with more than one argument.

#### 12.17  try

**Synopsis:** `try` *script* ?`finally` *script*?

R-29919-42617
:   The `try` command SHALL evaluate its body script and preserve the return code and result.
R-51881-64256
:   When a `finally` clause is present, the `try` command SHALL evaluate the finally script after the body script completes, regardless of whether the body succeeded or failed.
R-29352-51924
:   If the finally script completes successfully, the `try` command SHALL return the body script's return code and result.
R-60969-27299
:   If the finally script fails, the `try` command SHALL return the finally script's return code and result, overriding the body's outcome.
R-32581-44432
:   The finally script SHALL be temporarily exempt from a pending script cancellation: the cancel state SHALL be saved before the finally script and restored after it completes.
R-05041-46440
:   The finally script SHALL NOT be evaluated if the interpreter's exit flag is set.
R-60341-22718
:   The finally script SHALL receive a fresh memory allocation budget equal to the interpreter's allocation limit, so that cleanup code can allocate memory even if the body exhausted the budget.
R-42434-29835
:   The `try` command SHALL store the finally script's return code and result in dedicated interpreter fields accessible via the C API.
R-02591-65336
:   The `try` command without a `finally` clause SHALL behave identically to `eval`.
R-12502-54202
:   If the finally script itself is canceled during execution, that cancellation SHALL take effect normally after the finally script completes.

R-20573-49781
:   The `try` command SHALL raise a "wrong # args" error if invoked with anything other than one argument or three arguments where the second is the literal `finally` keyword.

**Design rationale.**  This is the purest form of `try`/`finally`,
adapted from Eagle.  Unlike Tcl 8.6's `try`, there is no `catch`,
`on`, or `trap` clause — only the unconditional `finally` block.

The omission of `catch` clauses is intentional: `[catch]` is a
separate, well-tested command.  Conflating error handling and
resource cleanup in one command creates complexity that obscures
both concerns.  With TH8's `try`, the pattern is explicit:

```
catch {
    try {
        # ... risky operation ...
    } finally {
        # ... guaranteed cleanup ...
    }
} msg
```

**Security implications:**

-   **Cancel exemption.**  The finally block is exempt from a *pending*
    cancellation, not from *all* cancellations.  If the finally block
    is itself canceled during execution, that fresh cancellation takes
    effect normally.  The save/restore mechanism ensures that a SIGINT
    arriving during the body does not prevent cleanup from running.

-   **Exit flag check.**  If the exit flag is set, the interpreter is
    in a terminal state and the finally block is skipped.  Running
    cleanup code after exit could interact with partially torn-down
    state.

-   **Memory budget.**  The finally block receives a fresh allocation
    budget equal to the interpreter's memory limit.  This ensures
    cleanup can allocate memory (for error messages, logging, etc.)
    even if the body exhausted the budget.  The total maximum
    memory during a finally block is 2× the configured limit.

-   **NRE safety.**  Both the body and the finally script are
    evaluated via the non-recursive eval engine, ensuring deep
    scripts do not overflow the C stack.

### 13  Procedure Commands

#### 13.1  proc

**Synopsis:** `proc` *name* *argList* *body*

R-33690-08330
:   The `proc` command creates a new command with the given name that, when invoked, evaluates body in a new local scope.
R-32995-09838
:   Each element of argList names a formal parameter.
R-54322-24989
:   A two-element list `{name default}` in argList specifies a parameter with a default value.
R-54924-11430
:   The special parameter name `args` collects all remaining arguments into a list.
R-51060-55401
:   Invoking a procedure with too few or too many arguments produces an error, unless `args` is present.
R-19079-49744
:   When a procedure body completes without an explicit `return` command, the procedure returns the result of the last command executed in the body.

#### 13.2  nproc

**Synopsis:** `nproc` *name* *argList* *body*

R-64857-12453
:   The `nproc` command creates a procedure where every parameter is bound by name at the call site, not by position.
R-56848-64742
:   Each parameter in argList is declared as either *paramName* (required) or `{` *paramName* *defaultValue* `}` (optional with a default), where *paramName* is any identifier; a leading hyphen is conventional but not interpreted specially by the runtime.
R-22328-03699
:   At the call site, arguments are passed as alternating *paramName* *value* pairs, where each *paramName* matches a declared parameter identifier exactly, including any hyphen prefix.
R-21440-63588
:   Named arguments may appear in any order at the call site; the interpreter binds each value to its corresponding parameter by name, not by position.

#### 13.3  apply

**Synopsis:** `apply` *func* ?*arg* ...?

R-36957-50481
:   The `apply` command evaluates a lambda expression, where func is a two-element list `{argList body}`.
R-49466-26821
:   The argument binding follows the same rules as `proc`.

#### 13.4  napply

**Synopsis:** `napply` *func* ?*arg* ...?

R-29574-18521
:   The `napply` command evaluates a lambda expression using named argument binding, following `nproc` rules.

#### 13.5  rename

**Synopsis:** `rename` *oldName* *newName*

R-30634-62834
:   The `rename` command changes the name of an existing command.
R-58228-24850
:   If newName is the empty string, the command is deleted.
R-35801-03855
:   Renaming a nonexistent command produces an error.
R-44541-15485
:   If a command is deleted (renamed to the empty string) while script code is executing, the command SHALL be removed from the namespace immediately (preventing further invocation) and the delete callback and memory free SHALL be deferred until the script evaluation stack fully unwinds.

#### 13.6  tailcall

**Synopsis:** `tailcall` *command* ?*arg* ...?

R-01034-50775
:   The `tailcall` command replaces the current procedure invocation with the specified command, evaluated in the caller's scope.
R-15545-60396
:   The current procedure's local variables are released before the new command executes.
R-04566-42664
:   The `tailcall` command is an error if invoked outside a procedure or lambda body.

#### 13.7  downlevel

**Synopsis:** `downlevel` *script*

R-24561-62433
:   The `downlevel` command evaluates script in the call frame that existed prior to the most recent `uplevel`.

#### 13.8  uplevel

**Synopsis:** `uplevel` ?*level*? *script*

R-00011-09866
:   The `uplevel` command evaluates script in the call frame identified by level.
R-17399-64558
:   For `uplevel`, the default level is 1, meaning the caller's frame.
R-05612-10993
:   Level `#0` refers to the global frame.
R-39968-16588
:   The `uplevel` command accepts absolute level specifiers of the form `#N` where N is a stack level number, with `#0` referring to the global scope.


### 14  List Commands

#### 14.1  list

**Synopsis:** `list` ?*arg* ...?

R-05434-27940
:   The `list` command creates a properly formatted Tcl list from its arguments, applying quoting as needed.
R-12427-49975
:   The `list` command with no arguments returns the empty string.
R-11387-53455
:   Each argument becomes exactly one element of the resulting list regardless of its content.

#### 14.2  lindex

**Synopsis:** `lindex` *list* *index*

R-62854-48784
:   The `lindex` command returns the element of list at the given index.
R-37667-43203
:   The first element has index 0.
R-57862-26416
:   The index `end` refers to the last element.
R-07541-34977
:   The index `end-N` refers to the element N positions before the last.
R-13187-19003
:   For `lindex`, an index outside the valid range returns the empty string.
R-32094-10782
:   The `lindex` command with no index argument returns the list unchanged.
R-43980-53018
:   The `lindex` command accepts multiple index arguments for nested list indexing, where each successive index drills into the element selected by the previous index.

R-17493-11882
:   The `lindex` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

R-30852-38658
:   The `lindex` command SHALL raise a script error if any index argument is not a valid integer expression or one of the forms `end`, `end-N`, or `end+N`.

> **Rationale (informative).**  Indexing into a malformed list
> is undefined; an out-of-range index is well-defined (returns
> the empty string per R-13187-19003) but a syntactic defect in
> the list itself is not.  An invalid index expression is a
> programmer error and SHALL fail loudly rather than silently
> defaulting to 0 or `end`.

#### 14.3  lrange

**Synopsis:** `lrange` *list* *first* *last*

R-20123-18594
:   The `lrange` command returns a list consisting of elements first through last (inclusive) of the input list.
R-14342-04254
:   The indices `end` and `end-N` are supported.
R-43443-37988
:   If first is greater than last, the result is the empty string.
R-44569-25869
:   The `lrange` command treats a first index less than zero as zero.
R-35094-63625
:   The `lrange` command treats a last index beyond the end of the list as the last element.

R-56799-24156
:   The `lrange` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

R-59028-43635
:   The `lrange` command SHALL raise a script error if either index argument is not a valid integer expression or one of the forms `end`, `end-N`, or `end+N`.

#### 14.4  lreplace

**Synopsis:** `lreplace` *list* *first* *last* ?*element* ...?

R-07529-35170
:   The `lreplace` command returns a new list formed by replacing elements first through last with the given replacement elements.
R-49118-09608
:   If no replacement elements are given, the specified range is deleted.
R-03155-13217
:   If first is greater than last, the replacement elements are inserted before position first.
R-19062-07944
:   The `lreplace` command with more replacement elements than the range being replaced increases the length of the list.
R-28645-27103
:   The `lreplace` command with fewer replacement elements than the range being replaced decreases the length of the list.

R-49701-23068
:   The `lreplace` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

R-39623-53505
:   The `lreplace` command SHALL raise a script error if either of the first or last index arguments is not a valid integer expression or one of the forms `end`, `end-N`, or `end+N`.

#### 14.5  lsearch

**Synopsis:** `lsearch` ?*options*? *list* *pattern*

R-33422-41481
:   The `lsearch` command returns the index of the first element in list that matches pattern using glob-style matching.
R-03526-22310
:   The `lsearch` command returns -1 if no element matches.
R-65407-60482
:   The `lsearch` command uses the same matching rules as `string match` for its default glob mode.
R-38095-62274
:   The `lsearch` command with `-exact` performs exact string comparison instead of glob matching.
R-55396-44593
:   The `lsearch` command with `-all` returns a list of all matching indices instead of the first.
R-01660-06229
:   The `lsearch` command with `-inline` returns matching values instead of indices.
R-37219-60333
:   The `lsearch` command with `-not` negates the match sense.
R-13428-50077
:   The `lsearch` command with `-start N` begins searching at index N.
R-12193-55604
:   The `lsearch` command with `-regexp` performs regular expression matching.
R-36657-11143
:   The `-sorted` option performs a binary search, assuming the list is already sorted in the appropriate order.

R-28142-11585
:   The `lsearch` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

R-21893-49079
:   The `lsearch` command SHALL raise a script error if an unrecognized option is given, with a message identifying the offending option.

R-00062-01135
:   The `lsearch` command SHALL raise a script error if `-start N` is given with N that is not a valid integer expression or one of the forms `end`, `end-N`, or `end+N`.

#### 14.6  lsort

**Synopsis:** `lsort` ?*options*? *list*

R-30185-38802
:   The `lsort` command returns a new list with the elements of list sorted according to the given options.
R-12088-02535
:   The default sort mode is `-ascii` (lexicographic).
R-53425-17573
:   The `-integer` option sorts by integer value.
R-09875-38623
:   The `-real` option sorts by floating-point value.
R-40622-36745
:   The `-increasing` option (default) sorts in ascending order.
R-39269-21617
:   The `-decreasing` option sorts in descending order.
R-53256-47651
:   The `-unique` option removes duplicate elements after sorting.
R-56935-37465
:   The `lsort` command provides a stable sort: elements that compare equal retain their original relative order.
R-21407-05967
:   The `-command` option specifies a comparison command that is called with two list elements and SHALL return an integer less than, equal to, or greater than zero.
R-34299-16437
:   The `-index` option causes each element to be treated as a list, and the sort key is the sub-element at the given index.
R-12054-28168
:   The `-dictionary` option uses dictionary-style comparison: case-insensitive with embedded integers compared numerically.

R-29556-41653
:   The `lsort` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

R-00115-36867
:   The `lsort` command SHALL raise a script error if an unrecognized option is given, with a message identifying the offending option.

R-63220-57491
:   The `lsort` command SHALL raise a script error if a `-command` callback returns a non-integer value, returns an error, or returns a value not in {-1, 0, 1, or any other integer with the documented sign semantics}.

R-40934-00017
:   The `lsort` command SHALL raise a script error if `-integer` or `-real` is requested and any list element is not parseable in the requested numeric mode.

#### 14.7  lappend

**Synopsis:** `lappend` *varName* ?*value* ...?

R-41293-44424
:   The `lappend` command appends each value argument as a new element to the list stored in the named variable.
R-37687-02473
:   If the variable does not exist, it is created as an empty list before appending.
R-58254-03308
:   The `lappend` command returns the new value of the variable.
R-45401-22079
:   The `lappend` command with no value arguments returns the current value of the variable, creating it as an empty string if it does not exist.

R-18890-36310
:   The `lappend` command SHALL raise a script error if the existing value of the named variable cannot be parsed as a well-formed list.

> **Rationale (informative).**  `lappend` extends an existing
> list value with new elements; it cannot do so coherently when
> the existing value is structurally malformed.  The error
> surfaces the latent corruption rather than silently producing
> a value that is itself malformed.

#### 14.8  llength

**Synopsis:** `llength` *list*

R-48562-48787
:   The `llength` command returns the number of elements in list as a decimal integer string.

R-36165-17591
:   The `llength` command SHALL raise a script error if its sole argument cannot be parsed as a well-formed list, with a message identifying the structural defect (e.g. "Unmatched braces:", "Unmatched brackets:", "Unmatched quote:").

R-61105-51996
:   The `llength` command SHALL raise a "wrong # args" error if invoked with a number of arguments other than exactly one.

> **Rationale (informative).**  A length is undefined for a
> value that does not parse as a list.  `llength` therefore
> chooses the strict-error path rather than returning a count
> of partially-parsed elements; silent partial parsing would
> mask data-corruption bugs at the call site.
>
> *See also:* style guide §8.5 (existence checks) and the
> "Defensive idioms" section of the style guide for
> pre-validation patterns such as `[string is list -strict]`
> when the caller wants to handle malformed input without an
> exception.

#### 14.9  join

**Synopsis:** `join` *list* ?*separator*?

R-07716-29030
:   The `join` command returns a string formed by concatenating the elements of list with separator between each pair.
R-09813-27218
:   The default separator is a single space.

R-44497-19699
:   The `join` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

#### 14.10  split

**Synopsis:** `split` *string* ?*splitChars*?

R-18165-20346
:   The `split` command returns a list created by splitting string at each character found in splitChars.
R-34801-30956
:   The default split characters are space, tab, and newline.
R-49049-40006
:   An empty splitChars string causes each character to become a separate list element.
R-05599-50617
:   Adjacent split characters produce empty-string elements.
R-22346-05431
:   Splitting an empty string returns the empty string.
R-25333-10244
:   The `split` command treats the splitChars argument as a set of individual characters, not as a substring.
R-38622-32405
:   The `split` command with a delimiter at the beginning of the string produces a leading empty element.
R-01699-22478
:   The `split` command with a delimiter at the end of the string produces a trailing empty element.


#### 14.12  lreverse

**Synopsis:** `lreverse` *list*

R-03143-05122
:   The `lreverse` command takes a single list argument and returns a new list with the elements in reverse order.
R-20941-53492
:   `lreverse` with an empty list returns an empty string.
R-18329-05882
:   `lreverse` preserves the internal structure of nested list elements.

R-51717-49000
:   The `lreverse` command SHALL raise a script error if its argument cannot be parsed as a well-formed list.

#### 14.13  lremove

**Synopsis:** `lremove` *list* ?*index* ...?

R-05764-04130
:   The `lremove` command removes elements from a list by index, returning the resulting list.
R-57118-58718
:   `lremove` accepts index arguments that may be integers, `end`, or `end-N`.
R-12457-39560
:   Out-of-range or duplicate indices in `lremove` are silently ignored.
R-64017-07410
:   `lremove` with no index arguments returns the original list unchanged.

R-29529-17693
:   The `lremove` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

#### 14.14  lassign

**Synopsis:** `lassign` *list* ?*varName* ...?

R-35860-32063
:   The `lassign` command assigns successive list elements to the named variables.
R-54553-37675
:   If the list has fewer elements than variables, remaining variables are set to the empty string.
R-48926-20099
:   The `lassign` command returns a list of elements that were not assigned to any variable.
R-61728-16131
:   `lassign` with no variable names returns the original list.

R-57099-14041
:   The `lassign` command SHALL raise a script error if the list argument cannot be parsed as a well-formed list.

#### 14.15  dict

**Synopsis:** `dict` *subcommand* ?*arg* ...?

The `dict` command provides operations on dictionary values (even-length key-value lists).

R-44992-17786
:   The `dict` command preserves the insertion order of keys.

##### 14.15.1  dict create

R-17869-41009
:   `dict create` with key-value arguments returns a dictionary with those entries.
R-22739-46547
:   `dict create` with no arguments returns an empty dictionary.
R-43180-64455
:   `dict create` with an odd number of arguments is an error.

##### 14.15.2  dict get

R-33307-13601
:   `dict get` with a single key returns the value associated with that key.
R-27642-25795
:   `dict get` with multiple keys performs nested key traversal into sub-dictionaries.
R-02371-22586
:   `dict get` with a key that does not exist is an error.

##### 14.15.3  dict set

R-10262-03615
:   `dict set` stores a value in a dictionary variable at the given key.
R-37114-11559
:   `dict set` with multiple keys performs nested key traversal, creating intermediate dictionaries as needed.
R-29570-08733
:   If the dictionary variable does not exist, `dict set` creates it.

##### 14.15.4  dict unset

R-10572-25216
:   `dict unset` removes a key-value pair from a dictionary variable.
R-03732-31421
:   `dict unset` with multiple keys performs nested key traversal.
R-30666-01387
:   `dict unset` of a non-existent key silently succeeds.

##### 14.15.5  dict exists

R-04929-09801
:   `dict exists` returns 1 if the specified key path exists in the dictionary, 0 otherwise.
R-23027-59510
:   `dict exists` with nested keys traverses sub-dictionaries.
R-61636-45436
:   `dict exists` never raises an error for missing keys or malformed sub-dictionaries.

##### 14.15.6  dict keys

R-19857-58268
:   `dict keys` returns a list of all keys in the dictionary.
R-09119-24154
:   `dict keys` with a glob pattern returns only keys matching the pattern.

##### 14.15.7  dict values

R-21802-14758
:   `dict values` returns a list of all values in the dictionary.
R-01218-14976
:   `dict values` with a glob pattern returns only values matching the pattern.

##### 14.15.8  dict size

R-29709-06850
:   `dict size` returns the number of key-value pairs in the dictionary.

##### 14.15.9  dict remove

R-59382-21479
:   `dict remove` returns a new dictionary with the specified keys removed.

##### 14.15.10  dict replace

R-14524-29134
:   `dict replace` returns a new dictionary with specified key-value pairs replaced or added.

##### 14.15.11  dict merge

R-24769-65001
:   `dict merge` combines multiple dictionaries, with later values overriding earlier ones for duplicate keys.
R-25319-64569
:   `dict merge` with no arguments returns an empty dictionary.

##### 14.15.12  dict filter

R-56151-04074
:   `dict filter` with `key` glob pattern returns entries whose keys match the pattern.
R-59639-33915
:   `dict filter` with `value` glob pattern returns entries whose values match the pattern.
R-47676-13385
:   `dict filter` with `script` evaluates an expression for each entry, keeping those where it returns true.

##### 14.15.13  dict append

R-04288-56669
:   `dict append` concatenates strings onto the value of a key in a dictionary variable.
R-28867-58888
:   If the key does not exist, `dict append` creates it with the concatenated strings as the value.

##### 14.15.14  dict lappend

R-50914-32512
:   `dict lappend` list-appends values onto the value of a key in a dictionary variable.

##### 14.15.15  dict incr

R-32167-11311
:   `dict incr` increments the integer value of a key in a dictionary variable.
R-43364-15119
:   `dict incr` with a non-existent key creates the key with the increment as its value.
R-03007-09001
:   `dict incr` with no increment argument uses 1.

##### 14.15.16  dict for

R-57123-15315
:   `dict for` iterates over key-value pairs, evaluating a body script for each.
R-03369-11783
:   `dict for` supports `break` and `continue` within the body.
R-26792-30421
:   `dict for` with an empty dictionary does not execute the body.

##### 14.15.17  dict map

R-10464-48599
:   `dict map` iterates over key-value pairs, collecting the results of evaluating the body script into a new dictionary.
R-44839-33014
:   The body result alternates as key-value pairs in the output dictionary.

##### 14.15.18  dict update

R-62849-08308
:   `dict update` extracts specified keys into local variables, evaluates a body, then writes the variables back.

##### 14.15.19  dict with

R-62515-51147
:   `dict with` extracts all keys into local variables named after the keys, evaluates a body, then writes back.

##### 14.15.20  dict info

R-64284-56368
:   `dict info` returns a human-readable string describing the dictionary's internal representation.

#### 14.16  info default

See Section 21 (Introspection Commands).


### 15  String Commands

All string commands are subcommands of the `string` ensemble.

#### 15.1  string compare

**Synopsis:** `string compare` ?`-nocase`? ?`-length` *N*? *string1* *string2*

R-42418-26915
:   The `string compare` command compares two strings lexicographically and returns -1, 0, or 1.
R-16156-64588
:   The return value is 0 if the strings are equal, -1 if string1 sorts before string2, and 1 if string1 sorts after string2.
R-41690-44412
:   The `string compare` command with the `-nocase` option performs case-insensitive comparison.
R-55448-42983
:   The `string compare` command with the `-length` option restricts comparison to the first N characters.
R-27142-24249
:   The `string compare` command with the `-nocase` option folds ASCII uppercase to lowercase before comparing.
R-15142-04104
:   The `string compare` command with the `-length` option compares at most the first N characters of each string.
R-49734-20318
:   The `string compare` command with `-length 0` considers all strings equal.

#### 15.1a  string equal

**Synopsis:** `string equal` ?`-nocase`? ?`-length` *N*? *string1* *string2*

R-46584-30958
:   The `string equal` command returns 1 if the strings are equal, 0 otherwise.
R-25119-35336
:   The `string equal` command with `-nocase` performs case-insensitive comparison.
R-29243-18547
:   The `string equal` command with `-length N` compares only the first N characters.

#### 15.2  string first / string last

**Synopsis:** `string first` *needle* *haystack* ?*startIndex*? | `string last` *needle* *haystack* ?*startIndex*?

R-04230-52791
:   The `string first` command returns the index of the first occurrence of needle in haystack, or -1 if not found.
R-02521-52297
:   The `string last` command returns the index of the last occurrence of needle in haystack, or -1 if not found.
R-03141-61401
:   The `string first` command with a startIndex argument begins the search at the specified position.

#### 15.3  string index

**Synopsis:** `string index` *string* *index*

R-29414-04053
:   The `string index` command returns the character at the given index in string.
R-23190-27431
:   For `string index`, an index outside the valid range returns the empty string.
R-40313-14181
:   The index `end` refers to the last character.

#### 15.4  string range

**Synopsis:** `string range` *string* *first* *last*

R-07615-63304
:   The `string range` command returns the substring from index first to index last, inclusive.

#### 15.5  string length

**Synopsis:** `string length` *string*

R-54796-49566
:   The `string length` command returns the number of characters in string.
R-27926-63046
:   The `string length` of an empty string is 0.

#### 15.6  string repeat

**Synopsis:** `string repeat` *string* *count*

R-63308-24468
:   The `string repeat` command returns the result of concatenating string with itself count times.
R-59360-18389
:   A count of 0 returns the empty string.

#### 15.7  string match

**Synopsis:** `string match` *pattern* *string*

R-61454-64199
:   The `string match` command returns 1 if string matches the glob pattern, 0 otherwise.
R-14747-06481
:   The `*` pattern character matches any sequence of characters.
R-38300-13377
:   The `?` pattern character matches any single character.
R-43744-48634
:   A bracketed range `[chars]` matches any single character in the set.
R-16347-20268
:   A range within brackets `[a-z]` matches any character in the inclusive range.
R-00941-13743
:   The `string match` command with the `-nocase` option performs case-insensitive matching.
R-45728-58943
:   The `string match` pattern SHALL match the entire string, not just a substring.
R-19250-20590
:   The `string match` command with a backslash in the pattern matches the following character literally.

#### 15.8  string is

**Synopsis:** `string is` *class* ?`-strict`? *string*

R-26556-03981
:   The `string is` command returns 1 if every character in string belongs to the named class, 0 otherwise.
R-20242-47754
:   The supported classes include: `integer`, `double`, `alpha`, `digit`, `alnum`, `space`, `list`, `ascii`, `boolean`, `true`, `false`, `tainted`.
R-12751-19518
:   Without `-strict`, an empty string is considered valid for any class (returns 1).  With `-strict`, an empty string returns 0.
R-52560-48849
:   The `ascii` class returns 1 if every byte in string has a value in the range 0--127.
R-04129-32285
:   The `boolean` class returns 1 if string is a valid boolean value (true, false, yes, no, on, off, 0, 1).
R-42813-48603
:   The `true` class returns 1 if string is a valid boolean AND its boolean value is true.
R-20823-56724
:   The `false` class returns 1 if string is a valid boolean AND its boolean value is false.

#### 15.8a  string replace

**Synopsis:** `string replace` *string* *first* *last* ?*newString*?

R-34028-21505
:   The `string replace` command returns a copy of string with the characters from first to last replaced by newString.
R-63394-43490
:   If newString is omitted, the characters in the range are simply deleted.

#### 15.8b  string totitle

**Synopsis:** `string totitle` *string* ?*first*? ?*last*?

R-21634-12986
:   The `string totitle` command converts the first character of string to uppercase and the remaining characters to lowercase.
R-40657-41865
:   If first and last are given, only the characters in that range are affected; characters outside the range are left unchanged.

#### 15.8c  string wordend

**Synopsis:** `string wordend` *string* *index*

R-16185-02807
:   The `string wordend` command returns the index of the character just after the last character of the word containing the character at index.

#### 15.8d  string wordstart

**Synopsis:** `string wordstart` *string* *index*

R-54004-17470
:   The `string wordstart` command returns the index of the first character of the word containing the character at index.

#### 15.8e  string bytelength

**Synopsis:** `string bytelength` *string*

R-56057-18552
:   The `string bytelength` command returns the number of bytes used to represent string in UTF-8.

#### 15.8f  string reverse

**Synopsis:** `string reverse` *string*

R-32232-36774
:   The `string reverse` command returns a string with all characters in reverse order.

#### 15.9  string map

**Synopsis:** `string map` *mapping* *string*

R-34313-48141
:   The `string map` command applies the key-value pairs in mapping to string, replacing each occurrence of a key with its corresponding value.
R-35519-31735
:   The mapping is a list of an even number of elements: key1 value1 key2 value2 and so on.
R-50014-43354
:   The `string map` command with the `-nocase` option performs case-insensitive key matching.
R-01692-49711
:   The `string map` command scans the string once from left to right; after a match and replacement, scanning continues after the replacement, not from the beginning.
R-43508-65179
:   The `string map` command with an empty mapping list returns the original string unchanged.
R-49362-08621
:   The `-nocase` option causes key matching in `string map` to be case-insensitive.

#### 15.10  string tolower / string toupper

**Synopsis:** `string tolower` *string* ?*first*? ?*last*? | `string toupper` *string* ?*first*? ?*last*?

R-24333-26171
:   The `string tolower` command returns a copy of string with all characters converted to lowercase.
R-23460-14939
:   The `string toupper` command returns a copy of string with all characters converted to uppercase.
R-32165-05446
:   The `string tolower` command with first and last arguments converts only characters in the specified range.
R-02055-02875
:   The `string toupper` command with first and last arguments converts only characters in the specified range.

#### 15.11  string trim / string trimleft / string trimright

**Synopsis:** `string trim` *string* ?*chars*? | `string trimleft` *string* ?*chars*? | `string trimright` *string* ?*chars*?

R-47333-52710
:   The `string trim` command returns string with all leading and trailing characters found in chars removed.
R-26884-20829
:   The `string trimleft` command removes only leading characters.
R-52896-29531
:   The `string trimright` command removes only trailing characters.
R-05938-07453
:   The default characters to trim are whitespace (space, tab, newline, carriage return).


### 16  Regular Expression Commands

The regular expression engine implements Advanced Regular Expressions
(ARE), a superset of POSIX Extended Regular Expressions (ERE).  The
engine operates in `REG_ADVANCED` mode by default.

#### 16.1  Pattern Syntax --- Atoms and Quantifiers

R-33468-32177
:   The dot `.` matches any single character except newline (in linestop mode).
R-34777-25939
:   A bracket expression `[chars]` matches any one of the enclosed characters.
R-64253-04052
:   A negated bracket expression `[^chars]` matches any character not in the enclosed set.
R-31107-57725
:   The caret `^` matches the beginning of the string (or line in lineanchor mode).
R-51380-33368
:   The dollar `$` matches the end of the string (or line in lineanchor mode).
R-06400-59604
:   Parentheses `(re)` define a capturing group.
R-41718-25558
:   Non-capturing parentheses `(?:re)` group without capturing.
R-61274-09385
:   The `*` quantifier matches zero or more times (greedy).
R-07873-34500
:   The `+` quantifier matches one or more times (greedy).
R-52041-26691
:   The `?` quantifier matches zero or one time (greedy).
R-28969-19748
:   The `{n}` quantifier matches exactly n times.
R-29967-02128
:   The `{n,}` quantifier matches n or more times.
R-20758-24731
:   The `{n,m}` quantifier matches at least n and at most m times.
R-47559-26507
:   Appending `?` to any quantifier makes it non-greedy (matches as few characters as possible).
R-54246-17008
:   The alternation operator `|` matches the left or right alternative.

#### 16.2  Pattern Syntax --- Character Classes

R-04954-51251
:   The POSIX class `[:digit:]` matches decimal digits inside a bracket expression.
R-58577-10255
:   The POSIX class `[:alpha:]` matches alphabetic characters inside a bracket expression.
R-02272-51548
:   The POSIX class `[:alnum:]` matches alphanumeric characters inside a bracket expression.
R-49612-39968
:   The POSIX class `[:space:]` matches whitespace characters inside a bracket expression.
R-13282-57984
:   The POSIX class `[:upper:]` matches uppercase letters inside a bracket expression.
R-27381-46764
:   The POSIX class `[:lower:]` matches lowercase letters inside a bracket expression.
R-49340-27957
:   A range `a-z` inside a bracket expression matches any character in the range.
R-47123-17767
:   The escape `\d` matches any digit (equivalent to `[[:digit:]]`).
R-24262-61017
:   The escape `\s` matches any whitespace character (equivalent to `[[:space:]]`).
R-62736-24340
:   The escape `\w` matches any word character (equivalent to `[[:alnum:]_]`).
R-18473-15349
:   The escapes `\D`, `\S`, `\W` match the complement of `\d`, `\s`, `\w` respectively.

#### 16.3  Pattern Syntax --- Escapes and Constraints

R-57379-18020
:   The escape `\n` matches a newline character within a pattern.
R-00196-22132
:   The escape `\t` matches a horizontal tab character within a pattern.
R-04473-65202
:   The escape `\xhh` matches the character with the given hexadecimal code.
R-32401-59222
:   The constraint `\y` matches at a word boundary.
R-05573-61453
:   The constraint `\A` matches only at the start of the string.
R-16169-01932
:   The constraint `\Z` matches only at the end of the string.
R-06227-52955
:   The constraint `\m` matches at the beginning of a word.
R-29873-37784
:   The constraint `\M` matches at the end of a word.
R-32448-61263
:   A back-reference `\N` (where N is 1-9) matches the same text as the Nth capturing group.

#### 16.4  Pattern Syntax --- Lookaround

R-35267-17023
:   A positive look-ahead `(?=re)` asserts that re matches at the current position without consuming characters.
R-62481-14132
:   A negative look-ahead `(?!re)` asserts that re does not match at the current position.
R-41708-30134
:   A positive look-behind `(?<=re)` asserts that re matches immediately before the current position.
R-45746-40475
:   A negative look-behind `(?<!re)` asserts that re does not match immediately before the current position.

#### 16.5  Pattern Syntax --- Embedded Options

R-26729-05107
:   The embedded option `(?i)` enables case-insensitive matching for the remainder of the pattern.
R-37892-07426
:   The embedded option `(?x)` enables expanded mode where unescaped whitespace and `#` comments are ignored.

#### 16.6  regexp Command

**Synopsis:** `regexp` ?*switches*? *pattern* *string* ?*matchVar*? ?*subVar* ...?

R-10050-09986
:   The `regexp` command matches string against the regular expression pattern.
R-38092-61134
:   With no match variables, `regexp` returns 1 if the pattern matches and 0 otherwise.
R-31950-35276
:   If match variables are provided, the first receives the entire matched substring and subsequent variables receive captured subexpressions.
R-05796-03008
:   The `-nocase` switch makes the regexp match case-insensitive.
R-19399-61281
:   The `-all` switch causes regexp to find all non-overlapping matches, returning the count.
R-17691-40453
:   The `-indices` switch causes regexp to store start and end index pairs instead of matched text.
R-51773-07357
:   The `-inline` switch causes regexp to return match results as a list instead of storing in variables.
R-23678-64539
:   The `-expanded` switch enables expanded mode (whitespace and `#` comments ignored in pattern).
R-02077-01666
:   The `-line` switch enables both `-linestop` and `-lineanchor`.
R-44438-56877
:   The `-linestop` switch prevents `.` and `[^...]` from matching newline characters.
R-59417-42036
:   The `-lineanchor` switch makes `^` and `$` match after and before embedded newlines.
R-47378-21594
:   The `-start N` switch begins matching at byte offset N in the string.
R-14943-11718
:   The `--` switch marks the end of switches.

#### 16.7  regsub Command

**Synopsis:** `regsub` ?*switches*? *pattern* *string* *replacement* ?*varName*?

R-60287-61395
:   The `regsub` command substitutes the first match of pattern in string with replacement and stores the result in varName, returning the number of substitutions.
R-52185-28363
:   If no variable is given, the modified string is returned directly.
R-12785-54861
:   The `-all` switch causes regsub to substitute all non-overlapping matches.
R-37530-51413
:   The `-nocase` switch makes the regsub match case-insensitive.
R-49932-20637
:   In the replacement string, `&` refers to the entire matched text.
R-22922-29165
:   In the replacement string, `\N` (where N is 0-9) refers to the Nth captured subexpression; `\0` is equivalent to `&`.
R-30346-01991
:   The `-line` switch enables newline-sensitive matching in regsub.
### 17  Expression Command

#### 17.1  expr

**Synopsis:** `expr` *arg* ?*arg* ...?

R-46538-24607
:   The `expr` command concatenates its arguments with spaces and evaluates the result as an expression according to the rules of Section 9.
R-57807-21393
:   The `expr` command returns the result of the expression as a string.
R-11223-03145
:   An integer expression result is returned as a decimal integer string.
R-23909-62226
:   A floating-point expression result is returned as a string with the minimum number of digits needed to uniquely identify the value.
R-55151-16878
:   Integer operands with a leading `0` are interpreted as octal.
R-10236-64979
:   Integer operands with a leading `0x` are interpreted as hexadecimal.
R-02187-29178
:   Integer division rounds toward negative infinity (floor division).
R-49019-45026
:   The remainder operator `%` always produces a result with the same sign as the divisor.
R-33950-35991
:   The `&&` and `||` operators produce integer results 0 or 1.
R-19076-33990
:   The `expr` command supports the ternary conditional operator `x ? y : z` which evaluates y if x is true, z otherwise.
R-14522-60989
:   The `expr` command supports bitwise operators: `&` (and), `|` (or), `^` (xor), `<<` (left shift), `>>` (right shift), and `~` (bitwise complement).
R-39874-12230
:   The `expr` command supports `0b` prefix for binary integer literals and `0o` prefix for octal integer literals.

#### 17.2  fpclassify

**Synopsis:** `fpclassify` *value*

R-63096-35181
:   The `fpclassify` command SHALL classify *value* as a floating-point number and return one of the strings `"zero"`, `"subnormal"`, `"normal"`, `"infinite"`, or `"nan"`.
R-42165-31603
:   `fpclassify` is the standalone command form of the `fpclassify(x)` math function specified in §9.2.4; both forms SHALL produce identical results for the same *value*.
R-43092-20230
:   The `fpclassify` command with any argument count other than exactly one SHALL produce a wrong-number-of-arguments error.


### 18  Formatting Commands

#### 18.1  format

**Synopsis:** `format` *formatString* ?*arg* ...?

R-55206-47918
:   The `format` command produces a formatted string according to a format string containing conversion specifiers in the style of ANSI C `sprintf`.
R-02380-17814
:   The `%d` and `%i` specifiers format an integer in decimal.
R-16079-32511
:   The `%u` specifier formats an unsigned integer in decimal.
R-63435-44549
:   The `%o` specifier formats an integer in octal.
R-29851-05339
:   The `%x` and `%X` specifiers format an integer in hexadecimal.
R-58954-60169
:   The `%s` specifier formats a string value.
R-06602-58148
:   The `%c` specifier formats an integer as the character with that code point.
R-54170-43783
:   The `%f` specifier formats a floating-point number in fixed-point notation.
R-20004-22166
:   The `%e` and `%E` specifiers format a floating-point number in scientific notation.
R-51311-13182
:   The `%g` and `%G` specifiers format a floating-point number in either fixed-point or scientific notation, whichever is shorter.
R-51483-24977
:   A precision specifier `.N` controls the number of digits after the decimal point for `%f`, `%e`, and `%g`.
R-08455-55100
:   The `%%` specifier produces a literal percent character.
R-30604-44538
:   The `format` command with a `-` flag left-justifies the output within its field width.
R-57537-52532
:   The `format` command with a `+` flag always includes a sign character for numeric conversions.
R-56711-61056
:   The `format` command with a `0` flag pads numeric output with leading zeros instead of spaces.
R-03770-38577
:   The `format` command with a `#` flag with `%o` ensures the first digit is always 0.
R-14169-30646
:   The `format` command with a `#` flag with `%x` or `%X` prepends `0x` or `0X` to nonzero values.
R-61231-38333
:   The `format` command with a field width specified as `*` takes the width from the next argument.
R-46212-64941
:   The `format` command with insufficient arguments for its conversion specifiers produces an error.

#### 18.2  scan

**Synopsis:** `scan` *string* *format* ?*varName* ...?

R-03334-00938
:   The `scan` command parses string according to the format string and stores the results in the named variables, returning the number of conversions performed.
R-27422-38271
:   If no variable names are given, `scan` returns a list of the converted values.
R-56814-07430
:   The `%d` specifier scans a decimal integer.
R-47387-33797
:   The `%x` specifier scans a hexadecimal integer, recognizing the `0x` prefix.
R-33929-04283
:   The `%o` specifier scans an octal integer.
R-38513-56453
:   The `%s` specifier scans a non-whitespace string.
R-30567-08335
:   The `%c` specifier scans a single character and returns its code point as an integer.
R-17663-64974
:   The `%f` specifier scans a floating-point number.
R-27160-02205
:   The `%n` specifier stores the number of characters consumed so far.
R-08988-52516
:   The `scan` command with a `*` flag after `%` discards the converted value without assigning it.
R-08104-06557
:   The `scan` command returns -1 when the end of input is reached before any conversions are performed.
R-60691-08269
:   The `scan` command with no variable names returns the converted values as a list.
R-18495-56959
:   A whitespace character in the scan format matches any number of whitespace characters in the input, including zero.


#### 18.3  base64

**Synopsis:** `base64 encode` *string* | `base64 decode` *string*

R-13975-09984
:   The `base64 encode` subcommand encodes *string* using the standard RFC 4648 Base64 alphabet (A-Z, a-z, 0-9, +, /) with `=` padding, and returns the encoded string.
R-53274-40319
:   The `base64 encode` subcommand inserts CRLF line endings every 76 characters per RFC 2045 (MIME).  No trailing line ending is appended after the final group.
R-27573-45023
:   The `base64 decode` subcommand decodes a Base64-encoded *string* and returns the original data.
R-40774-56899
:   The `base64 decode` subcommand silently ignores whitespace characters (space, tab, newline, carriage return) in the input.
R-10516-34894
:   The `base64 decode` subcommand returns an error if the input contains characters outside the Base64 alphabet and whitespace.
R-06825-47962
:   The `base64` command is 8-bit clean: encoding and decoding preserve all 256 possible byte values, including NUL bytes and values above 127.


#### 18.4  binary

**Synopsis:** `binary format` *formatString* ?*arg arg ...*? | `binary scan` *binaryData formatString* ?*varName varName ...*?

R-20661-36254
:   The `binary format` subcommand consumes a *formatString* and zero or more value arguments and returns a byte sequence built by packing each value according to the corresponding specifier.

R-61607-09342
:   The `binary scan` subcommand consumes a *binaryData* byte sequence and a *formatString* and assigns the values extracted from *binaryData* to the named variables, returning the number of variables that were successfully assigned.

R-06885-20942
:   Specifier letters in the format string are case-significant: lowercase forms denote little-endian byte order, uppercase forms denote big-endian byte order, and selected lowercase forms (`t`, `n`, `m`) denote the host's native byte order.

R-45855-01725
:   An integer specifier may be followed by a non-negative decimal count.  A count of `N` denotes `N` consecutive values (for `binary format`) or `N` consecutive readings into the next `N` variables (for `binary scan`).

R-46440-49876
:   Whitespace characters in the format string are ignored and may be used freely for readability.

R-25063-08152
:   The `c` specifier denotes an 8-bit integer field.

R-42966-06721
:   The `s` specifier denotes a 16-bit integer field in little-endian byte order.

R-42592-48705
:   The `S` specifier denotes a 16-bit integer field in big-endian byte order.

R-15892-10663
:   The `t` specifier denotes a 16-bit integer field in the host's native byte order.

R-54981-36508
:   The `i` specifier denotes a 32-bit integer field in little-endian byte order.

R-50117-49851
:   The `I` specifier denotes a 32-bit integer field in big-endian byte order.

R-64038-44860
:   The `n` specifier denotes a 32-bit integer field in the host's native byte order.

R-26067-44132
:   The `w` specifier denotes a 64-bit integer field in little-endian byte order.

R-37793-17393
:   The `W` specifier denotes a 64-bit integer field in big-endian byte order.

R-17918-60828
:   The `m` specifier denotes a 64-bit integer field in the host's native byte order.

R-31495-52668
:   An integer value passed to `binary format` is truncated to the field width modulo `2^N` where N is the field width in bits.

R-61115-28808
:   An integer value read by `binary scan` is sign-extended from the field width to the implementation's native integer width.

R-32465-27648
:   `binary scan` accepts the count `*` on an integer specifier to mean "as many elements as remain in the input"; the resulting list of values is assigned to a single variable.

R-59103-40802
:   `binary format` rejects the count `*` on the `x` and `@` cursor specifiers with the error message "cannot use \"*\" in binary format field"; on every other specifier the `*` count has a defined behavior given in the per-specifier requirements below.

R-18953-16554
:   `binary format` with the count `*` on the `a` or `A` specifier consumes one value argument and emits exactly its bytes, performing no padding or truncation; the produced field length equals the input string length.

R-14183-57437
:   `binary format` with the count `*` on the `b`, `B`, `h`, or `H` specifier consumes one value argument and emits exactly its characters (one per bit for `b`/`B`; one per nibble for `h`/`H`), performing no padding or truncation; the produced field length equals the input string length expressed in the specifier's natural unit.

R-33956-61677
:   `binary format` with the count `*` on an integer specifier (`c`, `s`, `S`, `t`, `i`, `I`, `n`, `w`, `W`, `m`) consumes exactly one value argument and treats it as a Tcl list; the specifier emits one integer field per list element, packing each element after applying the same per-width truncation rule that applies to a fixed count.

R-57487-54613
:   `binary format` with the count `*` on a floating-point specifier (`f`, `r`, `R`, `d`, `q`, `Q`) consumes exactly one value argument and treats it as a Tcl list; the specifier emits one IEEE 754 field per list element using the specifier's width and byte order.

R-28819-01644
:   `binary format` with the count `*` on the `X` cursor specifier rewinds the cursor to position zero.

R-47116-54899
:   `binary format` returns the error message "not enough arguments for all format specifiers" when the supplied value arguments are insufficient.

R-42906-22341
:   `binary format` and `binary scan` return the error message "bad field specifier" when the format string contains an unknown specifier letter.

R-65410-51824
:   `binary scan` follows the partial-scan rule: if the *binaryData* has too few bytes remaining to satisfy the next value, scanning stops, the corresponding variable and any subsequent variables are left untouched, and the count of variables already assigned is returned.

R-49218-09375
:   An empty *formatString* passed to `binary format` produces an empty byte sequence.

R-31564-03739
:   An empty *binaryData* passed to `binary scan` produces a return value of `0`.

R-03852-65413
:   The `a` specifier denotes an ASCII byte field NUL-padded to its count.

R-53174-31709
:   The `A` specifier denotes an ASCII byte field space-padded to its count.

R-34542-04103
:   `binary format` truncates an input string longer than the count when using the `a` or `A` specifier.

R-53255-62069
:   `binary scan` with the `A` specifier strips trailing space and NUL bytes from the read field.

R-48584-10712
:   The `b` specifier denotes a bit field encoded low-bit-first within each byte; one input character produces one bit, with `0` meaning zero and any other character meaning one.

R-00830-46773
:   The `B` specifier denotes a bit field encoded high-bit-first within each byte.

R-06973-20849
:   The `h` specifier denotes a hex field encoded low-nibble-first within each byte; one input character produces one nibble where `0`-`9` and `a`-`f` (case-insensitive) map to values 0-15.

R-07840-57050
:   The `H` specifier denotes a hex field encoded high-nibble-first within each byte.

R-47120-33399
:   `binary format` pads short input with zero bits or nibbles when the `b`, `B`, `h`, or `H` count exceeds the input length.

R-32135-39472
:   The `x` specifier inserts `count` NUL bytes when used in `binary format` and skips `count` input bytes when used in `binary scan`.

R-20624-04095
:   The `X` specifier moves the cursor back by `count` bytes; movement past the start of the buffer is silently clamped to position zero.

R-25940-65513
:   The `@` specifier moves the cursor to the absolute byte position given by its count.  Under `binary format`, any gap between the previous high-water mark and the seek position is zero-filled.

R-14406-51731
:   The `x`, `X`, and `@` cursor specifiers consume no value arguments under `binary format` and no variable arguments under `binary scan`.

R-05069-28317
:   `binary scan` accepts the count `*` on the `a`, `A`, `b`, `B`, `h`, and `H` specifiers to mean "all remaining bytes" (for `a`/`A`), "all remaining bytes' worth of bits" (for `b`/`B`), or "all remaining bytes' worth of nibbles" (for `h`/`H`).

R-30882-58136
:   The `f` specifier denotes an IEEE 754 binary32 (single-precision) float field in the host's native byte order.

R-65398-59132
:   The `r` specifier denotes an IEEE 754 binary32 float field in little-endian byte order.

R-40141-64595
:   The `R` specifier denotes an IEEE 754 binary32 float field in big-endian byte order.

R-52505-47336
:   The `d` specifier denotes an IEEE 754 binary64 (double-precision) float field in the host's native byte order.

R-44824-14970
:   The `q` specifier denotes an IEEE 754 binary64 float field in little-endian byte order.

R-24212-52504
:   The `Q` specifier denotes an IEEE 754 binary64 float field in big-endian byte order.

R-45917-21828
:   `binary format` accepts the strings `NaN`, `Inf`, and `-Inf` as float-specifier arguments and emits the canonical IEEE 754 patterns for each.

R-55429-28115
:   Any NaN bit pattern emitted by `binary format` for a binary32 field is normalised to the canonical quiet NaN pattern `0x7FC00000`; any NaN bit pattern emitted for a binary64 field is normalised to `0x7FF8000000000000`.

R-65214-62481
:   `binary scan` reads a NaN bit pattern from a float field as the string `NaN`; positive and negative infinities are read as `Inf` and `-Inf`.

R-36503-62216
:   `binary format` accepts arbitrary-precision (bignum) integer arguments on every integer specifier and truncates each value modulo 2^N, where N is the field width in bits.

R-64289-18387
:   The `j` and `J` specifiers denote an arbitrary-precision signed integer field whose width in bytes is the specifier's count.  `j` writes / reads the bytes in little-endian (least-significant byte first); `J` writes / reads in big-endian (most-significant byte first).  The value is encoded as two's complement with the sign bit at the most-significant byte in the big-endian view; on `binary scan` the value is always interpreted as signed and sign-extended.

R-22686-46265
:   `binary format j`N or `J`N raises an error with the message "integer value too large for j/J field" when the supplied integer value is outside the representable range `[-2^(N*8-1), 2^(N*8-1) - 1]`.

R-54898-61945
:   `binary format` with the count `*` on `j` or `J` consumes one value argument and packs it in the minimum number of bytes needed to represent that value as two's complement (one byte for zero; for non-zero values, the minimum width is just sufficient to include the sign bit).

R-07235-20862
:   `binary scan` with the count `*` on `j` or `J` consumes all remaining bytes (including zero remaining, which yields the value `0`) and assigns the decoded signed bigint to a single variable.

R-05827-41628
:   The `j` and `J` specifiers accept and produce arbitrary-precision integer values when `TH8_ENABLE_BIGINT` is set at compile time and `Th8_IsBigintEnabled(interp)` returns true at runtime; otherwise they are limited to values that fit in a 64-bit two's-complement field and scans wider than eight bytes raise an error.


### 19  I/O Commands

#### 19.1  puts

**Synopsis:** `puts` ?`-nonewline`? ?*channel*? *string*

R-24090-61568
:   The `puts` command writes string followed by a newline to the output.
R-06032-53760
:   The `-nonewline` option suppresses the trailing newline.

#### 19.2  gets

**Synopsis:** `gets` *channel* ?*varName*?

R-07337-52920
:   The `gets` command reads one line of input (up to and not including the newline) from the specified channel.
R-12363-41715
:   If a variable name is given, the line is stored in that variable and the number of characters read is returned.
R-62412-53550
:   If no variable name is given, the line itself is returned.
R-18288-38201
:   The `gets` command with a variable name stores the line in the variable and returns the number of characters read, or -1 at end-of-file.

#### 18.2a  read

**Synopsis:** `read` ?`-nonewline`? *channelId* | `read` *channelId* *numChars*

R-58165-00884
:   The [read] command in form "read ?-nonewline? channelId" SHALL read all data from the channel until EOF. With -nonewline, the trailing newline character SHALL be stripped from the result.
R-08255-31625
:   The [read] command in form "read channelId numChars" SHALL read exactly numChars characters, or fewer if EOF is reached before numChars are available.

#### 18.2b  flush

**Synopsis:** `flush` *channel*

R-39389-57537
:   The `flush` command returns the empty string.

#### 18.2c  close

**Synopsis:** `close` ?*channelId*?

R-05475-50994
:   The `close` command closes the specified channel, flushing any buffered output.
R-61890-36314
:   The `close` command returns the empty string.

#### 18.2d  seek

**Synopsis:** `seek` *channelId* *offset* ?*origin*?

R-28652-29370
:   The `seek` command sets the file position for the specified channel.
R-60631-05666
:   The `seek` command accepts an origin of `start`, `current`, or `end` to specify the reference point for the offset.

#### 18.2e  tell

**Synopsis:** `tell` *channelId*

R-34046-08322
:   The `tell` command returns the current byte position of the specified channel as an integer.

#### 19.3  source

**Synopsis:** `source` *name*

R-09664-38899
:   The `source` command retrieves the script identified by name through the platform's data-retrieval callback and evaluates it.
R-61728-51322
:   The name of the sourced script is recorded so that `info script` returns it during evaluation.
R-05497-60890
:   Nested `source` invocations maintain a stack of script names.
R-31363-07661
:   The `source` command returns the result of the last command executed in the sourced file.

#### 19.4  load

**Synopsis:** `load` *name* ?*initProc*?

The `load` command loads a binary extension into the interpreter.
Loading must be enabled by the host application via the
embedder's loading-enable mechanism before any `load` command
can succeed.

R-25864-47382
:   The `load` command loads the binary identified by name into the interpreter using the implementation's binary loader.
R-61098-59911
:   The `load` command produces an error if binary loading has not been enabled by the host.
R-41729-49555
:   If the initProc argument is provided, it names the initialization entry point; otherwise the entry point is derived from name.
R-11211-50952
:   A successfully loaded binary is recorded in the interpreter's loaded-library list.
R-33726-54804
:   The `load` command returns the empty string on success.

**Deviation from Tcl 8.x:**  In Tcl 8.x, `load` is
unconditionally available and a script can load any binary
extension the operating system permits.  This standard requires
the host application to enable binary loading explicitly via the
embedder's loading-enable mechanism before any `load` call can
succeed; in the absence of that opt-in, every `load` raises a
script error.  This is a security-by-default deviation: scripts
do not gain native code execution unless the embedder has decided
to allow it.  See Appendix C for the full deviation index.

#### 19.5  unload

**Synopsis:** `unload` ?`-nocomplain`? ?`-keeplibrary`? ?`-nokeeplibrary`? ?`--`? *name*

The `unload` command finalizes and optionally closes a previously
loaded binary extension.  Unloading must be enabled by the host
application via the embedder's unloading-enable mechanism before
any `unload` command can succeed.

R-45616-08734
:   The `unload` command calls the _Unload entry point of the named binary and removes it from the interpreter's loaded-library list.
R-56356-31045
:   The `unload` command produces an error if the named binary is not in the interpreter's loaded-library list.
R-58288-53995
:   The `-nocomplain` option suppresses all errors, causing the command to return the empty string silently.
R-15174-22820
:   The default behavior is `-keeplibrary`: the _Unload entry point is called but the shared library is not closed.
R-50869-02312
:   The `-nokeeplibrary` option requests that the shared library be closed (via dlclose or FreeLibrary) after the _Unload entry point is called.
R-36679-29193
:   The `-nokeeplibrary` option produces an error unless the host has enabled the implementation's dangerous-unload mode via the embedder's unloading-enable mechanism.
R-03886-00091
:   The `--` option marks the end of options, allowing library names that begin with a dash.
R-21269-37512
:   The `unload` command returns the empty string on success.

**Deviation from Tcl 8.x:**  In Tcl 8.x, `unload`
operates on any loaded library without requiring host opt-in;
the `-nokeeplibrary` option is also unconditionally available.
This standard requires the host application to enable unloading
via the embedder's unloading-enable mechanism, and further
requires a separate opt-in for the dangerous `-nokeeplibrary`
mode (which physically detaches the library from the process and
can corrupt the address space if any code or data references
remain).  Both gates default to off; the absence of either
produces a script error.  See Appendix C for the full deviation
index.


### 20  File Commands

All file commands are subcommands of the `file` ensemble.
The path manipulation commands (`dirname`, `join`, `split`) perform
pure string operations.  The `exists` command queries the platform's
data-existence callback.

#### 20.1  file dirname

**Synopsis:** `file dirname` *name*

R-40969-00092
:   The `file dirname` command returns the directory portion of a path name, omitting the last component and its preceding separator.
R-55000-49041
:   If the path contains no separator, the result is `.` (dot).
R-18250-49096
:   If the path is a root directory, the result is the root itself.

#### 20.2  file join

**Synopsis:** `file join` *name* ?*name* ...?

R-50124-31522
:   The `file join` command combines path components using a forward slash separator.
R-50437-19510
:   An absolute component (beginning with a separator) resets the result, discarding all preceding components.
R-37984-15130
:   Empty components are skipped.

#### 20.3  file split

**Synopsis:** `file split` *name*

R-64401-01441
:   The `file split` command returns a list whose elements are the components of the path.
R-22232-42067
:   A leading separator becomes the first element of the list as a standalone separator character.
R-46268-59137
:   Consecutive separators are collapsed.

#### 20.4  file exists

**Synopsis:** `file exists` *name*

The `file exists` command tests whether named data exists by
querying the platform's `xDataExists` callback.  The meaning of
"exists" is determined by the platform -- it may check a file
system, a database, a virtual file system, or any other data store.

R-51814-55378
:   The `file exists` command returns 1 if the named data exists according to the platform, 0 otherwise.
R-03414-21845
:   The `file exists` command returns 0 if the platform does not provide a data-existence callback.
R-33853-30033
:   The `file exists` command with an empty string argument returns 0.


#### 20.5  file normalize

**Synopsis:** `file normalize` *name*

The `file normalize` command returns the canonical absolute path
for *name*.  It resolves `.` and `..` components, collapses
redundant separators, and (when the platform provides it) resolves
symbolic links via the `xNormalizePath` callback.  If the platform
callback is not available, the path is returned unchanged.

R-19004-61741
:   The `file normalize` command returns the platform-canonical absolute path for the given name.
R-10315-21713
:   If the platform does not provide a path normalization callback, `file normalize` returns its argument unchanged.


#### 20.6  file tail

**Synopsis:** `file tail` *name*

The `file tail` command returns the last component of *name*
(everything after the last path separator).  Returns the name
unchanged if it contains no separator.  Returns the empty string
for root paths.  Pure string manipulation.

R-59685-03456
:   The `file tail` command returns the last component of the path, or the empty string for a root path.

#### 20.7  pwd

**Synopsis:** `pwd`

The `pwd` command returns the current working directory via the
platform's working-directory query mechanism.  Under TH8's
security model the working directory is confined to the
**base-path subtree** configured at interpreter creation; the
value returned by `pwd` is the current location **relative to**
the base path (e.g. `"."` at the base, `"./lib"` after `cd lib`).
If the platform does not provide a working-directory query
mechanism, the command returns an error.

R-58870-35364
:   The `pwd` command returns the current working directory as reported by the platform's working-directory query mechanism.
R-04236-17465
:   The `pwd` command produces a script error if the platform does not provide a working-directory query mechanism, with the message "permission denied: access to file system unavailable".

**Deviation from Tcl 8.x:**  In Tcl 8.x, `pwd`
returns the OS-absolute current working directory of the
process.  This standard requires `pwd` to return the location
**relative to** the implementation-defined base path
(`./subdir`-style paths), and confines the working directory to
the base-path subtree (see §20.8 for the corresponding `cd`
constraint).  Scripts that capture `[pwd]` and use it as an
OS-absolute path will fail under this standard; scripts that use
it as a base-relative anchor for further navigation will work
unchanged.  See Appendix C for the full deviation index.

#### 20.8  cd

**Synopsis:** `cd` ?*dirName*?

The `cd` command changes the current working directory via the
platform's working-directory change mechanism.  If *dirName* is
omitted, it defaults to `"."` (the base path).  Under TH8's
security model the target directory must resolve to a location
inside the **base-path subtree** configured at interpreter
creation; relative navigation within the subtree (`cd subdir`,
`cd ..`, `cd ./other`) is permitted.  Any path that resolves
outside the base subtree --- an absolute path that does not
start at the base, or a `..` traversal that would rise above it
--- is rejected with a permission error.

R-20968-52147
:   The `cd` command changes the current working directory via the platform's working-directory change mechanism, returning an empty string on success.
R-57714-55951
:   When *dirName* is omitted, `cd` defaults to `"."`.
R-41249-16138
:   The `cd` command produces a script error if the platform does not provide a working-directory change mechanism, with the message "permission denied: access to file system unavailable".
R-04719-20198
:   The `cd` command produces a script error if the implementation rejects the path, with the message "permission denied: cannot access foreign directory".

**Deviation from Tcl 8.x:**  In Tcl 8.x, `cd`
accepts any path the operating system permits, including
absolute paths anywhere on the filesystem and `..` traversals
that escape the script's starting directory.  This standard
confines `cd` to the implementation-defined base-path subtree:
relative navigation within the subtree (`cd subdir`, `cd ..`,
`cd ./other`) is permitted, but any path resolving outside the
base subtree is rejected with a permission error.  Scripts that
relied on `cd` to reach unrelated filesystem locations must
either constrain themselves to in-subtree navigation or run
under an embedder that places the base path at the filesystem
root.  See Appendix C for the full deviation index.

#### 20.9  file extension

**Synopsis:** `file extension` *name*

R-16870-62748
:   [file extension name] SHALL return the file extension (the last dot and everything after it in the last path component), or the empty string if no extension is present.

#### 20.10  file nativename

**Synopsis:** `file nativename` *name*

R-48203-47216
:   [file nativename name] SHALL convert directory separators to the native form: forward slashes on POSIX, backslashes on Windows.

#### 20.11  file pathtype

**Synopsis:** `file pathtype` *name*

R-25988-01568
:   [file pathtype name] SHALL return "absolute" for paths beginning with a root separator or drive letter plus separator, "volumerelative" for paths beginning with a drive letter without a separator (Windows only), and "relative" for all other paths.

#### 20.12  file rootname

**Synopsis:** `file rootname` *name*

R-29418-47253
:   [file rootname name] SHALL return the path with the file extension removed (everything before the last dot in the last path component).

#### 20.13  file separator

**Synopsis:** `file separator` ?*name*?

R-41650-23802
:   [file separator] without arguments SHALL return the native directory separator character. With a name argument, it SHALL return the first directory separator found in the name, or the native separator if none is present.

#### 20.14  file type

**Synopsis:** `file type` *name*

R-16710-28334
:   [file type name] SHALL return "file" for regular files, "directory" for directories, "file symbolicLink" or "directory symbolicLink" for symbolic links to files or directories respectively, "unsupported" for other file types, and "unknown" when the native API fails or the file does not exist.


### 21  Introspection Commands

All introspection commands are subcommands of the `info` ensemble.

#### 21.0a  info patchlevel

**Synopsis:** `info patchlevel`

Returns the implementation's patch level version string.  The
value is read from the `VERSION` file at build time.  The format must conform
to the regular expression
`^\d+\.\d+(?:\.\d){0,2}(?:-[0-9A-Za-z][_0-9A-Za-z])?$`.

R-01267-55373
:   The `info patchlevel` command returns the interpreter's patch level version string.

#### 21.0b  info complete

**Synopsis:** `info complete` *script*

Returns `1` if *script* is syntactically complete (all braces,
brackets, and double-quotes are balanced), `0` otherwise.

R-40646-36669
:   The `info complete` command returns 1 if the script has balanced delimiters, 0 otherwise.

#### 21.0c  info globals

**Synopsis:** `info globals` ?*pattern*?

R-50590-64823
:   The `info globals` command returns a list of all global variables.
R-64634-09797
:   The `info globals` command with a pattern argument returns only globals whose names match the glob pattern.

#### 21.0d  info library

**Synopsis:** `info library`

The `info library` command returns the path to the implementation's
script library directory if the engine's package has been provided
(via `package provide`), or an empty string if it has not.  This allows scripts to locate the
library directory for sourcing auxiliary files.

R-35603-27691
:   The `info library` command returns `"./lib/th8"` if the "th8" package has been provided via `package provide`.
R-00034-24955
:   The `info library` command returns an empty string if the "th8" package has not been provided.
R-36839-38193
:   The `info library` result reflects the package registration state, not merely the existence of the directory on disk.

#### 21.0e  info subcommands

**Synopsis:** `info subcommands` *command* ?*pattern*?

R-05560-54350
:   The `info subcommands` command returns a list of subcommands of the named ensemble command.
R-11672-38479
:   The `info subcommands` command with a pattern returns only subcommand names matching the glob pattern.
R-43740-46756
:   The `info subcommands` command returns the list of sub-commands for any ensemble command without requiring the ensemble to have been invoked first.

#### 21.0f  info cmdcount

**Synopsis:** `info cmdcount`

R-42349-45141
:   The `info cmdcount` command returns the total number of commands that have been evaluated by the interpreter.

#### 21.0g  info nameofexecutable

**Synopsis:** `info nameofexecutable`

R-19219-42106
:   The `info nameofexecutable` command returns the path of the host executable relative to the base directory.
R-13502-28084
:   The resolution order is: (1) an implementation-defined override variable if set, (2) the platform's executable-path query mechanism, (3) the empty string.

#### 21.0h  info sharedlibextension

**Synopsis:** `info sharedlibextension`

R-38164-60602
:   The `info sharedlibextension` command returns the platform's shared library file extension.

#### 21.0i  info loaded

**Synopsis:** `info loaded`

R-45786-18817
:   The `info loaded` command returns a list of the names of all libraries that have been loaded into the interpreter.

#### 21.1  info exists

**Synopsis:** `info exists` *varName*

R-11017-47048
:   The `info exists` command returns 1 if the named variable exists in the current scope, 0 otherwise.
R-09588-64534
:   The `info exists` command SHALL return 1 for a variable that is an array (has elements), even when accessed by its bare name without a subscript.

#### 21.1a  info default

**Synopsis:** `info default` *procName* *argName* *varName*

R-23634-54601
:   `info default` returns 1 if the named argument of the procedure has a default value, storing it in varName; returns 0 otherwise.
R-46238-50851
:   `info default` sets varName to the empty string when the argument has no default value.
R-26222-29181
:   `info default` with a non-existent procedure is an error.
R-59596-03970
:   `info default` with a non-existent argument name is an error.

#### 21.2  info commands

**Synopsis:** `info commands` ?*pattern*?

R-25423-56315
:   The `info commands` command returns a list of all commands visible in the current namespace, optionally filtered by a glob pattern.

#### 21.3  info procs

**Synopsis:** `info procs` ?*pattern*?

R-00425-56412
:   The `info procs` command returns a list of all procedures (user-defined commands), optionally filtered by a glob pattern.

#### 21.4  info vars

**Synopsis:** `info vars` ?*pattern*?

R-61970-65350
:   The `info vars` command returns a list of all variables in the current scope, optionally filtered by a glob pattern.

#### 21.5  info level

**Synopsis:** `info level` ?*number*?

R-21715-33701
:   The `info level` command with no argument returns the current procedure nesting depth as an integer, with 0 denoting the global level.
R-29828-17756
:   The `info level` command with a number argument returns the command and arguments of the invocation at that level.

#### 21.6  info script

**Synopsis:** `info script` ?*filename*?

R-38991-58513
:   The `info script` command with no argument returns the name of the script file currently being evaluated by `source`, or the empty string if no `source` is active.
R-51523-46504
:   The `info script` command with an argument sets the script name for the current invocation level and returns the new name.

#### 21.7  info body

**Synopsis:** `info body` *procName*

R-04279-51073
:   The `info body` command returns the body script of the named procedure.
R-02797-01349
:   Applying `info body` to a nonexistent or non-procedure command produces an error.

#### 21.8  info args

**Synopsis:** `info args` *procName*

R-39692-20126
:   The `info args` command returns the formal parameter list of the named procedure.
R-34410-07117
:   Applying `info args` to a nonexistent or non-procedure command produces an error.


#### 21.9  Platform Variables

The global scalars `::tcl_version` and `::tcl_patchLevel`, and the global array `::tcl_platform`, provide language-version and platform-specific information.

R-60831-14829
:   The `::tcl_version` global scalar contains a "major.minor" version string identifying the Tcl language version the interpreter implements.
R-55601-40893
:   The `::tcl_patchLevel` global scalar contains a "major.minor.patch" version string identifying the patch level of the Tcl language implementation, where the leading "major.minor" matches `::tcl_version`.
R-35060-23937
:   The `::tcl_platform(user)` element contains the login name of the user running the process.
R-01127-40264
:   The `::tcl_platform(platform)` element contains "windows" on Windows or "unix" on POSIX systems.
R-35186-60631
:   Per TIP #440, the `::tcl_platform(engine)` element contains a non-empty string identifying the Tcl language implementation in use (e.g. "Tcl" for canonical Tcl, "Eagle" for Eagle, "TH8" for TH8).
R-50835-55889
:   The `::tcl_platform(patchLevel)` element contains the interpreter's patch level version string.
R-48861-57662
:   The `::tcl_platform(host)` element contains the hostname of the machine running the process.
R-30439-28378
:   The `::tcl_platform(debug)` element is set to the string "1" when the interpreter was compiled with debug builds enabled (the TH8_DEBUG preprocessor symbol defined); the element is absent on release builds.

R-14274-04677
:   The `::tcl_platform(compileOptions)` element SHALL exist in
:   every TH8 interpreter and SHALL contain a Tcl-format list of
:   compile-time feature toggle names (e.g. `ENABLE_CRYPTOGRAPHY`,
:   `ENABLE_BIGINT`, `ENABLE_REGEXP`) that were defined when the
:   binary was built.  The list SHALL contain at least one
:   element; an empty list indicates a build configuration error.

R-14030-24353
:   The presence of a feature-toggle name in
:   `::tcl_platform(compileOptions)` SHALL imply that the
:   corresponding compile-time gate (`TH8_<NAME>`) was defined at
:   build time, and that script-visible features which depend on
:   that gate are available at run time.

#### 21.10  info context

**Synopsis:** `info context`

R-02028-09998
:   [info context] SHALL return a stable 128-character hexadecimal string computed by SHA-512 hashing a 32-byte seed composed of the parent process ID (4 bytes), process ID (4 bytes), thread ID (8 bytes), and 16 bytes of cryptographic random entropy. The value SHALL be computed once and cached for the lifetime of the process.

#### 21.11  info varlinks

**Synopsis:** `info varlinks`

R-08542-47922
:   [info varlinks] SHALL return a list of all variable names in the current call frame that are linked to variables in other frames via upvar, global, or variable.


### 22  Namespace Commands

All namespace commands are subcommands of the `namespace` ensemble.

#### 22.1  namespace current

**Synopsis:** `namespace current`

R-53980-30653
:   The `namespace current` command returns the fully qualified name of the current namespace.

#### 22.2  namespace eval

**Synopsis:** `namespace eval` *name* *script* ?*script* ...?

R-24585-38621
:   The `namespace eval` command evaluates script in the context of the named namespace, creating the namespace if it does not exist.

##### 22.2.1  Ephemeral Frame Semantics

R-16667-44187
:   `namespace eval` SHALL push a new ephemeral call frame for the duration of the script evaluation.  Variables created by `set` within this frame are local to the evaluation and are destroyed when the frame is popped.
R-20051-12919
:   Variables created by `set` inside `namespace eval` SHALL NOT persist in the namespace's variable storage after the `namespace eval` completes.  Only the `variable` command creates persistent namespace variables.
R-19891-51894
:   The `variable` command inside `namespace eval` SHALL create or access a variable in the namespace's persistent variable storage (`paVar`), which survives after the `namespace eval` frame is popped.
R-22638-00510
:   Variables created by `set` inside `namespace eval` SHALL be accessible during the evaluation via `$`, `set`, `upvar`, and other variable commands, but SHALL NOT be visible via `info vars` on the namespace after the evaluation completes.

**Deviation from Tcl 8.x:**  In Tcl 8.x, `set` inside
`namespace eval` creates a variable in the namespace's persistent
storage, identical in lifetime to one created by `variable`.  This
standard intentionally deviates from that behavior.

The rationale is that conflating ephemeral and persistent variable
creation in a single command (`set`) leads to namespace pollution
and reduces code clarity.  In Tcl 8.x, scripts routinely use `set`
where `variable` is semantically correct, because both happen to
produce the same result.  This makes it impossible to distinguish
"I need this value only during this eval" from "I am declaring
persistent namespace state" by reading the code.

Under this standard, the distinction is explicit and enforceable:

| Command | Scope | Lifetime | Semantics |
|---------|-------|----------|-----------|
| `set x 1` inside `namespace eval` | Ephemeral frame | Until eval returns | Scratch variable |
| `variable x 1` inside `namespace eval` | Namespace storage | Until `unset` or namespace deletion | Declared state |

This means that `namespace eval ::foo { set i 0; while {$i < 10} { ... ; incr i } }` does not leave the loop variable `i` in the `::foo` namespace after the eval completes — only variables explicitly declared with `variable` persist.

Scripts that depend on the Tcl 8.x behavior of `set` creating
persistent namespace variables must be updated to use `variable`
instead, which is the correct and self-documenting command for
that purpose.

#### 22.3  namespace exists

**Synopsis:** `namespace exists` *name*

R-09931-18274
:   The `namespace exists` command returns 1 if the named namespace exists, 0 otherwise.

#### 22.4  namespace delete

**Synopsis:** `namespace delete` ?*name* ...?

R-41102-11253
:   The `namespace delete` command deletes each named namespace and all its contained commands, variables, and child namespaces.
R-45968-58474
:   If a namespace is deleted while script code is executing, the namespace SHALL be detached from its parent immediately and the recursive free of its contents SHALL be deferred until the script evaluation stack fully unwinds.

#### 22.5  namespace children / namespace parent

**Synopsis:** `namespace children` ?*name*? | `namespace parent` ?*name*?

R-05433-10222
:   The `namespace children` command returns a list of the child namespaces of the given namespace (default: current).
R-41224-57965
:   The `namespace parent` command returns the fully qualified name of the parent namespace.

#### 22.6  namespace export / namespace import

**Synopsis:** `namespace export` ?*pattern* ...? | `namespace import` ?*pattern* ...?

R-04455-29532
:   The `namespace export` command adds patterns to the namespace's export list, determining which commands may be imported by other namespaces.
R-10701-37097
:   The `namespace import` command creates commands in the current namespace that are copies of the exported commands matching the given patterns.

#### 22.7  namespace code

**Synopsis:** `namespace code` *script*

R-49982-44031
:   The `namespace code` command returns a script prefix that, when evaluated, will execute script in the current namespace.


### 23  Package Commands

All package commands are subcommands of the `package` ensemble.

#### 23.1  package provide

**Synopsis:** `package provide` *name* ?*version*?

R-37867-23416
:   The `package provide` command declares that the named package is present, optionally at the given version.

#### 23.2  package require

**Synopsis:** `package require` ?`-exact`? *name* ?*version*?

R-09837-41339
:   The `package require` command loads the named package, invoking the registered `ifneeded` script if necessary.
R-36419-44049
:   The `-exact` option requires an exact version match.

#### 23.3  package ifneeded

**Synopsis:** `package ifneeded` *name* *version* ?*script*?

R-49109-54019
:   The `package ifneeded` command registers a script that, when evaluated, provides the given version of the named package.
R-11081-25091
:   When `package ifneeded` is called with a script argument (register form), the interpreter result is cleared.

#### 23.4  package present

**Synopsis:** `package present` ?`-exact`? *name* ?*version*?

R-22252-01822
:   The `package present` command checks whether the named package is already loaded, returning its version if so.

#### 23.5  package names / package versions

**Synopsis:** `package names` | `package versions` *name*

R-11675-43758
:   The `package names` command returns a list of all known package names.
R-30063-56559
:   The `package versions` command returns a list of all known versions of the named package.

#### 23.6  package forget

**Synopsis:** `package forget` ?*name* ...?

R-16506-57090
:   The `package forget` command removes all information about the named packages.

#### 23.7  package unknown

**Synopsis:** `package unknown` ?*command*?

R-32039-29681
:   The `package unknown` command sets or queries the script invoked when `package require` cannot find a package.

#### 23.8  package vcompare / package vsatisfies

**Synopsis:** `package vcompare` *v1* *v2* | `package vsatisfies` *version* *requirement*

R-17088-65425
:   The `package vcompare` command compares two version strings and returns -1, 0, or 1.
R-61177-25697
:   The `package vsatisfies` command returns 1 if version satisfies requirement, 0 otherwise.

#### 23.9  package scan

**Synopsis:** `package scan`

R-27404-11181
:   The `package scan` command re-scans the `auto_path` variable for pkgIndex files and sources any newly discovered package index files.


### 24  Interpreter Commands

#### 24.1  interp cancel

**Synopsis:** `interp cancel` ?`-unwind`? ?`--`? ?*path*? ?*result*?

R-07502-38675
:   The `interp cancel` command requests cancellation of the currently executing script.
R-00559-15113
:   The `-unwind` option causes all call frames to unwind to the top level, preventing `catch` from intercepting the cancellation.


### 25  Time and System Commands

#### 25.1  clock seconds

**Synopsis:** `clock seconds`

R-42628-45607
:   The `clock seconds` command returns the current time as the number of seconds since the Unix epoch (1970-01-01 00:00:00 UTC).
R-55223-15762
:   The return value is a wide integer sufficient to represent dates beyond the year 2038.

#### 25.2  time

**Synopsis:** `time` *script* ?*count*?

R-21676-43295
:   The `time` command SHALL evaluate script count times (default 1) and return a string of the form "N microseconds per iteration" where N is the integer truncated elapsed microseconds per iteration.
R-36514-28636
:   If count is omitted, the `time` command SHALL default to a single iteration.
R-33048-52619
:   If the script produces an error during any iteration, the `time` command SHALL propagate that error immediately.

#### 25.3  pid

**Synopsis:** `pid`

R-13293-54261
:   The `pid` command returns the process identifier of the host process.

#### 25.4  after

**Synopsis:** `after` *milliseconds*

R-40471-05227
:   The `after` command SHALL sleep for the specified number of milliseconds, returning the empty string on completion.
R-52937-01216
:   The `after` command SHALL check for cancellation, suspension, and step-counter limits between each sleep increment, returning an error immediately if any condition fires.
R-08659-00690
:   The `after` command SHALL sleep in increments no larger than 50 milliseconds to ensure responsive cancellation.

#### 25.5  update

**Synopsis:** `update` ?`-limit` *N*?

R-05853-51502
:   The `update` command SHALL drain pending events from the
interpreter's event queue, invoking each event's callback in
FIFO order on the calling thread, and returning the empty
string when no events remain (or when the optional `-limit N`
ceiling has been reached).

R-48121-08664
:   The `update -limit N` form SHALL stop after `N` callbacks
have been invoked, leaving any remaining events queued for
the next call.  `N` SHALL be a positive integer; values less
than 1 SHALL raise a script error.

R-59869-11636
:   The `update` command SHALL check for interpreter
cancellation between callbacks via `Th8_Ready` (or its
equivalent).  When cancellation is observed, `update` SHALL
return an error immediately, leaving any remaining events
queued.

R-29415-33770
:   The script side has NO API for adding events to the queue.
Events are added only by embedders via the public C API
`Th8_QueueEvent`, which is callable from any thread.  Scripts
can ONLY drain (`update`) or wait on (`vwait`) the queue.
This is a deliberate security boundary: untrusted script
cannot inject work into the event loop.

R-47665-55162
:   An event callback invoked by `update` MAY suspend its
:   enclosing coroutine via `yield` or `yieldto`.  When the
:   coroutine subsequently resumes, `update`'s drain loop
:   SHALL continue from the point at which the callback
:   suspended; remaining queued events SHALL be processed
:   by the same `update` invocation, subject to its
:   `-limit` and cancellation gates.

> **Implementer's note (informative).**  Implementations
> that do not provide a thread-safe event queue SHALL still
> recognise the `update` command, but SHALL return an error
> with a message indicating that the event queue is not
> available, rather than silently no-op.  See the TH8
> Language Extensions document for the C-API contract.

#### 25.6  vwait

**Synopsis:** `vwait` ?`-timeout` *MS*? *varName*

R-50111-34183
:   The `vwait` command SHALL block the calling thread until
the named variable is signaled (created, changed, or unset).
While blocked, it SHALL drain events from the queue exactly
as `update` does, so that an event callback that sets the
named variable can release the wait.

R-54984-57209
:   `varName` MAY name a scalar variable or an array element
(in the form `arrayName(elementName)`).  An element is
signaled by writing or unsetting that specific element; the
parent array's other elements do not signal it.

R-11279-35358
:   The `vwait -timeout MS` form SHALL bound the wait to at
most `MS` milliseconds.  If the variable is not signaled
within that interval, `vwait` SHALL raise a script error
with a message of the form `vwait: timeout`.  When `-timeout`
is omitted, the wait is unbounded except by interpreter
cancellation.

R-58860-53854
:   The `vwait` command SHALL check for interpreter
cancellation between events and immediately before each
underlying wait, so that a `Th8_CancelEval` from another
thread surfaces as a script error promptly (target latency
≤ 50 milliseconds).

R-57698-44679
:   On success, `vwait` SHALL return the empty string.

R-27275-32732
:   Nested `vwait` invocations are permitted: a callback
drained by an outer `vwait` MAY itself call `vwait` on a
different (or the same) variable.  The implementation SHALL
NOT impose a nesting limit beyond the general
`Th8_Ready`-checked recursion budget.

> **Implementer's note (informative).**  The script-visible
> semantics above are independent of whether the
> implementation uses a manual-reset event handle, a
> condition variable, or a polling loop.  TH8 uses a
> manual-reset event handle for alertable-wait support
> (`QueueUserAPC` on Windows, signal interruption on POSIX).

## Part IV --- Semantics

### 26  Error Model

#### 26.1  Error Propagation

R-07142-01462
:   When a command returns TCL_ERROR, the error propagates up the call stack until intercepted by `catch`.
R-56189-52173
:   The global variable `errorInfo` accumulates a stack trace as the error propagates.
R-42886-55673
:   Each level adds an annotation indicating the command that was executing when the error occurred.

#### 26.2  Error Information Variables

R-45043-49981
:   The `errorInfo` variable contains a human-readable stack trace of the error.
R-07255-02881
:   The `errorCode` variable contains a machine-readable error code set by the `error` command.


### 27  Namespace System

#### 27.1  Namespace Hierarchy

R-39235-55125
:   Namespaces form a tree rooted at the global namespace `::`.
R-00397-49795
:   Namespace names are separated by `::`.
R-11940-58702
:   The global namespace is both the root and the default namespace at the top level.

#### 27.2  Name Resolution

R-30207-53705
:   An unqualified command name is first looked up in the current namespace, then in the global namespace.
R-48221-02733
:   A fully qualified name beginning with `::` is resolved from the global namespace.
R-56076-25034
:   Variable names follow the same resolution rules when qualified with `::`.


---

## Part V --- Conformance


### 28  Minimal Conformance

A conforming implementation must satisfy every normative
requirement in this document for the commands listed in
Sections 11 through 25.  Failure to satisfy any single
requirement means the implementation does not conform.

R-31203-09563
:   A conforming implementation SHALL provide all commands specified in Sections 11 through 25 with the behavior described by the associated requirements.
R-63449-27809
:   A conforming implementation may provide additional commands beyond those specified in this document.
R-52766-54222
:   A conforming implementation SHALL NOT alter the behavior of any command specified in this document in a way that contradicts a normative requirement.

---

## Appendices

### Appendix A --- Reserved for Requirements Cross-Reference

The requirements cross-reference table will be generated by the
companion tool `tools/mkreq.tcl` from the normative requirement
text in this document.  Each requirement is identified by an
**R-*nnnnn*-*nnnnn*** marker derived from the MD5 hash of the
normalized requirement text.

See Appendix B for the algorithm.

### Appendix B --- Requirement Identifier Algorithm

The requirement identifier is computed as follows:

1.  Extract the normative requirement text.
2.  Strip leading and trailing whitespace.
3.  Collapse all internal whitespace sequences to a single space.
4.  Remove Markdown inline formatting (backticks, asterisks).
5.  Compute the MD5 hash of the resulting byte string (UTF-8).
6.  Interpret the first 4 hexadecimal digits of the hash as a
    16-bit unsigned integer *a* and the next 4 hexadecimal digits
    as a 16-bit unsigned integer *b*.
7.  The requirement identifier is `R-` followed by *a* as a
    five-digit zero-padded decimal number, a hyphen, and *b* as
    a five-digit zero-padded decimal number.

This algorithm is identical in principle to the requirement
marking algorithm used by the SQLite project.

### Appendix C --- Deviations from Tcl 8.x

This appendix is informative.  It indexes every place in this
standard where the normative requirements **intentionally
diverge** from canonical Tcl 8.x behavior, so that
implementers, reviewers, and porters can audit the diffs in a
single place.  Each row points to the section that contains the
authoritative deviation note.

| § | Topic | Tcl 8.x behavior | This standard |
|---|---|---|---|
| §1 | Maximum string byte length | No fixed limit (memory- and `int`-bounded, often 2 GB+) | 100 MB hard cap (R-29508-16704) |
| §5.1 | Source encoding | Permissive: invalid UTF-8 stored as raw bytes and re-interpreted opportunistically | Strict UTF-8; invalid byte sequences rejected at input boundaries |
| §19.4 | `load` | Always available; loads any binary the OS permits | Gated by the embedder's loading-enable mechanism; default-off |
| §19.5 | `unload` | Always available; `-nokeeplibrary` always available | Gated by the embedder's unloading-enable mechanism; `-nokeeplibrary` requires a separate dangerous-mode opt-in; both default off |
| §20.7 | `pwd` | Returns OS-absolute current working directory | Returns location relative to the implementation-defined base path |
| §20.8 | `cd` | Accepts any path the OS permits | Confined to the base-path subtree; in-subtree navigation permitted, escapes rejected |
| §22.2.1 | `set` inside `namespace eval` | Creates a persistent namespace variable, identical in lifetime to one declared by `variable` | Creates an ephemeral frame-local variable that does not persist after the eval returns; only `variable` declares persistent state |

**Anchors that are NOT deviations** (listed for clarity, since
they have been a source of confusion):

- §11.8 `variable` with a fully qualified name (R-19756-08248)
  *matches* Tcl 8.x behavior — the requirement is normative
  conformance, not a deviation.
- §9.2.1 `random()` is a TH8 *addition* (canonical Tcl 8.x has
  only `rand()` / `srand()`).  The standard's R-22028-56793
  distinguishes the two; this is not a deviation but a
  superset.
- §9.2.3 C99 math functions (TIP #745) and §9.2.4 float
  classification (TIP #521) are TIPs adopted into this standard;
  they do not conflict with canonical Tcl 8.6 (which lacks them
  but does not contradict them).

### Appendix D --- Submission to the Tcl Core Team (Omnibus TIP)

This appendix is informative.

The deviations indexed in Appendix C are deliberate, but the
long-term aim of this standard is alignment with canonical Tcl
through the **Tcl Improvement Proposal (TIP)** process.

#### D.1  Why an omnibus TIP

The standard's deviations are not independent --- they form a
coherent design (security-by-default + deterministic semantics +
bounded resource use) and only deliver their full value when
adopted together.  Submitting them as separate TIPs would
fragment the conversation, multiply the voting load on the Tcl
Core Team, and create the risk of partial adoption that leaves
the standard's invariants unmet.

The intended submission path is therefore a single **Omnibus
TIP** --- a "Strict Compliance Mode for Tcl" proposal that
bundles every Appendix C deviation into one document, voted on
once.  The omnibus framing minimizes Core-Team review overhead
(one debate, one vote, one reference implementation), keeps the
deviations stack-ranked against each other, and lets adopters
enable the standard wholesale rather than piece-by-piece.

#### D.2  Proposed structure of the omnibus TIP

The omnibus TIP is anticipated to introduce **one new
per-interpreter mechanism** --- a *strict-compliance mode* ---
plus a small set of default-behavior changes that are safe enough
to land without an opt-in.

1. **A new strict-compliance mode** (the bulk of the TIP).
   Canonical Tcl gains a per-interpreter (and optionally
   per-script) compliance toggle.  When enabled, the
   interpreter's observable behavior matches this standard
   wholesale: every deviation in Appendix C activates as a
   single switch.  When disabled, behavior remains the
   historical Tcl 8.x default so that no existing script
   breaks.  The toggle is *all-or-nothing* by design --- the
   standard's invariants are interlocking, and per-deviation
   toggling defeats the rationale.

2. **Default-behavior changes** (a short list inside the same
   TIP).  Deviations that are strictly safer / strictly more
   deterministic and that would not plausibly break existing
   scripts are proposed as new defaults rather than opt-ins.
   The candidate set is small and conservative.

#### D.3  Per-deviation disposition in the omnibus

| Appendix C deviation | Disposition in the omnibus TIP |
|---|---|
| §1 100 MB string-length cap | **Strict-compliance only.** Existing scripts may exceed this. |
| §5.1 Strict UTF-8 input | **Strict-compliance only.** Existing scripts may carry Latin-1 bytes. |
| §19.4 / §19.5 `load` / `unload` gating | **Default change.** The gating mechanism already exists at the C API level in stock Tcl (via `Tcl_StaticPackage` / linker policy); the omnibus formalizes the script-level error on disabled-gate as a default behavior. |
| §20.7 / §20.8 `pwd` / `cd` base-path confinement | **Strict-compliance only.** Existing scripts may roam the filesystem. |
| §22.2.1 `set` inside `namespace eval` ephemeral | **Strict-compliance only.** This is the largest behavioral change. |

The split between "default change" and "strict-compliance only"
is calibrated to keep the default-change set vanishingly small
(so the omnibus TIP is a low-risk vote) while still concentrating
the standard's invariants in one cleanly-toggled mode.

#### D.4  Tracking and stewardship

This appendix is the standard's *forward-looking* statement only;
the omnibus TIP itself is not part of this standard and is
authored separately when the Tcl Core Team's submission window
opens.

A separate TIP-submission plan, to be authored when the Tcl
Core Team's submission window opens, will track the omnibus
TIP's draft status, owners, target Tcl release, and any
Core-Team feedback that warrants amending Appendix C of this
standard.

The standard itself is stable: ratification by the Tcl Core
Team would not require re-issuing this document, since the
Appendix C deviations are already self-contained normative
statements.  Adoption simply means that canonical Tcl (with
strict-compliance mode enabled, plus the few default changes)
becomes a conforming implementation of this standard.

### Appendix E --- Lexical and Syntactic Grammar

This appendix gives the formal grammar of the language.  It is a
lexical-syntactic grammar rather than a token grammar: because the
language performs substitution during parsing and word boundaries
depend on quoting context, the traditional split into an independent
lexer and parser does not apply.  The grammar below is normative for
syntax; the semantics of each production (in particular the value
produced by each form of substitution) are given in the body of this
standard (Parts II and IV).  Where this grammar and the prose body
appear to differ, the prose body governs.

#### E.1  Notation

The grammar is written in a variant of EBNF:

-   `=`            defines a production.
-   `,`            concatenation.
-   `|`            alternation.
-   `[ x ]`        zero or one occurrence of `x`.
-   `{ x }`        zero or more occurrences of `x`.
-   `( x )`        grouping.
-   `"..."`        a literal terminal (the quoted characters).
-   `? ... ?`      a terminal described in prose.
-   `- x`          exception (any terminal of the preceding class
                   except those matched by `x`).

The terminal alphabet is the set of Unicode code points (§3.2).

#### E.2  Characters and separators

```
character        = ? any Unicode code point ? ;
newline          = ? U+000A ? ;
whitespace-char  = " " | ? U+0009 ? | ? U+000B ? | ? U+000C ? | ? U+000D ? ;
command-sep      = newline | ";" ;
word-sep         = whitespace-char , { whitespace-char }
                 | continuation ;
continuation     = "\" , newline ;
```

A `continuation` (a backslash immediately followed by a newline) acts
as a word separator that is replaced by a single space; it does not
terminate the command.

#### E.3  Scripts and commands

```
script           = { blank-or-sep } ,
                   [ command , { command-sep , { blank-or-sep } , command } ] ,
                   { blank-or-sep } ;
blank-or-sep     = word-sep | command-sep ;
command          = comment
                 | word , { word-sep , word } ;
comment          = "#" , { character - newline } ;
```

A `comment` is recognized only where a command's first word would
begin (that is, at the start of a command).  A `#` occurring where a
word is expected in any other position is an ordinary bare-word
character.

#### E.4  Words

```
word             = [ expansion-prefix ] , word-body ;
expansion-prefix = "{*}" ;
word-body        = braced-word | quoted-word | bare-word ;
```

The `expansion-prefix` `{*}` causes the substituted value of the word
to be split into multiple words (§ word expansion).  It has no effect
unless it immediately precedes a non-empty `word-body`.

```
braced-word      = "{" , { brace-content } , "}" ;
brace-content    = ( character - ( "{" | "}" | "\" ) )
                 | "\" , character
                 | braced-word ;
```

Inside a `braced-word`, no substitution occurs except that a
`continuation` is replaced by a single space; braces nest and must
balance, and a brace preceded by a backslash does not count toward
nesting.

```
quoted-word      = '"' , { quoted-content } , '"' ;
quoted-content   = ( character - ( '"' | "\" | "$" | "[" ) )
                 | substitution ;
```

Inside a `quoted-word`, word separators lose their special meaning
(spaces and newlines are literal), but variable, command, and
backslash substitution all occur.

```
bare-word        = bare-item , { bare-item } ;
bare-item        = ( character - ( whitespace-char | newline | ";"
                                   | "$" | "[" | "\" ) )
                 | substitution ;
```

A `bare-word` ends at the first unquoted `word-sep` or `command-sep`.

#### E.5  Substitution

```
substitution     = variable-subst | command-subst | backslash-subst ;

variable-subst   = "$" , ( braced-var-name
                         | var-name , [ "(" , { index-char } , ")" ] ) ;
var-name         = name-char , { name-char }
                 | "{" , { character - "}" } , "}" ;
braced-var-name  = "{" , { character - "}" } , "}" ;
name-char        = ? a letter, digit, underscore, or ":" ? ;
index-char       = character - ")" ;

command-subst    = "[" , script , "]" ;

backslash-subst  = "\" , escape ;
escape           = "a" | "b" | "f" | "n" | "r" | "t" | "v"
                 | "\" | newline
                 | "x" , hex-digit , { hex-digit }
                 | "u" , hex-digit , [ hex-digit , [ hex-digit ,
                       [ hex-digit ] ] ]
                 | "U" , hex-digit , { hex-digit }   (* up to 8 *)
                 | octal-digit , [ octal-digit , [ octal-digit ] ]
                 | ( character - ( "a" | "b" | "f" | "n" | "r" | "t"
                       | "v" | "x" | "u" | "U" | newline
                       | octal-digit ) ) ;
hex-digit        = ? "0"-"9", "a"-"f", "A"-"F" ? ;
octal-digit      = ? "0"-"7" ? ;
```

For `variable-subst`, the `$name(index)` form performs array-element
substitution and the `index-char` sequence is itself subject to
substitution; the `${name}` form takes the variable name verbatim
between the braces.  A `$` that is not followed by a valid `var-name`
or `{` is a literal dollar sign.

For `backslash-subst`, an `escape` that is not one of the recognized
sequences yields the escaped character literally (the backslash is
removed).  A backslash before a `newline` is the `continuation` of
§E.2 when it occurs between words; within a word it likewise collapses
to a single space.

#### E.6  Relationship to the prose body

Each production above corresponds to a parsing rule stated
normatively in the body:

-   `script`, `command`, `command-sep` --- Part II, "Script and command
    structure".
-   `word`, `braced-word`, `quoted-word`, `bare-word` --- Part II, "Words
    and quoting".
-   `expansion-prefix` --- Part II, "Argument expansion".
-   `variable-subst` --- Part II, "Variable substitution".
-   `command-subst` --- Part II, "Command substitution".
-   `backslash-subst` --- Part II, "Backslash substitution".
-   `comment` --- Part II, "Comments".

Substitution is performed exactly once per pass, scanning each word
left to right; the grammar does not re-scan the result of a
substitution.  This single-pass rule is normative (Part IV,
"Substitution semantics").

### Appendix F --- Cross-Engine Compatibility Matrix

This appendix is informative.  It records, feature by feature, how the
three reference engines used to validate this standard behave: **TH8**
(this implementation), **Tcl** (native Tcl 8.6.x), and **Eagle** (the
managed .NET Tcl, 8.4-derived).  It is the companion to Appendix C:
Appendix C indexes where the *standard* deliberately diverges from
canonical Tcl 8.x; this appendix indexes where the *three engines*
differ from each other, which is what the differential-conformance
gates (`make check-tcl86`, `make check-eagle`) reconcile via
engine-aware test constraints.

Legend: **Y** = present/applicable; **--** = absent/not applicable;
a value or phrase describes the engine-specific behavior.  "n/a
(gate: `c`)" means the suite guards the case with test constraint `c`.

#### F.1  Command and syntax availability

| Feature | TH8 | Tcl 8.6 | Eagle |
|---|---|---|---|
| `{*}` argument expansion | Y | Y | -- (gate: run under TH8/Tcl only) |
| `exec` (subprocess) | -- (no forking) | Y | Y (heredoc `<<` form differs; gate: `not_eagle`) |
| `file tempname` | Y (in-memory temp channel; size arg) | -- (has `file tempfile`) | Y (zero-argument form only) |
| `lremove` | Y (multi-index removal) | -- | Y (nested-descent index chain) |
| `info subcommands` | Y | -- | Y (empty result on a non-ensemble) |
| `interp cancel` (TIP #285) | Y | Y | n/a (gate: `tip285` / `interp_cancel`) |
| `random()` math function | Y (addition) | -- (`rand`/`srand` only) | Y |
| C99 math funcs (TIP #745) | Y | -- | engine-dependent (gate: `mathfuncs`) |
| `package scan` | Y | version-dependent | n/a (gate: `package_scan`) |

#### F.2  Behavioral divergences (same command, different result)

| Behavior | TH8 | Tcl 8.6 | Eagle |
|---|---|---|---|
| `lassign list` with zero varNames | returns the list | returns the list | error: requires >= 1 varName (gate: `not_eagle`) |
| `epsilon()` value | `2.220446049250313e-16` (DBL_EPSILON, machine epsilon) | `2.220446049250313e-16` | `5e-324` (.NET `Double.Epsilon`, smallest denormal) |
| float scientific notation | lowercase `e` (`2e-16`) | lowercase `e` | uppercase `E` (`2E-16`) -- suite normalizes |
| `regexp -badswitch` | error: `bad switch "..."` | error | no error (switch silently ignored) |
| `info cmdcount` extra args | rejects 2+ args | rejects 2+ args | accepts `?path? ?type?` (up to two) |
| `info script` setter | returns the set name | returns the set name | by-design different (gate: `not_eagle`) |
| array search after array mutation | strict invalidation (`couldn't find search`) | version-dependent | different (gate: `array_searches`) |
| array auto-create on element write | Y | Y | -- (gate: skip under Eagle) |

#### F.3  Diagnostic / error-text differences

| Case | TH8 | Tcl 8.6 | Eagle |
|---|---|---|---|
| bareword in `[expr]` | `invalid bareword "x"` | `invalid bareword "x"` | rejected, different wording (suite matches both) |
| `base64 decode` of invalid input | clean Tcl error `invalid base64 character` | -- (no `base64`) | leaks the raw .NET exception text |

#### F.4  String, numeric, and encoding model

| Property | TH8 | Tcl 8.6 | Eagle |
|---|---|---|---|
| internal string representation | strict UTF-8 | modified UTF-8 (permissive; raw bytes tolerated) | UTF-16 (.NET `System.String`) |
| source-text length of multi-byte input | code-point count | code-point count | source-*byte* count (suite uses `-encoding`) |
| maximum string length | 100 MB hard cap (R-29508-16704) | ~2 GB+ (memory/`int`-bounded) | .NET runtime limits |
| floating-point model | IEEE 754 binary64 | IEEE 754 binary64 | IEEE 754 binary64 (.NET formatting differs, see F.2) |

#### F.5  Standard deviations from canonical Tcl (summary)

The rows below are where **the standard itself** (and hence TH8)
deliberately diverges from canonical Tcl 8.x; see Appendix C for the
authoritative notes.  Native Tcl 8.6 exhibits the "canonical" column
and is reconciled by the suite through documented deviations rather
than engine constraints.

| Topic | TH8 / this standard | Canonical Tcl 8.x |
|---|---|---|
| maximum string length | 100 MB cap | no fixed limit |
| source encoding | strict UTF-8, invalid rejected | permissive, raw bytes tolerated |
| `load` / `unload` | embedder-gated, default-off | always available |
| `pwd` / `cd` | confined to a base-path subtree | full OS current directory |
| `set` in `namespace eval` | ephemeral frame-local | persistent namespace variable |

#### F.6  Maintenance

This matrix is derived from the per-item divergence record in
`docs/internal/more_info.md` and the deviation index in Appendix C.
When a differential-conformance gate (`make check-tcl86` /
`make check-eagle`) is brought to green by adding or changing an
engine constraint, or when a new deviation is ratified, the
corresponding row here is updated so this appendix stays the
single cross-engine reference.

### Appendix G --- Command Index

This appendix is informative.  It catalogs every script-visible
command specified by this standard, with a pointer to the section
that normatively defines it.  It is generated mechanically from the
command sections of this document by `tools/gencmdindex.tcl`, so it
cannot drift from the normative text.  Ensemble sub-command variants
written with a slash in a section heading (for example `string
first / string last`) are listed separately, both pointing at the
shared section.  127 command entries are indexed.

#### G.1  By command category

The category numbering mirrors the chapter numbering of Part III;
each chapter corresponds to a built-in command group, several of
which are individually removable at compile time via the
`TH8_PLUGIN_*` feature gates.

##### Section 11 --- Variable Commands

- `append` --- §11.3
- `array` --- §11.5
- `global` --- §11.6
- `incr` --- §11.4
- `set` --- §11.1
- `unset` --- §11.2
- `upvar` --- §11.7
- `variable` --- §11.8

##### Section 12 --- Control Flow Commands

- `break` --- §12.6
- `catch` --- §12.9
- `concat` --- §12.13
- `continue` --- §12.7
- `coroutine` --- §12.15
- `error` --- §12.10
- `eval` --- §12.11
- `exit` --- §12.12
- `for` --- §12.2
- `foreach` --- §12.4
- `if` --- §12.1
- `return` --- §12.8
- `subst` --- §12.14
- `switch` --- §12.5
- `try` --- §12.17
- `while` --- §12.3
- `yield` --- §12.16

##### Section 13 --- Procedure Commands

- `apply` --- §13.3
- `downlevel` --- §13.7
- `napply` --- §13.4
- `nproc` --- §13.2
- `proc` --- §13.1
- `rename` --- §13.5
- `tailcall` --- §13.6
- `uplevel` --- §13.8

##### Section 14 --- List Commands

- `dict` --- §14.15
- `info default` --- §14.16
- `join` --- §14.9
- `lappend` --- §14.7
- `lassign` --- §14.14
- `lindex` --- §14.2
- `list` --- §14.1
- `llength` --- §14.8
- `lrange` --- §14.3
- `lremove` --- §14.13
- `lreplace` --- §14.4
- `lreverse` --- §14.12
- `lsearch` --- §14.5
- `lsort` --- §14.6
- `split` --- §14.10

##### Section 15 --- String Commands

- `string compare` --- §15.1
- `string first` --- §15.2
- `string index` --- §15.3
- `string is` --- §15.8
- `string last` --- §15.2
- `string length` --- §15.5
- `string map` --- §15.9
- `string match` --- §15.7
- `string range` --- §15.4
- `string repeat` --- §15.6
- `string tolower` --- §15.10
- `string toupper` --- §15.10
- `string trim` --- §15.11
- `string trimleft` --- §15.11
- `string trimright` --- §15.11

##### Section 16 --- Regular Expression Commands

- `regexp Command` --- §16.6
- `regsub Command` --- §16.7

##### Section 17 --- Expression Command

- `expr` --- §17.1
- `fpclassify` --- §17.2

##### Section 18 --- Formatting Commands

- `base64` --- §18.3
- `binary` --- §18.4
- `format` --- §18.1
- `scan` --- §18.2

##### Section 19 --- I/O Commands

- `gets` --- §19.2
- `load` --- §19.4
- `puts` --- §19.1
- `source` --- §19.3
- `unload` --- §19.5

##### Section 20 --- File Commands

- `cd` --- §20.8
- `file dirname` --- §20.1
- `file exists` --- §20.4
- `file extension` --- §20.9
- `file join` --- §20.2
- `file nativename` --- §20.10
- `file normalize` --- §20.5
- `file pathtype` --- §20.11
- `file rootname` --- §20.12
- `file separator` --- §20.13
- `file split` --- §20.3
- `file tail` --- §20.6
- `file type` --- §20.14
- `pwd` --- §20.7

##### Section 21 --- Introspection Commands

- `info args` --- §21.8
- `info body` --- §21.7
- `info commands` --- §21.2
- `info context` --- §21.10
- `info exists` --- §21.1
- `info level` --- §21.5
- `info procs` --- §21.3
- `info script` --- §21.6
- `info varlinks` --- §21.11
- `info vars` --- §21.4

##### Section 22 --- Namespace Commands

- `namespace children` --- §22.5
- `namespace code` --- §22.7
- `namespace current` --- §22.1
- `namespace delete` --- §22.4
- `namespace eval` --- §22.2
- `namespace exists` --- §22.3
- `namespace export` --- §22.6
- `namespace import` --- §22.6
- `namespace parent` --- §22.5

##### Section 23 --- Package Commands

- `package forget` --- §23.6
- `package ifneeded` --- §23.3
- `package names` --- §23.5
- `package present` --- §23.4
- `package provide` --- §23.1
- `package require` --- §23.2
- `package scan` --- §23.9
- `package unknown` --- §23.7
- `package vcompare` --- §23.8
- `package versions` --- §23.5
- `package vsatisfies` --- §23.8

##### Section 24 --- Interpreter Commands

- `interp cancel` --- §24.1

##### Section 25 --- Time and System Commands

- `after` --- §25.4
- `clock seconds` --- §25.1
- `pid` --- §25.3
- `time` --- §25.2
- `update` --- §25.5
- `vwait` --- §25.6

#### G.2  Alphabetical index

- `after` --- §25.4
- `append` --- §11.3
- `apply` --- §13.3
- `array` --- §11.5
- `base64` --- §18.3
- `binary` --- §18.4
- `break` --- §12.6
- `catch` --- §12.9
- `cd` --- §20.8
- `clock seconds` --- §25.1
- `concat` --- §12.13
- `continue` --- §12.7
- `coroutine` --- §12.15
- `dict` --- §14.15
- `downlevel` --- §13.7
- `error` --- §12.10
- `eval` --- §12.11
- `exit` --- §12.12
- `expr` --- §17.1
- `file dirname` --- §20.1
- `file exists` --- §20.4
- `file extension` --- §20.9
- `file join` --- §20.2
- `file nativename` --- §20.10
- `file normalize` --- §20.5
- `file pathtype` --- §20.11
- `file rootname` --- §20.12
- `file separator` --- §20.13
- `file split` --- §20.3
- `file tail` --- §20.6
- `file type` --- §20.14
- `for` --- §12.2
- `foreach` --- §12.4
- `format` --- §18.1
- `fpclassify` --- §17.2
- `gets` --- §19.2
- `global` --- §11.6
- `if` --- §12.1
- `incr` --- §11.4
- `info args` --- §21.8
- `info body` --- §21.7
- `info commands` --- §21.2
- `info context` --- §21.10
- `info default` --- §14.16
- `info exists` --- §21.1
- `info level` --- §21.5
- `info procs` --- §21.3
- `info script` --- §21.6
- `info varlinks` --- §21.11
- `info vars` --- §21.4
- `interp cancel` --- §24.1
- `join` --- §14.9
- `lappend` --- §14.7
- `lassign` --- §14.14
- `lindex` --- §14.2
- `list` --- §14.1
- `llength` --- §14.8
- `load` --- §19.4
- `lrange` --- §14.3
- `lremove` --- §14.13
- `lreplace` --- §14.4
- `lreverse` --- §14.12
- `lsearch` --- §14.5
- `lsort` --- §14.6
- `namespace children` --- §22.5
- `namespace code` --- §22.7
- `namespace current` --- §22.1
- `namespace delete` --- §22.4
- `namespace eval` --- §22.2
- `namespace exists` --- §22.3
- `namespace export` --- §22.6
- `namespace import` --- §22.6
- `namespace parent` --- §22.5
- `napply` --- §13.4
- `nproc` --- §13.2
- `package forget` --- §23.6
- `package ifneeded` --- §23.3
- `package names` --- §23.5
- `package present` --- §23.4
- `package provide` --- §23.1
- `package require` --- §23.2
- `package scan` --- §23.9
- `package unknown` --- §23.7
- `package vcompare` --- §23.8
- `package versions` --- §23.5
- `package vsatisfies` --- §23.8
- `pid` --- §25.3
- `proc` --- §13.1
- `puts` --- §19.1
- `pwd` --- §20.7
- `regexp Command` --- §16.6
- `regsub Command` --- §16.7
- `rename` --- §13.5
- `return` --- §12.8
- `scan` --- §18.2
- `set` --- §11.1
- `source` --- §19.3
- `split` --- §14.10
- `string compare` --- §15.1
- `string first` --- §15.2
- `string index` --- §15.3
- `string is` --- §15.8
- `string last` --- §15.2
- `string length` --- §15.5
- `string map` --- §15.9
- `string match` --- §15.7
- `string range` --- §15.4
- `string repeat` --- §15.6
- `string tolower` --- §15.10
- `string toupper` --- §15.10
- `string trim` --- §15.11
- `string trimleft` --- §15.11
- `string trimright` --- §15.11
- `subst` --- §12.14
- `switch` --- §12.5
- `tailcall` --- §13.6
- `time` --- §25.2
- `try` --- §12.17
- `unload` --- §19.5
- `unset` --- §11.2
- `update` --- §25.5
- `uplevel` --- §13.8
- `upvar` --- §11.7
- `variable` --- §11.8
- `vwait` --- §25.6
- `while` --- §12.3
- `yield` --- §12.16

---

*End of Document*
