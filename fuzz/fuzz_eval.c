/* fuzz_eval.c -- Fuzz the TH8 script evaluator. */
#include "fuzz_common.h"
static Th8_Interp *g;
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; g=th8FuzzCreateInterp(1); return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    if (!g||!n||n>100000) return 0;
    Th8_SetStepLimit(g,50000);
    Th8_Eval(g,0,(const char*)d,n,NULL,0);
    Th8_SetResult(g,"",0); Th8_ResetCancel(g); Th8_SetStepLimit(g,0);
    return 0;
}
#include "fuzz_main.h"
