# LadyBird DOM Examples — TH8 Smoke Harness

The LadyBird DOM scripting examples themselves ship with the LadyBird
integration (`Examples/TH8/dom/*.html` in the `mistachkin_th8` branch),
because they need the browser's DOM bridge to run.  This directory holds
a browser-free **smoke harness** that verifies the *TH8 logic* of those
examples with `./bin/th8sh`, so the example code can be exercised and
kept honest without building LadyBird.

## Files

| File | Role |
|------|------|
| `dom_bridge_mock.tcl` | Test-only mock of the LadyBird DOM bridge command surface (`register_dom_commands` in `Libraries/LibWeb/TH8/DOMBridge.cpp`): registers `dom::document` / `dom::console` / `dom::release` / `dom::eval_js` and the node/element handle subcommands over a small in-memory DOM, with synchronous event dispatch. |
| `dom_examples.tcl` | The example procedures (`::examples::domLookup`, `domCreateInsert`, `domTraverse`, `domEvents`, `domCrossEval`) — the same TH8 embedded in the shipped HTML pages, using only the bridge command surface. |
| `dom_smoke.tcl` | Builds a fixture DOM through the mocked commands, runs each example, dispatches a synthetic click, and asserts the observed results. |

## Running

From the TH8 repository root:

```
./bin/th8sh examples/dom/dom_smoke.tcl
```

Expected output ends with `==== passed=5 failed=0 ====`; the script exits
non-zero on any failure.

## Fidelity

The mock mirrors the real bridge's contract, including the document
handle carrying both document- and node-scoped subcommands (matching the
`DOMBridge.cpp` fix that delegates document ops from
`object_ensemble_command` when the handle is the `Document`), and the
event dictionary keys produced by `serialize_event_to_dict`
(`type`, `bubbles`, `cancelable`, `eventPhase`, `target`,
`currentTarget`, `timeStamp`).  It is a smoke aid, not a full DOM: CSS
selector support is limited to `#id`, `.class`, and bare tag names, and
`dom::eval_js` returns a deterministic marker rather than running
JavaScript.
