#include "jass/vm_public.h"
#include "jass/vm_ext.h"
#include "stdlib.h"
int main(int argc, char **argv) {
    
    vmext_init(".");
    LPJASS s = jass_newstate(NULL);
    jass_dofile(s, "./src/index.ts");
}