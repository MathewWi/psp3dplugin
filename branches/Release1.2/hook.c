/*------------------------------------------------------------------------------*/
/* hook																			*/
/*------------------------------------------------------------------------------*/
#include <pspkernel.h>
#include <psputilsforkernel.h>
#include <string.h>
#include <stdio.h>
//#include "interruptman.h"
#include "hook.h"
#include "debug.h"

// MIPS Opcodes
#define MIPS_NOP 0x00000000
#define MIPS_SYSCALL(NUM) (((NUM)<<6)|12)
#define MIPS_J(ADDR) (0x08000000 + ((((unsigned int)(ADDR))&0x0ffffffc)>>2))
#define MIPS_STACK_SIZE(SIZE) (0x27BD0000 + (((unsigned int)(SIZE)) & 0x0000FFFF))
#define MIPS_PUSH_RA(STACKPOS) (0xAFBF0000 + (((unsigned int)(STACKPOS)) & 0x0000FFFF))
#define MIPS_POP_RA(STACKPOS) (0x8FBF0000 + (((unsigned int)(STACKPOS)) & 0x0000FFFF))
#define MIPS_RETURN 0x03E00008

//#include "hook.h"

/*------------------------------------------------------------------------------*/
/* HookNidAddress																*/
/*------------------------------------------------------------------------------*/
void *HookNidAddress( SceModule *mod, char *libname, u32 nid )
{
	int i;

	u32 *ent_next = (u32 *)mod->ent_top;
	u32 *ent_end  = (u32 *)mod->ent_top + (mod->ent_size >> 2);

	while ( ent_next < ent_end )
	{
		SceLibraryEntryTable *ent = (SceLibraryEntryTable *)ent_next;
		if ( ent->libname != NULL )
		{
			if ( strcmp( ent->libname, libname ) == 0 ){
				int count = ent->stubcount + ent->vstubcount;
				u32 *nidt = ent->entrytable;
				for ( i=0; i<count; i++ )
				{
					if ( nidt[i] == nid )
					{ 
						return( (void *)nidt[count+i] );
					}
				}
			}
		}
		ent_next += ent->len;
	}
	return( NULL );
}

/*------------------------------------------------------------------------------*/
/* HookSyscallAddress															*/
/*------------------------------------------------------------------------------*/
void *HookSyscallAddress( void *addr )
{
	u32		**ptr;
	if ( addr == NULL ){ return( NULL ); }
	asm( "cfc0 %0, $12\n" : "=r"(ptr) );
	if ( ptr == NULL ){ return( NULL ); }

	int		i;
	u32		*tbl = ptr[0];
	int		size = (tbl[3]-0x10)/sizeof(u32);
	for ( i=0; i<size; i++ )
	{
		if ( tbl[4+i] == (u32)addr )
		{ 
			return( &tbl[4+i] );
		}
	}
	return( NULL );
}

/*------------------------------------------------------------------------------*/
/* HookFuncSetting																*/
/*------------------------------------------------------------------------------*/
void HookFuncSetting( void *addr, void *entry )
{
	if ( addr == NULL ){ return; }

	((u32 *)addr)[0] = (u32)entry;
	sceKernelDcacheWritebackInvalidateRange( addr, sizeof( addr ) );
	sceKernelIcacheInvalidateRange( addr, sizeof( addr ) );
}
/*
void sctrlHENPatchSyscall(u32 addr, void *newaddr)
{


	struct SyscallHeader *head;
	u32 *syscalls;
	u32 **ptr;
	u32	*tbl = ptr[0];
	int size;
	int i;

	// get syscall struct from cop0
	asm("cfc0 %0, $12\n" : "=r"(ptr));

	if (ptr == NULL) {
		return;
	}

	head = (struct SyscallHeader *) *ptr;
	syscalls = (u32*) (*ptr + 0x10);
	size = (tbl[3] - 0x10) / sizeof(u32);

	for(i=0; i<size; i++) {
		if(syscalls[i] == addr) {
			syscalls[i] = (u32)newaddr;
		}
	}
}
*/
int hookJump(const char * module, const char * library, unsigned int nid, void * function, unsigned int * orig_loader)
{
  // Hooking Result
  int result = 0;

  // Check Arguments
  if(module && library && function)
  {
    // Find Function
    unsigned int * sfunc = (unsigned int*)sctrlHENFindFunction(module, library, nid);

    // Found Function
    if(sfunc)
    {
      // Backup Interrupts
      int interrupt = pspSdkDisableInterrupts();

      // Create Original Loader
      if(orig_loader)
      {
        // Backup Original Instructions
        orig_loader[0] = sfunc[0];
        orig_loader[1] = sfunc[1];
        orig_loader[2] = sfunc[2];
        orig_loader[3] = sfunc[3];
        orig_loader[4] = sfunc[4];
        orig_loader[5] = sfunc[5];
        orig_loader[6] = sfunc[6];
        orig_loader[7] = sfunc[7];

        // Jump Delay Slot (Just to be on the safe side...)
        orig_loader[8] = MIPS_NOP;

        // Jump to Original Code
        orig_loader[9] = MIPS_J(&sfunc[8]);

        // Jump Delay Slot
        orig_loader[10] = MIPS_NOP;
      }

      // Patch Function with Jump
      sfunc[0] = MIPS_STACK_SIZE(-4); // Allocate 4 Bytes on Stack
      sfunc[1] = MIPS_PUSH_RA(0); // Backup RA on Stack
      sfunc[2] = MIPS_SYSCALL(sceKernelQuerySystemCall(function)); // Syscall to our Hook
      sfunc[3] = MIPS_NOP; // Delay Slot
      sfunc[4] = MIPS_POP_RA(0); // Get RA from Stack
      sfunc[5] = MIPS_STACK_SIZE(4); // Free 4 Bytes on Stack
      sfunc[6] = MIPS_RETURN; // Return
      sfunc[7] = MIPS_NOP; // Delay Slot

      // Force Memory Mirroring
      sceKernelDcacheWritebackInvalidateRange(sfunc, sizeof(unsigned int) * 8);
      sceKernelIcacheInvalidateRange(sfunc, sizeof(unsigned int) * 8);

      // Enable Interrupts
      pspSdkEnableInterrupts(interrupt);

      // Hooking Debug Log
      char log[128];
      sprintf(log, "hookJump: Set Jump Hook on %08X to %08X (Module: %s, Library: %s, NID: %08X).\n", (unsigned int)sfunc, (unsigned int)function, module, library, nid);
      debuglog(log);
    }

    // Failed Finding Function
    else
    {
      // Result
      result = -5;

      // Log Error
      debuglog("hookJump: Couldn't find Function. NID might be incorrect.\n");
    }
  }

  // Invalid Arguments
  else
  {
    // Result
    result = -4;

    // Log Error
    debuglog("hookJump: Invalid Arguments.\n");
  }

  // Return Hooking Result
  return result;
}

int hookSyscall(const char * module, const char * library, unsigned int nid, void * function, unsigned int * orig_loader)
{
  // Hooking Result
  int result = 0;

  // Check Arguments
  if(module && library && function)
  {
    // Find Function
    unsigned int * sfunc = (unsigned int*)sctrlHENFindFunction(module, library, nid);

    // Found Function
    if(sfunc)
    {
      // Create Original Loader
      if(orig_loader)
      {
        // Jump to Original Code
        orig_loader[0] = MIPS_J(sfunc);

        // Jump Delay Slot
        orig_loader[1] = MIPS_NOP;
      }

      // Patch Syscall
      sctrlHENPatchSyscall((unsigned int)sfunc, function);

      // Hooking Debug Log
      char log[128];
      sprintf(log, "hookSyscall: Set Syscall Hook on %08X to %08X (Module: %s, Library: %s, NID: %08X).\n", (unsigned int)sfunc, (unsigned int)function, module, library, nid);
      debuglog(log);
    }

    // Failed Finding Function
    else
    {
      // Result
      result = -5;

      // Log Error
      debuglog("hookSyscall: Couldn't find Target Function. NID might be incorrect.\n");
    }
  }

  // Invalid Arguments
  else
  {
    // Result
    result = -4;

    // Log Error
    debuglog("hookSyscall: Invalid Arguments.\n");
  }

  // Return Hooking Result
  return result;
}

int hookAPI(const char * module, const char * library, unsigned int nid, void * function, int mode, unsigned int * orig_loader)
{
  // Hooking Result
  int result = 0;

  // Avoid Crash
  sceKernelDelayThread(10000);

  // Check Arguments
  if(module && library && function)
  {
    // Find Module in Memory
    SceModule * findmodule = (SceModule*)sceKernelFindModuleByName(module);

    // Found Module
    if(findmodule)
    {
      // Hook via Syscall
      if(mode == HOOK_SYSCALL) result = hookSyscall(module, library, nid, function, orig_loader);

      // Hook via Jump
      else if(mode == HOOK_JUMP) result = hookJump(module, library, nid, function, orig_loader);

      // Invalid Hook Mode
      else
      {
        // Result
        result = -3;

        // Log Error
        debuglog("hookAPI: Invalid Hook Mode.\n");
      }
    }

    // Couldn't Find Module
    else
    {
      // Result
      result = -2;

      // Log Error
      debuglog("hookAPI: Couldn't find Module. Might not be loaded yet.\n");
    }
  }

  // Invalid Arguments
  else
  {
    // Result
    result = -1;

    // Log Error
    debuglog("hookAPI: Invalid Arguments.\n");
  }

  // Avoid Crash
  sceKernelDelayThread(10000);

  // Return Hooking Result
  return result;
}
