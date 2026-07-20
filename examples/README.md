# TH8 Pure-Computation Example Scripts

A suite of small, self-contained TH8 scripts demonstrating idiomatic
scripting through classic algorithms and pure computation.  Each file
is standalone (no `package require`, no external dependencies, no I/O
beyond printing its final results), carries a one-line purpose in its
header, and is signed for the signed-only script policy.

These double as approachable, copy-pasteable reference material for
documentation and onboarding, and as incidental exercise material for
the interpreter.

## Running an example

Each script prints a short demonstration of its procedures when
evaluated directly:

```
./bin/th8sh examples/fibonacci.tcl
```

Under the signed-only policy the interpreter verifies each script's
`.b64sig` signature before evaluating it.  After editing any example,
re-sign it with `bash tools/signScript.sh examples/<file>.tcl`.

## Index

| Script | Purpose | Key procedures |
|--------|---------|----------------|
| `fibonacci.tcl` | Exact Fibonacci numbers (iterative, arbitrary precision) | `fib`, `fibSequence` |
| `factorial.tcl` | Factorial and binomial coefficients | `factorial`, `choose` |
| `primes.tcl` | Prime testing and the Sieve of Eratosthenes | `isPrime`, `sieve` |
| `gcdlcm.tcl` | Euclidean GCD, LCM, and list GCD | `gcd`, `lcm`, `gcdList` |
| `collatz.tcl` | The Collatz (3n+1) sequence and stopping time | `collatzSequence`, `collatzSteps` |
| `ackermann.tcl` | The Ackermann-Peter recursive function | `ackermann` |
| `sqrt.tcl` | Integer and floating square roots by Newton's method | `isqrt`, `newtonSqrt` |
| `pi.tcl` | Digits of pi via Machin's formula (pure integer math) | `pi`, `arctanTerm` |
| `pascal.tcl` | Pascal's triangle (incremental, no factorials) | `pascalRow`, `pascalTriangle` |
| `baseconv.tcl` | Integer conversion between number bases (2..36) | `toBase`, `fromBase` |
| `sorting.tcl` | Quicksort and mergesort from scratch | `quicksort`, `mergesort` |
| `strings.tcl` | Palindrome, anagram, Caesar cipher, run-length coding | `isPalindrome`, `isAnagram`, `caesar`, `runLengthEncode`, `runLengthDecode` |

## Conventions

Every procedure lives in the `::examples` namespace so the files can be
sourced together without clashing.  The code follows the project
scripting style guide (`docs/pending/tcl_eagle_th8_style_guide.md`):
2-space indent, the `then` keyword on every `if`, braced `expr`,
camelCase procedure names, and a `# name --` documentation block above
each procedure.

Several examples deliberately show off TH8's arbitrary-precision
integers -- `factorial 30`, `fib 100`, `isqrt` of a large perfect
square, and `pi 50` (fifty correct digits) are all exact with no
overflow and no floating-point rounding.
