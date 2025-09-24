
#include "shared.h"
#include "vm_public.h"
#include "vm_priv.h"
#include "vm_ext.h"
#include "zhash.h"
#include <memory.h>
#include <string.h>


DWORD __Object_constructor(LPJASS j) {
    LPJASSOBJECT obj= ALLOCZ(obj, JASSOBJECT);
    obj->ht= zcreate_hash_table();
    return jass_pushhandle(j,obj,"Object");
}

DWORD __Array_constructor(LPJASS j) {
    LPJASSARRAY array= ALLOCZ(array, JASSARRAY);
    return jass_pushhandle(j,array,"Array");
}
DWORD __Float32Array_constructor(LPJASS j) {
    LPJASSARRAY array= ALLOCZ(array, JASSARRAY);
    return jass_pushhandle(j,array,"Float32Array");
}

static NATIVE arrayfuncs[] ={
    {"Object.constructor",__Object_constructor},
    {"Array.constructor",__Array_constructor},
    {"Float32Array.constructor",__Float32Array_constructor},
    {0}
};

void jass_register_Array(LPHASHTABLE fntable,LPHASHTABLE types){
    jass_register_type("Object","Object.constructor", types);
    jass_register_type("Array","Array.constructor", types);
    jass_register_type("Float32Array","Float32Array.constructor", types);
    jass_register_natives(arrayfuncs,fntable);
}