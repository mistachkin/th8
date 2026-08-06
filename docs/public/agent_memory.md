# TH8 as an Agent Memory Representation Language

*A design exploration of TH8's properties as a memory store for
LLM agents.  The patterns below are not a shipped library; they
are the small set of conventions that make the TH8 language
itself a useful **substrate** for that workload.*

## Motivation

Large language models and autonomous agents need a compact,
structured, extensible way to persist and consolidate memories
across sessions.  Existing formats force a choice between human
readability (Markdown), machine parseability (JSON / YAML), and
executable semantics (a programming language).  TH8's Tcl-derived
syntax offers all three at once -- a single representation that
is prose-readable to humans, list-parseable to tooling, and
evaluatable as code when the host chooses to evaluate it.  That
combination is unusual, and it is what this document explores.

The companion document, `agent_surface_area.md`, surveys what TH8
can compute once memories are loaded; this document is concerned
with the representation itself.

## Why Tcl Semantics Map Well to Memory

Tcl's foundational design — "everything is a string, but strings
have structure" — mirrors how memories actually work.  A memory is
simultaneously:

- **A flat narrative** (the string representation) — readable by
  humans and LLMs alike.
- **A structured record** (the parsed representation) — fields,
  tags, timestamps, confidence scores.
- **Something that can be evaluated in context** (the command
  representation) — a memory can be a rule, a procedure, or a
  conditional observation.

In most memory formats, you pick one of these.  In TH8, you get
all three for free.  A memory entry like:

```tcl
remember {
    context {debugging platform callback signatures}
    observation {xMalloc was being called with wrong argument order
                 after reordering}
    root_cause {positional struct initializer was missing the new
                xGetEnv field}
    resolution {added 0 placeholder to all 5 platform initializer
                structs}
    confidence 0.95
    tags {platform callbacks struct-layout regression}
}
```

...is simultaneously human-readable prose, machine-parseable
structure, AND executable code that can be evaluated to populate a
data model.  No separate parser, schema validator, or template
engine is needed — the language IS all of those.

## The DSL Advantage for Memory Consolidation

Tcl's command-as-DSL pattern means different TYPES of memories can
have different structures without a schema migration:

```tcl
# A factual memory
fact {TH8 uses CALG_RSA_SIGN (0x00002400) for signing keys} \
    -source {th8_snk.c line 55} -verified 2026-04-09

# A preference memory
preference {user wants all return values checked} \
    -strength strong \
    -reason {mission-critical systems, ISO submission}

# A causal chain
caused_by {test key loading silently failed} \
    -because {hooks.tcl clobbered the policy callback} \
    -because {Th8test_Unload unconditionally cleared xPolicyCb} \
    -fix {save/restore pattern in preeval install/uninstall}
```

Each of these is a different "shape" but they are all valid Tcl.
No schema evolution is needed.  The DSL commands define the
semantics.  New memory types can be added by defining new commands
— the language grows with the agent's needs.

## Compact Representation

Tcl's syntax is extremely information-dense for structured data.
Compare:

```json
{
  "type": "observation",
  "subject": "allocation",
  "detail": "Th8_Malloc panics on failure",
  "implication": "sandbox child interpreters crash the host"
}
```

versus:

```tcl
observe {Th8_Malloc panics on failure} \
    -implies {sandbox children crash the host}
```

The Tcl version is roughly 2-3x shorter (depending on how many
optional fields are present in the JSON) and avoids the
quote-and-brace noise that JSON imposes on string-heavy content.
For LLM context windows where every token has a cost, that
compression matters; the savings compound across thousands of
remembered facts.  The trade-off is that JSON has more uniform
tooling support outside Tcl-aware environments.

## Executable Memories

Memories are not just data — they are sometimes procedures.  "When
you see pattern X, do Y."  In a data format, you would need a
separate rules engine.  In TH8, the memory IS the rule:

```tcl
proc on_malloc_audit {file line} {
    if {[is_script_reachable $file $line]} {
        suggest "convert to Th8_AttemptMalloc with NULL check"
    }
}

proc on_platform_callback_change {} {
    remind "update all 5 platform struct initializers"
    remind "update Makefile.msc alongside Makefile"
    remind "run make fresh to regenerate stubs"
}
```

This collapses the distinction between "what I know" and "what I do
about what I know" — the memory and the response to the memory are
the same artifact.

## Memory Consolidation via Evaluation

When an agent accumulates many small memories over time, they need
consolidation — merging redundant entries, resolving contradictions,
updating stale facts.  In a data format, consolidation requires
external tooling.  In TH8, consolidation can be expressed as a
script:

```tcl
# Load all memory files
foreach f [glob memories/*.th8] {
    source $f
}

# Resolve contradictions
if {[memory exists {key uses 0xa400}] &&
    [memory exists {key must use 0x2400}]} {
    memory delete {key uses 0xa400}
    memory annotate {key must use 0x2400} \
        -supersedes {key uses 0xa400} \
        -reason {0xa400 is CALG_RSA_KEYX, not valid for signing}
}

# Emit consolidated output
memory export consolidated.th8
```

The consolidation logic itself is a TH8 script — debuggable,
auditable, and version-controlled alongside the memories it
operates on.

## Cryptographic Provenance

TH8's optional signed-only policy and RSA signature infrastructure
combine with the memory pattern to give each memory file a
companion `.b64sig` signature that proves three things:

- The memory was authored by a holder of the signing key.
- The memory has not been altered since signing.
- The memory's timestamp annotations (`<<notBefore:...>>`,
  `<<notAfter:...>>`) are enforceable -- expired or
  not-yet-valid memories can be automatically excluded from
  evaluation.

This matters most in multi-agent systems where one agent's
memories are consumed by another, and the receiving agent
needs to trust the upstream agent without re-deriving the
underlying claims.  TH8's signing layer is the same RSA + SHA-256
pipeline used for script signing; the contract is described in
[`security_model.md`](security_model.md).

## Sandboxed Memory Evaluation

When loading memories from untrusted sources (for example, a
memory file shared by another agent), TH8's default
zero-capability sandbox provides several layers of containment:

- **Allocation limit**: prevents a malicious memory file from
  exhausting host resources.
- **Step limit**: prevents infinite loops in executable memories.
- **No binary loading**: memories cannot load native code unless
  the embedder explicitly registers the `load` plugin AND opts
  into a signing trust anchor.
- **Base-path sandboxing**: file-system reach is bounded by the
  embedder-supplied path-prefix policy.
- **`Th8_EvalTrusted` vs `Th8_Eval`**: the host explicitly
  selects which memory files are evaluated under the
  full-capability policy versus the restricted one.

See [`security_model.md`](security_model.md) for the full
boundary catalogue.

## A speculative observation

A common anecdote from long-time Tcl users is that, after enough
time, they begin to "think in Tcl" -- modelling problems as a
sequence of commands taking option-value pairs.  Whether this
generalises to LLMs is an open empirical question; we are not
aware of a study that has measured it directly.  The conjecture
is suggestive enough to be worth flagging: if a representation
that maps neatly onto how humans and language models structure
declarative-plus-procedural knowledge has a token-cost edge as
well as a readability edge, that combination is worth exploring
in concrete experiments.  This document does not claim to have
performed that experiment.

## Practical Considerations

### Memory Vocabulary

A practical implementation would define a standard set of DSL
commands (the "memory vocabulary"):

- `fact` / `observation` / `hypothesis` — knowledge with confidence
- `preference` / `feedback` — user-specific guidance
- `caused_by` / `resolved_by` — causal chains
- `remember` / `forget` — lifecycle management
- `when` / `on_pattern` — reactive rules
- `context` / `project` / `session` — scoping

### File Organization

Each memory type could map to a file or directory:

```
memories/
    user/           # user preferences and profile
    project/        # project-specific knowledge
    feedback/       # correction history
    rules/          # executable policies
    index.th8       # consolidation script
    index.th8.b64sig
```

### Integration with Existing Systems

TH8 memory files can coexist with other formats.  A thin bridge
layer could import/export JSON, Markdown, or YAML while maintaining
the Tcl-native representation internally.  The agent works in TH8
and converts at the boundary.

## Conclusion

TH8 brings together five properties that, individually, exist in
other memory formats, but collectively are unusual to find in a
single substrate:

  *   Compact, human-readable surface syntax.
  *   Seamless DSL extensibility (new memory shapes are new
      commands, not schema migrations).
  *   Executable semantics (memories CAN be policies, not just
      records).
  *   Cryptographic provenance via the optional signing pipeline.
  *   Capability-restricted sandboxed evaluation.

These all arise from the same root: Tcl was designed from the
beginning to blur the line between data and code, and the TH8
implementation has hardened that property for production use.
Whether the combination turns out to be the right substrate for
your agent will depend on your stack, your trust model, and how
much your agent's reasoning maps to the command-with-options
shape.  We think it is worth a look.

For a concrete starting point, see
[`quickstart_scripting.md`](quickstart_scripting.md) for the TH8
language itself, [`security_model.md`](security_model.md) for the
capability and signing model, and the project
[`README.md`](../../README.md) for the broader context that
motivates this exploration.
