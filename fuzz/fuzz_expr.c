/* fuzz_expr.c -- Fuzz the TH8 expression parser. */
#include "fuzz_common.h"
static Th8_Interp *g;
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; g=th8FuzzCreateInterp(1); return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (!g||!n||n>10000) return 0;
    Th8_SetStepLimit(g,10000);
    Th8_Expr(g,(const char*)d,n,NULL,0);
    Th8_SetResult(g,"",0); Th8_ResetCancel(g); Th8_SetStepLimit(g,0);
    return 0;
}
#include "fuzz_main.h"
