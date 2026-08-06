# mimalloc vendoring & local patches

TH8 links mimalloc **statically** into `libth8`, so mimalloc's code is part of
the TH8 binary.  Correctness issues in it are therefore TH8's, and must be
**fixed in a tracked patch here** -- never silenced with a Valgrind/sanitizer
suppression.  (Suppressions in `tools/data/th8.supp` are reserved for
*dynamically*-linked libraries such as system OpenSSL, whose object code is not
ours to change.)  This policy applies to **any** external code compiled in
statically.

## Layout (same model as libtommath)

- `externals/mimalloc/vendor/` -- **pristine** upstream mimalloc (v3.3.2); never
  edited.
- `externals/mimalloc/patches/` -- whole-file replacements, each stored at its
  **vendor-relative path** (e.g. `patches/src/prim/unix/prim.c`).
- `externals/mimalloc/build/` -- generated: a copy of `vendor/{src,include}`
  with `patches/` overlaid.  **The TH8 build compiles mimalloc from `build/`**
  (`MI_SRC`/`MI_INC` in the makefiles).

Regenerate `build/` after changing `vendor/` or `patches/`:

```
make mimalloc_vendor        # runs tools/mimalloc_vendor.tcl
```

To upgrade mimalloc: replace `vendor/` with the new upstream, re-check that each
`patches/` file still applies cleanly against it, then `make mimalloc_vendor`.

## Patches

### src/prim/unix/prim.c -- `unix_detect_thp()` uninitialised read

`unix_detect_thp()` reads `/sys/kernel/mm/transparent_hugepage/enabled` into a
**non**-zero-initialised `char buf[32]` and then searched the FULL 32 bytes with
`_mi_strnstr(buf, 32, "[never]")`, even though only `nread` bytes were read.
`_mi_strnlen` (called by `_mi_strnstr`) then scanned the uninitialised tail for a
NUL terminator -- a real read of uninitialised stack memory that Valgrind flags
("Conditional jump or move depends on uninitialised value(s)"), failing
`make valgrind`.

The fix bounds the search to `nread` (already guaranteed `>= 1`):
`_mi_strnstr(buf, (size_t)nread, "[never]")`.  Benign in release (the tail rarely
spells `[never]`) but a genuine correctness fix; worth reporting upstream.

### src/init.c -- NULL `heap_main` deref/abort during thread teardown

`_mi_thread_done()` decrements a thread-count statistic with
`mi_heap_stat_decrease(_mi_subproc_heap_main(tld->subproc), threads, 1)`.
`_mi_subproc_heap_main()` asserts `subproc->heap_main != NULL`; if that heap is
observed NULL during a worker thread's pthread-TSD cleanup, the debug build
(`MI_DEBUG=3`, as TH8 builds it) **aborts** via `mi_assert_internal`
(SIGTRAP / EXC_BREAKPOINT), and a **release** build would write through the NULL
heap -- `mi_heap_stat_decrease` expands to `__mi_stat_decrease_mt(&(heap)->stats.
threads, ...)`, an **extern** function (stats.c), so the compiler emits the call
even at `MI_STAT=0`, and its `mi_atomic_addi64_relaxed(&stat->current, -1)` then
faults on the near-NULL `&NULL->stats.threads`.  Release is **not** rescued by
the lazy `mi_heap_main_init()` inside `_mi_subproc_heap_main`: that init is a
run-once no-op once `heap_main.subproc != NULL` (true after process startup), so
it cannot restore `subproc->heap_main` during teardown -- which is precisely why
the debug assert on the very next line fires.  So this is a **latent release
crash**, not a debug-only cosmetic: the assert just makes the debug build fault
earlier and louder.  Seen once as a flaky, load-dependent crash on a TH8
worker-thread exit (mimalloc v3.3.2; see incomplete.md Bug 72).

How TH8 reaches it: TH8 uses mimalloc's **per-thread heap API** (a `mi_heap_new()`
per worker) and, at thread teardown, `Th8_ThreadDone()` calls `mi_thread_done()`
**explicitly** so the heap is reclaimed deterministically rather than at some
later GC point.  The thread then exits normally, and mimalloc's own
pthread-TSD destructor calls `mi_thread_done()` **a second time**.  The first
pass tears the thread's mimalloc state down (leaving `heap_main` observable as
NULL for this path); the second pass then dereferences it -- a double
`mi_thread_done()` that upstream's teardown does not guard (the surrounding code
cites upstream issue #699).  So it is an upstream thread-teardown robustness
issue, exposed by a legitimate explicit-teardown usage, **not** a TH8 misuse.

The patch reads `heap_main` directly (no main-heap re-init while tearing a thread
down) and skips the non-critical statistic when it is NULL, avoiding both the
debug abort and the release near-NULL store.  Stopgap until upstream fixes it; a
submittable write-up with a minimal repro is drafted in
`upstream-init-report.md` (next to this file) -- file it against upstream, then
drop both it and the patch once upstream lands a fix.
