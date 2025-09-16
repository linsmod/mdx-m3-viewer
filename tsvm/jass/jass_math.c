
#include "shared.h"
#include "vm_public.h"
#include "vm_priv.h"
#include "vm_ext.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include "math.h"
DWORD __Math_constructor(LPJASS j) {
    return jass_pushnullhandle(j, "Math");
}

DWORD __Math_random(LPJASS j) {
    FLOAT r = (double)rand() / (double)RAND_MAX;
    return jass_pushnumber(j, r);
}
//Math.hypot
DWORD __Math_hypot(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT result = hypot(x, y);
    return jass_pushnumber(j, result);
}

static JASSFUNC libfuncs[] ={
    VMFUNC2("Math",__Math_constructor, jasstype_handle),
    VMFUNC2("Math.random",__Math_random, jasstype_handle),
    VMFUNC2("Math.hypot",__Math_hypot, jasstype_handle),
};

void jass_register_Math(LPJASS j){
    FOR_EACH(JASSFUNC,func, libfuncs,sizeof(libfuncs)/sizeof(JASSFUNC)){
        LPJASSFUNC copy = ALLOCZ(copy, JASSFUNC);
        memcpy(copy, func, sizeof(JASSFUNC));
        PUSH_BACK(JASSFUNC,copy, j->functions);
        // if (j->functions) {
        //     JASSFUNC *lastJASSFUNC = j->functions;
        //     while (lastJASSFUNC->next) {
        //     lastJASSFUNC = lastJASSFUNC->next;
        //     }
        //     lastJASSFUNC->next = copy;
        //     if (lastJASSFUNC->next->next == lastJASSFUNC) {
        //         fprintf(stderr, "VAR copy required on `%s` at %s:%d\n", "JASSFUNC",
        //                 "/home/wulin/mdx-m3-viewer/tsvm/jass/jass_math.c", 23);
        //         exit(1);
        //     };
        // } else {
        //     j->functions = copy;
        // }
    }
}