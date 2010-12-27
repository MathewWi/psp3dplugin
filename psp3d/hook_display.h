#ifndef _HOOK_DISPLAY_H_
#define _HOOK_DISPLAY_H_
/*------------------------------------------------------------------------------*/
/* hook_display																	*/
/*------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------*/
/* prototype																	*/
/*------------------------------------------------------------------------------*/
extern void hookDisplay( void );
extern void unhook_display(void);
extern int can_parse;
extern void* framebuf;
extern char draw3D;

#endif	// _HOOK_DISPLAY_H_
