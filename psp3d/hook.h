#ifndef _HOOK_H_
#define _HOOK_H_
/*------------------------------------------------------------------------------*/
/* hook																			*/
/*------------------------------------------------------------------------------*/
#include "hook_display.h"


/*------------------------------------------------------------------------------*/
/* prototype																	*/
/*------------------------------------------------------------------------------*/
extern void *HookNidAddress( SceModule *mod, char *libname, u32 nid );
extern void *HookSyscallAddress( void *addr );
extern void HookFuncSetting( void *addr, void *entry );

void *ApiHookByNid(const char *modName, const char *libname, u32 nid, void* customFunc);

/* test new hooking code...*/
// Hook Modes
#define HOOK_SYSCALL 0
#define HOOK_JUMP 1

// Hook Function - returns 0 on success, < 0 on error.
int hookAPI(const char * module, const char * library, unsigned int nid, void * function, int mode, unsigned int * orig_loader);


#endif	// _HOOK_H_
