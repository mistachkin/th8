/* fuzz_snk.c -- Fuzz the SNK/CAPI key parser. */
#include "fuzz_common.h"
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
static Th8_Interp *g;
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; g=th8FuzzCreateInterp(0); return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    Th8_RsaKey *k=0;
    if (!g||!n||n>50000) return 0;
    if (Th8_RsaKeyLoad(g,d,n,&k)==TH8_OK) Th8_RsaKeyFree(g,k);
    Th8_SetResult(g,"",0);
    return 0;
}
#else
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) { (void)d;(void)n; return 0; }
#endif
#include "fuzz_main.h"
