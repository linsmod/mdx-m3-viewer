#pragma  once
#include "../shared.h"
#include "vm_public.h"
// External methods that application must provides
extern void vmext_skipbom(LPSTR);
extern LPSTR vmext_readalltext(LPCSTR fileName);
extern DWORD vmext_createthread(HANDLE (func)(HANDLE), HANDLE args);
extern void vmext_free(HANDLE ptr);
extern void vmfreeobj(LPJASSOBJECT ptr);
extern HANDLE vmext_alloc(DWORD size);
extern LPSTR vmext_resolvepath(LPCSTR path, LPCSTR relativeTo);
extern void vmext_init(const char* appbase_path);