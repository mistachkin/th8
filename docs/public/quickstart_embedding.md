# TH8 Embedding Quick-Start Guide

How to integrate the TH8 scripting engine into your C application.
Covers initialization, evaluation, variables, custom commands, platform
callbacks, resource limits, and security.

---

## 1  Overview

TH8 is designed to be embedded.  Your application:

1. Provides a **platform layer** (memory, I/O, filesystem callbacks).
2. Creates an **interpreter**.
3. **Evaluates** scripts and reads results.
4. Optionally registers **custom commands**.
5. **Destroys** the interpreter when done.

The entire API is in `th8.h`.  Link against `libth8.a` (static) or
`th8.dll`/`libth8.so` (dynamic).

---

## 2  Minimal Example

```c
#include "th8.h"

int main(void) {
    Th8_Platform *pPlat;
    Th8_Interp   *interp;
    int           rc;
    const char   *zResult;
    size_t        nResult;

    /* Step 1: Initialize the platform. */
    pPlat = Th8_ClonePlatform(NULL);   /* default platform */
    if (Th8_Initialize(pPlat) != TH8_OK) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }

    /* Step 2: Create an interpreter. */
    interp = Th8_CreateInterp(pPlat);
    if (!interp) {
        fprintf(stderr, "interp creation failed\n");
        Th8_Finalize(pPlat);
        return 1;
    }

    /* Step 3: Register the standard language commands. */
    Th8_RegisterLanguage(interp);

    /* Step 4: Evaluate a script. */
    rc = Th8_Eval(interp, 0,
            "expr {6 * 7}", TH8_NOLEN, NULL, 0);

    /* Step 5: Read the result. */
    zResult = Th8_GetResult(interp, &nResult);
    if (rc == TH8_OK) {
        printf("Result: %.*s\n", (int)nResult, zResult);
    } else {
        fprintf(stderr, "Error: %.*s\n", (int)nResult, zResult);
    }

    /* Step 6: Clean up. */
    Th8_DeleteInterp(interp);
    Th8_Finalize(pPlat);
    Th8_FreePlatform(pPlat);
    return (rc == TH8_OK) ? 0 : 1;
}
```

Build:
```
cc -o myapp myapp.c -Isrc -Lbin -lth8
```

---

## 3  Initialization

### Platform

A `Th8_Platform` struct holds function pointers for everything
TH8 cannot do portably: memory allocation, I/O, file access,
time, entropy, mutexes, etc.

```c
/* Start with the built-in defaults (libc + POSIX/Win32). */
Th8_Platform *pPlat = Th8_ClonePlatform(NULL);

/* Optionally override specific callbacks. */
pPlat->xOutput = my_output_handler;
pPlat->xGetData = my_file_reader;

/* Initialize (sets up global state, mutexes). */
Th8_Initialize(pPlat);
```

The built-in platform provides:
- Memory via `malloc`/`realloc`/`free` (or mimalloc)
- File I/O via `open`/`read` (POSIX) or `CreateFileA` (Win32)
- Console I/O via `write` (POSIX) or `WriteConsoleW` (Win32)
- Entropy via `/dev/urandom` (POSIX) or `RtlGenRandom` (Win32)
- Time via `gettimeofday` (POSIX) or `GetSystemTimeAsFileTime` (Win32)

### Interpreter

```c
Th8_Interp *interp = Th8_CreateInterp(pPlat);
Th8_RegisterLanguage(interp);  /* registers all built-in commands */
```

`Th8_RegisterLanguage` is separate so embedders can selectively
omit it and register only a subset of commands.

---

## 4  Evaluating Scripts

### From a string

```c
int rc = Th8_Eval(interp, 0, zScript, nScript, NULL, 0);
```

Parameters:
- `iFrame` --- 0 for global scope
- `zScript`, `nScript` --- script text and length (`TH8_NOLEN`
  for NUL-terminated)
- `zName`, `nName` --- optional source name for error messages

### From a file

```c
int rc = Th8_EvalFile(interp, "config.tcl", TH8_NOLEN);
```

Uses the platform's `xGetData` callback to read the file.

### Trusted evaluation

`Th8_EvalTrusted` bypasses the signed-only script policy.  Use it
for embedder-provided scripts that don't have signatures:

```c
int rc = Th8_EvalTrusted(interp, 0, zScript, nScript, NULL, 0);
```

### Reading results

```c
const char *zResult;
size_t nResult;

zResult = Th8_GetResult(interp, &nResult);
/* zResult is valid until the next Th8_Eval or Th8_SetResult. */
```

Convert to C types:

```c
int iVal;
Th8_ToInt(interp, zResult, nResult, &iVal);

th8_int64_t wVal;
Th8_ToWideInt(interp, zResult, nResult, &wVal);

double dVal;
Th8_ToDouble(interp, zResult, nResult, &dVal);
```

---

## 5  Variables

### Setting from C

```c
Th8_SetVar(interp, "::config(host)", TH8_NOLEN,
        "localhost", TH8_NOLEN);

Th8_SetVar(interp, "::config(port)", TH8_NOLEN,
        "8080", 4);
```

### Reading from C

```c
if (Th8_GetVar(interp, "::result", TH8_NOLEN) == TH8_OK) {
    const char *z = Th8_GetResult(interp, NULL);
    printf("result = %s\n", z);
}
```

### Checking existence

```c
if (Th8_ExistsVar(interp, "::config(host)", TH8_NOLEN)) {
    /* variable exists */
}
```

---

## 6  Custom Commands

### Registering a command

```c
static int
myCmd(Th8_Interp *interp, void *ctx,
      int argc, const char **argv, size_t *argl)
{
    if (argc != 2) {
        return Th8_WrongNumArgs(interp, "mycommand argument");
    }

    /* argv[0] = command name, argv[1] = argument */
    /* argl[0], argl[1] = corresponding lengths */

    /* Do something with the argument... */
    Th8_SetResult(interp, argv[1], argl[1]);
    return TH8_OK;
}

/* Register it: */
Th8_CreateCommand(interp, "mycommand", myCmd, NULL, NULL, NULL);
```

### Setting results

```c
/* String result: */
Th8_SetResult(interp, "hello", 5);

/* Integer result: */
Th8_SetResultInt(interp, 42);

/* Wide integer result: */
Th8_SetResultWideInt(interp, 1234567890123LL);

/* Double result: */
Th8_SetResultDouble(interp, 3.14159);

/* Error: */
Th8_SetResult(interp, "invalid input", TH8_NOLEN);
return TH8_ERROR;
```

### Context pointer

The `ctx` parameter lets you attach state to a command:

```c
typedef struct {
    int counter;
} MyState;

static int
counterCmd(Th8_Interp *interp, void *ctx,
           int argc, const char **argv, size_t *argl)
{
    MyState *p = (MyState *)ctx;
    p->counter++;
    Th8_SetResultInt(interp, p->counter);
    return TH8_OK;
}

MyState state = {0};
Th8_CreateCommand(interp, "counter", counterCmd, &state,
        NULL, NULL);
```

---

## 7  Platform Callbacks

The `Th8_Platform` struct has approximately 73 callback slots.
All are optional --- NULL slots are silently skipped or use
built-in fallbacks.

### Essential callbacks (most embedders need these)

| Callback | Purpose |
|----------|---------|
| `xMalloc` | Allocate memory |
| `xRealloc` | Resize allocation |
| `xFree` | Free memory |
| `xGetData` | Read a named data source (file, resource, URL) |
| `xDataExists` | Check if a data source exists |
| `xOutput` | Write to stdout (or equivalent) |
| `xOutputError` | Write to stderr (or equivalent) |
| `xInput` | Read a line of input |

### Security callbacks

| Callback | Purpose |
|----------|---------|
| `xRandomBytes` | Cryptographic random bytes |
| `xGetStackBounds` | Stack overflow detection |

### Filesystem callbacks

| Callback | Purpose |
|----------|---------|
| `xNormalizePath` | Resolve `.` and `..` in paths |
| `xGetCwd` | Get current working directory |
| `xSetCwd` | Change working directory |
| `xGetRealPath` | Canonical absolute path |
| `xSameFile` | Compare two paths |
| `xGetExePath` | Executable path |
| `xGetRootPath` | Sandbox root |

### Lifecycle callbacks

| Callback | Purpose |
|----------|---------|
| `xInitialize` | One-time global setup |
| `xFinalize` | One-time global teardown |
| `xDeleteInterp` | Per-interpreter cleanup |

### Advanced callbacks

| Callback | Purpose |
|----------|---------|
| `xLoad` / `xUnload` | Dynamic library loading |
| `xTimeMs` / `xTimeUs` | Monotonic time |
| `xSleep` | Thread sleep |
| `xKeyValue` | Persistent key-value storage |
| `xNeedMemory` | Second-chance allocator |
| `xEmitTrace` | Debug trace output |
| `xPanic` | Fatal error handler |

### Merging platforms

Override only what you need:

```c
Th8_Platform custom = {0};
custom.xOutput = my_output;
custom.xGetData = my_file_reader;

Th8_Platform *pPlat = Th8_ClonePlatform(NULL);
Th8_MergePlatform(pPlat, &custom);
```

---

## 8  Resource Limits

Protect against runaway scripts:

```c
/* Limit to 10 million evaluation steps. */
Th8_SetStepLimit(interp, 10000000);

/* Limit result string to 10 MB. */
Th8_SetResultLimit(interp, 10 * 1024 * 1024);

/* Limit total memory to 50 MB. */
Th8_SetAllocLimit(interp, 50 * 1024 * 1024);
```

### Cancellation

Cancel a running script from another thread:

```c
/* From the control thread: */
Th8_CancelEval(interp, "operation timed out", TH8_NOLEN);

/* The script receives TH8_ERROR with the cancel message. */
/* Reset after handling: */
Th8_ResetCancel(interp);
```

### Freeze / Thaw

Temporarily suspend an interpreter (preserves all state):

```c
Th8_Freeze(interp);      /* script cannot run */
/* ... do maintenance ... */
Th8_Thaw(interp);        /* script can run again */
```

---

## 9  Script Security

### Signed-only mode

TH8 can require all scripts to have valid Harpy (RSA) signatures:

```c
#include "th8.h"

/* Enable the signed-only policy. */
void *pPolicyCtx = NULL;
Th8_EnableSignedPolicy(interp, &pPolicyCtx, 1);

/* Now Th8_EvalFile will reject unsigned scripts. */
/* Use Th8_EvalTrusted for embedder-provided scripts. */
```

### Secure variables

Encrypted in-memory variables for sensitive data:

```tcl
secure create password "hunter2"
set password                  ;# returns plaintext (briefly)
secure delete password        ;# securely zeroes the key slot
```

From C:

```c
Th8_EnableCrypto(interp, 1);
/* Scripts can now use [secure create/delete]. */
```

---

## 10  Debugging

### Debug callback

```c
static int
myDebugHook(Th8_Interp *interp, void *ctx,
            int event, const char *zInfo, size_t nInfo)
{
    printf("DEBUG [%d]: %.*s\n", event, (int)nInfo, zInfo);
    return TH8_OK;  /* return TH8_ERROR to abort */
}

Th8_SetDebugCallback(interp, myDebugHook, NULL);
Th8_SetStepMode(interp, 1);  /* enable single-stepping */
```

### Breakpoints

```c
Th8_SetBreakpoint(interp, "myproc", TH8_NOLEN);
/* The debug callback fires when myproc is entered. */
```

---

## 11  Coroutines

TH8 supports stackless coroutines:

```c
/* From a script: */
const char *script =
    "coroutine gen apply {{} {\n"
    "    yield 1\n"
    "    yield 2\n"
    "    yield 3\n"
    "}}\n"
    "list [gen] [gen] [gen]";

Th8_Eval(interp, 0, script, TH8_NOLEN, NULL, 0);
/* Result: "1 2 3" */
```

---

## 12  Thread Safety

- Each `Th8_Interp` is single-threaded.  Do not share an
  interpreter across threads.
- `Th8_Initialize` / `Th8_Finalize` are process-global and
  must be called from the main thread.
- `Th8_CancelEval` is the only function safe to call from
  another thread (it sets an atomic flag).
- Create separate interpreters for concurrent threads.

---

## 13  Memory Management

### Overflow-safe allocation

Use `Th8_SafeAlloc*` for all size arithmetic:

```c
/* Simple allocation: */
char *p = (char *)Th8_SafeAlloc(interp, nBytes, __FILE__, __LINE__);

/* String (adds 1 for NUL, checks overflow): */
char *s = (char *)Th8_SafeAllocStr(interp, nLen, __FILE__, __LINE__);

/* Array (checks nElem * sizeof overflow): */
int *a = (int *)Th8_SafeAllocMul(interp, nElem, sizeof(int),
        __FILE__, __LINE__);
```

Or use the convenience macros from `th8_int.h`:

```c
char *p = (char *)TH8_ALLOC(interp, nBytes);
char *s = (char *)TH8_ALLOC_STR(interp, nLen);
int  *a = (int *)TH8_ALLOC_MUL(interp, nElem, sizeof(int));
```

All allocation goes through the platform's `xMalloc` and is
tracked against the interpreter's memory limit.

### Freeing

```c
Th8_Free(interp, p);
```

---

## 14  Error Handling Best Practices

Always check return codes:

```c
int rc = Th8_Eval(interp, 0, zScript, nScript, NULL, 0);
if (rc != TH8_OK) {
    const char *zErr = Th8_GetResult(interp, NULL);
    int line = Th8_GetErrorLine(interp);
    fprintf(stderr, "error at line %d: %s\n", line, zErr);
}
```

Return codes:
- `TH8_OK` (0) --- success
- `TH8_ERROR` (1) --- error (result contains message)
- `TH8_RETURN` (2) --- `[return]` from procedure
- `TH8_BREAK` (3) --- `[break]` from loop
- `TH8_CONTINUE` (4) --- `[continue]` from loop

---

## 15  Complete Embedding Checklist

1. Include `th8.h`
2. Create platform: `Th8_ClonePlatform(NULL)` + overrides
3. Initialize: `Th8_Initialize(pPlat)`
4. Create interpreter: `Th8_CreateInterp(pPlat)`
5. Register commands: `Th8_RegisterLanguage(interp)`
6. Set resource limits: `Th8_SetStepLimit`, `Th8_SetAllocLimit`
7. Register custom commands: `Th8_CreateCommand`
8. Set initial variables: `Th8_SetVar`
9. Evaluate scripts: `Th8_Eval` / `Th8_EvalFile`
10. Read results: `Th8_GetResult`, `Th8_ToInt`, etc.
11. Destroy: `Th8_DeleteInterp(interp)`
12. Finalize: `Th8_Finalize(pPlat)`, `Th8_FreePlatform(pPlat)`

---

## 16  Further Reading

- `tcl_language_standard_v1.md` --- formal language standard
- `th8_public_c_api_specification.md` --- complete C API reference
- `quickstart_scripting.md` --- scripting guide for end users
- `src/th8sh.c` --- the shell is itself an embedder and serves as
  a reference implementation
