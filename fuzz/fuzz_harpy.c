/* fuzz_harpy.c -- Fuzz the Harpy signature parser. */
#include "fuzz_common.h"
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
static Th8_Interp *g;
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; g=th8FuzzCreateInterp(0); return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    unsigned char *ps=0; size_t ns=0; char *zt=0;
    if (!g||!n||n>100000) return 0;
    if (Th8_HarpySigLoad(g,(const char*)d,n,&ps,&ns,&zt)==TH8_OK) {
        Th8_Free(g,ps); Th8_Free(g,zt);
    }
    Th8_SetResult(g,"",0);
    return 0;
}
#else
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) { (void)d;(void)n; return 0; }
#endif
#include "fuzz_main.h"
