# Upstream report draft: NULL `heap_main` deref in `_mi_thread_done` on re-entrant thread teardown

Draft for filing against upstream mimalloc (v3.3.2).  Kept next to the local
patch (`patches/src/init.c`) so it stays in sync; delete once upstream lands a
fix and the patch is dropped.

## Summary

`_mi_thread_done()` can dereference a NULL `subproc->heap_main` when a thread's
mimalloc teardown runs twice (an explicit `mi_thread_done()` followed by the
pthread-TSD destructor's `_mi_thread_done()`).  A `MI_DEBUG`-enabled build aborts
on `mi_assert_internal(subproc->heap_main != NULL)`; a release build performs a
near-NULL atomic store instead (SIGSEGV / silent corruption).  Related to the
re-entrancy discussed in issue #699.

## Affected version

v3.3.2 (static build, `MI_SECURE=5 MI_DEBUG=3 MI_GUARDED=1` on macOS/arm64;
release path reasoned through, see below).

## Root cause

In `_mi_thread_done()`:

```c
mi_heap_stat_decrease(_mi_subproc_heap_main(tld->subproc), threads, 1);
```

`_mi_subproc_heap_main()` (init.c) returns `subproc->heap_main`, and when that is
NULL it calls the run-once `mi_heap_main_init()` and then asserts the pointer is
non-NULL.  But `mi_heap_main_init()` is guarded by `if (heap_main.subproc == NULL)`,
which is already false after process startup, so on a *teardown* re-entry it is a
no-op and cannot restore `subproc->heap_main`.  Hence:

- **Debug** (`MI_DEBUG>0`): the following `mi_assert_internal(...heap_main != NULL)`
  fires → abort (SIGTRAP / `EXC_BREAKPOINT`).
- **Release** (`-DNDEBUG`, default `MI_STAT=0`): the assert is gone, but
  `mi_heap_stat_decrease(NULL, threads, 1)` expands to
  `__mi_stat_decrease_mt(&((mi_heap_t*)NULL)->stats.threads, 1)`.  Because
  `__mi_stat_decrease_mt` is an **extern** function (stats.c), the call is emitted
  regardless of `MI_STAT`, and it unconditionally runs
  `mi_atomic_addi64_relaxed(&stat->current, -1)` on the near-NULL address → fault.

So the release build is not immune; it just faults later and less visibly.

## How it is reached

A consumer that (a) uses per-thread heaps and (b) calls `mi_thread_done()`
explicitly at thread teardown (for deterministic reclamation) will, on that same
thread's exit, also trigger mimalloc's registered pthread-TSD destructor
`mi_pthread_done` → `_mi_thread_done()` a second time.  The second pass observes
the already-torn-down `subproc->heap_main` as NULL.

Backtrace (debug):

```
abort
  _mi_assert_fail            (options.c)
  _mi_subproc_heap_main      (init.c)
  _mi_thread_done            (init.c)
  _pthread_tsd_cleanup
  _pthread_exit
  thread_start
```

## Minimal reproduction (sketch)

```c
#include <mimalloc.h>
#include <pthread.h>

static void* worker(void* arg) {
  mi_heap_t* h = mi_heap_new();
  void* p = mi_heap_malloc(h, 64);
  mi_free(p);
  mi_thread_done();      // explicit, deterministic teardown
  return NULL;           // thread exits -> TSD destructor calls _mi_thread_done() again
}

int main(void) {
  for (int i = 0; i < 1000; i++) {
    pthread_t t; pthread_create(&t, NULL, worker, NULL); pthread_join(t, NULL);
  }
  return 0;
}
```

Build with `MI_DEBUG=3` to see the deterministic assert; the release path exhibits
the near-NULL store under the same race.  (The failure is timing-sensitive; a loop
plus concurrent load reproduces it more reliably than a single thread.)

## Suggested fix

Make the thread-count statistic tolerant of a NULL main heap during teardown
(read `heap_main` directly and skip the decrement when NULL), and/or make
`mi_thread_done()` idempotent so a subsequent TSD-destructor pass is a no-op.
The local stopgap patch takes the former approach; see `patches/src/init.c`.
