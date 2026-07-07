/* fuzz_list.c -- Fuzz the TH8 list parser. */
#include "fuzz_common.h"
static Th8_Interp *g;
int LLVMFuzzerInitialize(int *a, char ***v) { (void)a;(void)v; g=th8FuzzCreateInterp(1); return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n) {
    char **az=0; size_t *an=0; int nc=0;
    if (!g||!n||n>50000) return 0;
    if (Th8_SplitList(g,(const char*)d,n,&az,&an,&nc,TH8_LIST_NO_CACHE)==TH8_OK && az) Th8_Free(g,az);
    Th8_SetResultStatic(g,"",0);
    return 0;
}
#include "fuzz_main.h"
