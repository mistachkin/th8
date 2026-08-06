# Conference Submission Abstract

**Title:**
TH8: An AI Helped Us Build a Tcl That Can't Do Anything (And That's the Point)

---

When an AI agent generates a script and asks your application to run it, what
happens next?  If the runtime is Tcl 8 or 9, the answer is: anything.  The
interpreter ships with `exec`, `open`, `socket`, `glob`, and direct file
system access compiled into the core.  Safe interpreters hide these commands,
but hiding is not the same as not having — and safe-interp bypasses have been
found repeatedly over thirty years.

TH8 is a from-scratch Tcl implementation that takes the opposite approach.  A
freshly created TH8 interpreter has *no inherent capabilities*.  It cannot
read files, open sockets, execute processes, allocate memory, or tell time.
Every interaction with the outside world is routed through an explicit platform
abstraction layer — a table of ~73 function pointers — that the embedder
composes at initialization.  If a callback is NULL, the capability does not
exist.  Not hidden.  Not disabled.  Absent at the object-code level.

This is not a toy.  TH8 implements Tcl 8.4 surface syntax with selected modern
extensions: a non-recursive evaluation engine (inspired by Miguel Sofer's NRE
work in Tcl 8.6), coroutines, tail call optimization, 60 math functions
including the full C99 and IEEE 754 classification sets, 20-subcommand `dict`,
`subst` with TIP #712 semantics, named-argument procedures, and compile-time
modularity with 17 independently gateable plugins.  It ships as a single
amalgamation file (one `.c`, one `.h`, ~71,000 code lines excluding comments
and blanks) that integrates into a host application with a `#include`.

And yes, we built it with an AI.

**Why another Tcl?**  Three reasons.  First, no formal specification of
Tcl has been published.  TH8 ships with a **working-text** language standard
--- the Tcl Language Standard v1 --- carrying 830 normative requirements in
the language standard alone (1,140 across the standard plus the
language-extensions, C-API, and internal-API specifications), each tagged
with a deterministic MD5-derived identifier traceable to test cases and
organized into three conformance profiles (Core, Standard, Full) modeled
on C's freestanding/hosted distinction.  4,289 tests (2,467 R-marker
citations) cover 97.19% of these requirements with a 100% pass rate on the
reference platform (4,280 passed + 9 platform-conditional skipped, 0 failed,
0 mutated).  The standard is not yet at formal-standards-body submission
quality --- structural and editorial work remains --- but the discipline
of a written specification with mechanically-checkable
requirement-to-test traceability delivers most of the practical value
today.  Second, Tcl
9.0 is approximately 200,000 lines of C.  Many embedding scenarios — firmware,
single-binary tools, sandboxed microservices, agentic AI tool execution —
cannot accommodate that runtime.  TH8's minimal build (variables, expressions,
and loading all disabled) is 508 KB.  Third, the security properties we need
cannot be retrofitted onto an existing implementation.  TH8's preGetData and
preEval callbacks intercept data inside the core evaluator before parsing
begins; the signed-only policy verifies RSA-SHA512 signatures (made with
16384-bit RSA keys) on raw file bytes before line-ending translation; the
internal-representation cache is woven into the value lifecycle; the platform
abstraction mediates `malloc` itself.  These are not hooks you can bolt on
from outside.

**But doesn't it need a bytecode compiler?**  No.  TH8 maintains a
per-interpreter internal-representation cache that stores converted values
(integer, double, boolean, list, dict, bigint, command pointer) keyed by their
string representation.  Once a string is parsed as an integer, every
subsequent use skips the parse.  This eliminates the primary cost that bytecode
compilation avoids — repeated re-parsing of operands — without introducing a
second program representation.  For embedding use cases, startup latency
dominates throughput; TH8 interpreters start in microseconds with no
compilation pass and no JIT warmup.  And the absence of bytecode is itself a
security property: there is no compiled form that could diverge from the
inspectable source text, closing an entire class of TOCTOU vulnerabilities in
the pre-evaluation security hooks.

**The AI collaboration.**  TH8 was developed in sustained multi-hour sessions
between a human architect and Anthropic's Claude via Claude Code.  The roles
were distinct: the human made all architectural decisions, enforced platform
abstraction discipline, arbitrated correctness ("Tcl 8.4 says X, Eagle says
Y"), and reviewed every line.  The LLM drafted implementation code, generated
test suites (often 30–60 tests per command, catching bugs the human had not
anticipated), maintained the 830-requirement standard, performed large-scale
refactoring (extracting 11,000 lines into 17 plugin files while preserving all
cross-references), and wrote documentation including security analyses and this
paper.

The approach has real limitations.  The LLM repeatedly placed OS-specific
functions in the wrong platform file; the rule "each file uses only its
abstraction layer's APIs" required constant enforcement.  Subtle semantic
correctness (TH1 and TH8 return codes are swapped for BREAK and RETURN; the
LLM got the mapping backwards more than once) demanded immediate correction.
The LLM proposed over-engineered solutions where simple `#if` guards sufficed,
and under-estimated the impact of changes (suggesting that disabling variables
should disable control flow, when `[if]`/`[for]`/`[catch]` work fine without
them).  Build system integration — Makefile dependencies, amalgamation
generation, stubs table gating — required significant human intervention.

But the force-multiplier effect was real.  A solo developer does not normally
produce 71,000 lines of C, 830 formally marked requirements, 4,289 tests, 18
documentation files, two man pages, six fuzz harnesses, and a conference paper
in the span of weeks.  The LLM made that ambition feasible — not by replacing
the architect, but by handling the throughput-limited work (test generation,
documentation maintenance, mechanical refactoring) that would otherwise have
consumed months.

The meta-irony is not lost on us: we used an AI to build a runtime designed to
safely execute AI-generated code.  Every security mechanism in TH8 was tested
against the question, "could the LLM that helped write this also exploit it?"
Coverage-guided fuzzing proved essential for validating LLM-generated code:
AFL++ found 24 crash inputs in a parser the LLM had reviewed and declared safe.

This talk will cover TH8's zero-capability architecture, the Tcl Language
Standard v1, the performance story (caching vs. bytecode), compile-time
modularity, the signed-only script policy, agentic AI integration patterns,
and an honest assessment of what LLM-assisted systems programming looks like
in practice.

TH8 is open source, builds on Linux/macOS/Windows, and passes its full test
suite under GCC, Clang, and MSVC.
