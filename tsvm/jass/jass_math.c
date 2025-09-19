
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

static NATIVE libmathfuncs[] ={
    {"Math",__Math_constructor},
    {"Math.random",__Math_random},
    {"Math.hypot",__Math_hypot},
    {0},
};

void jass_register_Math(LPHASHTABLE table){
    jass_register_natives(libmathfuncs, table);
}