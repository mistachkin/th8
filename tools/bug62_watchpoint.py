#!/usr/bin/env python3
"""
Bug 62 hardware-watchpoint harness (see docs/internal/incomplete.md "Bug 62").

Bug 62 is a WILD WRITE of ~8 bytes onto interp->paSystemVar, an in-bounds
slot inside the (valid) interp allocation.  Because the destination is a live,
in-bounds address, NO redzone / guard-page / zone-check tool can catch it --
verified: ASan (no-mimalloc, external libs instrumented), libgmalloc, and the
maximal mimalloc DEBUG level (MI_SECURE=5 / MI_DEBUG=3 / MI_GUARDED) all come
up clean.  The only instrument that can catch an in-bounds write is a HARDWARE
watchpoint on the target address, which is also the lowest-perturbation option
(it does not shift the heap layout, so it preserves the fragile layout the
Heisenbug depends on).

paSystemVar is written legitimately in exactly ONE place -- Th8_DeclareSystemVar
(th8_vars.c, lazy Th8_HashNew) during interp setup -- and only read thereafter.
This harness breaks at the first Th8_IsSystemVar (by which point paSystemVar is
set and stable), arms an 8-byte write watchpoint on &interp->paSystemVar, then
runs.  Any writer that is NOT Th8_DeclareSystemVar is the Bug 62 culprit; its
backtrace is dumped.  The run is repeated (fresh process => fresh heap/TLS/DNS
jitter) until the wild write is caught or the iteration budget is spent.

PREREQUISITE (macOS): developer mode must be enabled or lldb cannot debug:
    sudo DevToolsSecurity -enable        # one-time, persists
(Symptom when disabled: "cannot get permission to debug processes", exit -1.)

Usage:
    tools/bug62_watchpoint.sh [iterations]        # convenience wrapper
  or directly:
    ulimit -s 65520
    lldb -b -o "command script import tools/bug62_watchpoint.py" \
            -o "wp62 200" -- ./bin/th8sh
Build ./bin/th8sh with debug info first (e.g. make ENABLE_TEST_KEY=1 debug).
"""
import lldb

TESTFILE = "tests/coverage/coverage_secure_load_blob_mcdc.tcl"
LEGIT_WRITER = "Th8_DeclareSystemVar"   # th8_vars.c -- the only sanctioned write

# Benign writers of the watched slot OTHER than the lazy init: at process exit
# Th8_DeleteInterp frees the interp block, and mimalloc's DEBUG mode memsets
# freed memory with the 0xDF fill -- which lands on the (now dead) paSystemVar
# slot.  That is teardown, not the bug.
FREE_FRAMES = ("Th8_DeleteInterp", "th8MimallocFree", "mi_free", "mi_page_free",
               "mi_heap_delete", "mi_heap_destroy", "mi_free_block")
# mimalloc DEBUG fill patterns (byte repeated 8x): FREED=0xDF, UNINIT=0xD0,
# PADDING=0xDE.  Bug 62's value is high-entropy, never one of these.
MI_FILL = (0xdfdfdfdfdfdfdfdf, 0xd0d0d0d0d0d0d0d0, 0xdededededededede)


def _bt(thread):
    return "\n".join("    " + str(f) for f in thread.frames)


def run_once(debugger, exe, testfile=TESTFILE):
    target = debugger.CreateTarget(exe)
    if not target:
        print("WP62: cannot create target", exe)
        return "ERROR"

    bp = target.BreakpointCreateByName("Th8_IsSystemVar")
    if bp.GetNumLocations() == 0:
        print("WP62: no location for Th8_IsSystemVar (need a -g build)")
        return "ERROR"

    if not target.LaunchSimple([testfile], None, "."):
        print("WP62: launch failed (developer mode disabled? "
              "run: sudo DevToolsSecurity -enable)")
        return "ERROR"

    process = target.GetProcess()
    armed = False
    wp = None
    watch_addr = 0

    while True:
        state = process.GetState()
        if state == lldb.eStateExited:
            return "EXIT:%d" % process.GetExitStatus()
        if state != lldb.eStateStopped:
            process.Continue()
            continue

        thread = process.GetSelectedThread()
        stop = thread.GetStopReason()

        if stop == lldb.eStopReasonBreakpoint and not armed:
            frame = thread.GetFrameAtIndex(0)
            interp = frame.FindVariable("interp")
            field = interp.GetChildMemberWithName("paSystemVar")
            addr = field.GetLoadAddress()
            werr = lldb.SBError()
            wp = field.Watch(True, False, True, werr)   # read=F, write=T
            if not wp:
                print("WP62: FAILED to set watchpoint:", werr)
                process.Kill()
                return "ERROR"
            watch_addr = addr
            print("WP62: armed write-watchpoint on &interp->paSystemVar="
                  "0x%x (cur=0x%x)" % (addr, field.GetValueAsUnsigned(0)))
            target.BreakpointDelete(bp.GetID())
            armed = True
            process.Continue()
            continue

        if stop == lldb.eStopReasonWatchpoint:
            frame = thread.GetFrameAtIndex(0)
            fn = frame.GetFunctionName() or "?"
            names = [(f.GetFunctionName() or "") for f in thread.frames[:10]]
            rerr = lldb.SBError()
            newval = process.ReadUnsignedFromMemory(watch_addr, 8, rerr) \
                if watch_addr else 0

            # Benign #1: the lazy init NULL -> valid hash (Th8_DeclareSystemVar).
            if any(LEGIT_WRITER in n for n in names):
                process.Continue()
                continue
            # Benign #2: teardown -- the interp block is being freed and
            # mimalloc's DEBUG fill (0xDF) is memsetting it.  Disable the
            # watchpoint (the slot is dead now) and let the process exit.
            if newval in MI_FILL or any(
                    any(ff in n for ff in FREE_FRAMES) for n in names):
                if wp:
                    wp.SetEnabled(False)
                process.Continue()
                continue

            print("=" * 70)
            print("WP62: *** WILD WRITE TO paSystemVar CAUGHT (Bug 62) ***")
            print("writer frame: %s   new value: 0x%016x" % (fn, newval))
            print(_bt(thread))
            print("=" * 70)
            process.Kill()
            return "CAUGHT"

        if stop == lldb.eStopReasonSignal:
            sig = thread.GetStopReasonDataAtIndex(0)
            print("WP62: signal %d in %s" % (
                sig, thread.GetFrameAtIndex(0).GetFunctionName()))
            print(_bt(thread))
            process.Kill()
            return "SIGNAL:%d" % sig

        process.Continue()


def wp62(debugger, command, result, internal_dict):
    # wp62 <iters> [testfile]   -- testfile defaults to the single crash file;
    # pass tests/all.tcl to watch across the full in-suite context.
    args = command.split()
    iters = int(args[0]) if args else 50
    testfile = args[1] if len(args) > 1 else TESTFILE
    tgt = debugger.GetSelectedTarget()
    exe = tgt.GetExecutable().fullpath if tgt else "./bin/th8sh"
    print("WP62: testfile=%s iters=%d" % (testfile, iters))
    for i in range(iters):
        r = run_once(debugger, exe, testfile)
        print("WP62 run %d/%d -> %s" % (i + 1, iters, r))
        if r == "CAUGHT" or r.startswith("SIGNAL"):
            print("WP62: STOPPING (caught event on run %d)" % (i + 1))
            return
    print("WP62: %d runs, no wild write caught" % iters)


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "command script add -f bug62_watchpoint.wp62 wp62")
