/* fuzz_format.c -- Fuzz the TH8 [format] command. */
#include "fuzz_common.h"
static Th8_Interp *g;
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; g=th8FuzzCreateInterp(1); return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    char *z=0; size_t nz=0;
    if (!g||!n||n>10000) return 0;
    Th8_StringAppend(g,&z,&nz,"format ",7);
    Th8_ListAppend(g,&z,&nz,(const char*)d,n);
    Th8_StringAppend(g,&z,&nz," 42 3.14 hello 255 65",TH8_NOLEN);
    Th8_SetStepLimit(g,10000);
    Th8_Eval(g,0,z,nz,NULL,0);
    Th8_Free(g,z);
    Th8_SetResult(g,"",0); Th8_ResetCancel(g); Th8_SetStepLimit(g,0);
    return 0;
}
#include "fuzz_main.h"
