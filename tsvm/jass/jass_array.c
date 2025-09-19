
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

static NATIVE arrayfuncs[] ={
    {"Array",__Array_constructor},
    {"Float32Array",__Float32Array_constructor},
    {0}
};

void jass_register_Array(LPHASHTABLE table){
    jass_register_natives(arrayfuncs,table);
}