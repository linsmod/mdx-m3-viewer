
#include "shared.h"
#include "vm_public.h"
#include "vm_priv.h"
#include "vm_ext.h"
#include <memory.h>
#include <string.h>

DWORD __Array_constructor(LPJASS j) {
    LPJASSARRAY array= ALLOCZ(array, JASSARRAY);
    return jass_pushhandle(j,array,"Array");
}
DWORD __Float32Array_constructor(LPJASS j) {
    LPJASSARRAY array= ALLOCZ(array, JASSARRAY);
    return jass_pushhandle(j,array,"Float32Array");
}

static JASSFUNC arrayfuncs[] ={
    VMFUNC2("Array",__Array_constructor, jasstype_handle),
    VMFUNC2("Float32Array",__Float32Array_constructor, jasstype_handle),
};

void jass_register_Array(LPJASS j){
    FOR_EACH(JASSFUNC,func, arrayfuncs,sizeof(arrayfuncs)/sizeof(JASSFUNC)){
        LPJASSFUNC copy = ALLOCZ(copy, JASSFUNC);
        memcpy(copy, func, sizeof(JASSFUNC));
        PUSH_BACK(JASSFUNC,copy, j->functions);
    }
}