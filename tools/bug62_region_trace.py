#!/usr/bin/env python3
"""
Bug 62 region write-tracer.

Bug 62 corrupts interp->paSystemVar (offset 4656) with a high-entropy 8-byte
value.  paSystemVar sits immediately after the secure-persist token gate:

    nSecurePersistToken  @ 4640
    nSecurePersistOk     @ 4648
    paSystemVar          @ 4656   <-- victim
    paCache              @ 4664

The token is a random (high-entropy) 8-byte value, so an 8-byte overrun of the
token write would land exactly on paSystemVar with a matching value signature.
This tracer arms HARDWARE write-watchpoints on all three gate fields plus
paSystemVar and logs EVERY writer during a full run, so we can see
deterministically whether anything other than the sanctioned writers
(Th8_EnableSecurePersist scalar stores, Th8_DeclareSystemVar init, teardown
free-fill) ever touches the region.

Usage:
    ulimit -s 65520
    lldb -b -o "command script import tools/bug62_region_trace.py" \
            -o "trace62" -- ./bin/th8sh
"""
import lldb

TESTFILE = "tests/coverage/coverage_secure_load_blob_mcdc.tcl"
FIELDS = ("nSecurePersistToken", "nSecurePersistOk", "paSystemVar", "paCache")

# The complete set of SANCTIONED writers of this region, established by a full
# instruction-level trace: field init, the secure-persist token gate, cache
# clear, and the teardown free-fill.  Any writer NOT in this set is an anomaly
# and a Bug 62 candidate.
SANCTIONED = ("Th8_DeclareSystemVar", "Th8_EnableSecurePersist",
              "th8ClearCache", "th8CacheFinish", "th8MimallocFree",
              "mi_free_block_local", "mi_free", "_platform_memset",
              "Th8_DeleteInterp")


def _sanctioned(names):
    return any(any(s in n for s in SANCTIONED) for n in names)


def run_once(debugger, exe, verbose):
    target = debugger.CreateTarget(exe)
    bp = target.BreakpointCreateByName("Th8_IsSystemVar")
    if bp.GetNumLocations() == 0:
        print("TRACE62: no location for Th8_IsSystemVar (need -g build)")
        return "ERROR"
    if not target.LaunchSimple([TESTFILE], None, "."):
        print("TRACE62: launch failed (developer mode disabled?)")
        return "ERROR"

    process = target.GetProcess()
    armed = False
    nwrite = 0
    anomalies = 0

    while True:
        state = process.GetState()
        if state == lldb.eStateExited:
            return "EXIT:%d writes=%d anomalies=%d" % (
                process.GetExitStatus(), nwrite, anomalies)
        if state != lldb.eStateStopped:
            process.Continue()
            continue

        thread = process.GetSelectedThread()
        stop = thread.GetStopReason()

        if stop == lldb.eStopReasonBreakpoint and not armed:
            frame = thread.GetFrameAtIndex(0)
            interp = frame.FindVariable("interp")
            for name in FIELDS:
                fld = interp.GetChildMemberWithName(name)
                werr = lldb.SBError()
                wp = fld.Watch(True, False, True, werr)
                if not wp:
                    print("TRACE62: FAILED to watch %s: %s" % (name, werr))
            target.BreakpointDelete(bp.GetID())
            armed = True
            process.Continue()
            continue

        if stop == lldb.eStopReasonWatchpoint:
            nwrite += 1
            frame = thread.GetFrameAtIndex(0)
            fn = frame.GetFunctionName() or "?"
            names = [(f.GetFunctionName() or "") for f in thread.frames[:10]]
            if not _sanctioned(names):
                anomalies += 1
                print("=" * 70)
                print("TRACE62: *** ANOMALOUS WRITE to region (Bug 62?) ***")
                print("writer: %s" % fn)
                for f in thread.frames[:12]:
                    print("    " + str(f))
                print("=" * 70)
            elif verbose:
                print("TRACE62: write by %s" % fn)
            process.Continue()
            continue

        if stop == lldb.eStopReasonSignal:
            sig = thread.GetStopReasonDataAtIndex(0)
            print("TRACE62: SIGNAL %d in %s" % (
                sig, thread.GetFrameAtIndex(0).GetFunctionName()))
            for f in thread.frames[:12]:
                print("    " + str(f))
            process.Kill()
            return "SIGNAL:%d" % sig

        process.Continue()


def trace62(debugger, command, result, internal_dict):
    args = command.split()
    iters = int(args[0]) if args else 1
    verbose = "-v" in args
    tgt = debugger.GetSelectedTarget()
    exe = tgt.GetExecutable().fullpath if tgt else "./bin/th8sh"
    total_anom = 0
    for i in range(iters):
        r = run_once(debugger, exe, verbose)
        print("TRACE62 run %d/%d -> %s" % (i + 1, iters, r))
        if "anomalies=" in r:
            total_anom += int(r.split("anomalies=")[1])
        if r.startswith("SIGNAL") or (
                "anomalies=" in r and int(r.split("anomalies=")[1]) > 0):
            print("TRACE62: STOPPING (anomaly/signal on run %d)" % (i + 1))
            return
    print("TRACE62: %d runs, %d anomalous region writes total" % (
        iters, total_anom))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "command script add -f bug62_region_trace.trace62 trace62")
