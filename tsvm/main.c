#include "jass/vm_public.h"
int main(int argc, char **argv) { 
    LPJASS vm = jass_newstate();
    jass_dofile(vm, "src/index.ts");
}