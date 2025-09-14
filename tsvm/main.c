#include "jass/vm_public.h"
#include "jass/vm_ext.h"

int main(int argc, char **argv) { 
    vmext_init(".");
    LPJASS vm = jass_newstate();
    // jass_dofile(vm, "./src/Array.jast");
    jass_dofile(vm, "./src/index.ts");
}