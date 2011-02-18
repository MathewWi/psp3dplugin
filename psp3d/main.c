/*------------------------------------------------------------------------------*/
/* RemoteJoyLite																*/
/*------------------------------------------------------------------------------*/
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspctrl.h>
#include <pspctrl_kernel.h>
#include <psppower.h>
#include <pspdisplay.h>
#include <pspdisplay_kernel.h>
#include <psputilsforkernel.h>
#include <pspsysmem_kernel.h>
#include <pspdebug.h>
#include <pspgu.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "hook.h"
#include "debug.h"
#include "gameinfo.h"
#include "config.h"
#include "render3d.h"
/*------------------------------------------------------------------------------*/
/* module info																	*/
/*------------------------------------------------------------------------------*/
PSP_MODULE_INFO( "psp3d", PSP_MODULE_KERNEL, 1, 1 );//PSP_MODULE_USER, 1, 1);//PSP_MODULE_KERNEL, 1, 1 );

extern configData currentConfig;
static SceUID MainThreadID = -1;
void *framebuf = 0;
char draw3D = 0;

#define MAX_THREAD	64
char running = 1;

static int MainThread( SceSize args, void *argp )
{
	unsigned int paddata_old = 0;
	short hooked = 0;
	char noteHandled = 0;
//	char txt[100];
	SceCtrlData paddata;
#ifdef DEBUG_MODE
	debuglog("Plugin started\r\n");
#endif

//#ifdef DEBUG_MODE
	readConfigFile(gametitle);
//#endif
	sceKernelDelayThread(10000);

	if (currentConfig.lateHook == 0){
		hookFunctions();
		hooked = 1;
	}

#ifdef DEBUG_MODE
	debuglog("Start main loop\r\n");
	//set a default activation button while in debug/trace mode
	currentConfig.activationBtn = 0x400000; // screen key
#endif
	
	while(running)
	{
		sceCtrlPeekBufferPositive(&paddata, 1);
		
		if(paddata.Buttons != paddata_old)
		{
			/*
			 * special keys to change behavior of plugin
			 */
			if (draw3D == 3){
				noteHandled = 0;
				//sprintf(txt, "Buttons : %X\r\n", paddata.Buttons);
				//debuglog(txt);
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_LEFT))
					currentConfig.rotationAngle -= 0.5f*GU_PI/180.0f;
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_RIGHT))
					currentConfig.rotationAngle += 0.5f*GU_PI/180.0f;

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_UP))
					currentConfig.rotationDistance -= 2.0f;
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_DOWN))
					currentConfig.rotationDistance += 2.0f;

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | currentConfig.activationBtn)){
					if (currentConfig.color1 == 0x0000ff){
						currentConfig.color1 = 0xff00ff;
						currentConfig.color2 = 0x00ff00;
					}else if (currentConfig.color1 == 0xff00ff){
						currentConfig.color1 = 0xff0000;
						currentConfig.color2 = 0x00ffff;
					} else if (currentConfig.color1 == 0xff0000){
						currentConfig.color1 = 0x0000ff;
						currentConfig.color2 = 0xffff00;
					}
					noteHandled = 1;
				}
			}
			//press "note" button and magick begin
			if(paddata.Buttons & currentConfig.activationBtn && noteHandled == 0)
			{
#ifdef DEBUG_MODE
				debuglog("ActivationButton pressed\n");
#endif
//#ifdef DEBUG_MODE

				if (hooked == 0){
					hookFunctions();
/*
					debuglog("get config\r\n");
					readConfigFile(gametitle);
					*/
					hooked = 1;
//					debuglog("config ready\r\n");
					//sceKernelDelayThread(10000);
				}
//#endif

				//can parse command list
				if (draw3D == 0){
#ifdef DEBUG_MODE
					debuglog("initiate render3D\r\n");
#endif
					draw3D = 1;
				}
				else{
#ifdef DEBUG_MODE
					debuglog("stop render3D\r\n");
#endif
					draw3D = 9;
				}
			}
		}
		paddata_old = paddata.Buttons;
		sceKernelDelayThread(10000);
	}

	//after leaving, unhook...
	//unhookFunctions();

	return( 0 );
}

/*------------------------------------------------------------------------------*/
/* module_start																	*/
/*------------------------------------------------------------------------------*/
#define GET_JUMP_TARGET(x)		(0x80000000|(((x)&0x03FFFFFF)<<2))

int module_start( SceSize args, void *argp )
{
	//get the current game info
	int gi_result = getGameInfo();
	if (gi_result < 0){
		debuglog("Error getting game info\r\n");
	}
#ifdef DEBUG_MODE
	char text[200];
	sprintf(text, "Game ID:%s\r\n", gameid);
	debuglog(text);
	sprintf(text, "Game Title:%.100s\r\n", gametitle);
	debuglog(text);
#endif

	MainThreadID = sceKernelCreateThread( "PSP3DPlugin", MainThread, 16, 0x800, 0, NULL );
	if ( MainThreadID >= 0 )
	{
		sceKernelStartThread( MainThreadID, args, argp );
	}
	
	return( 0 );
}

/*------------------------------------------------------------------------------*/
/* module_stop																	*/
/*------------------------------------------------------------------------------*/
int module_stop( SceSize args, void *argp )
{
	running = 0;
	sceKernelDelayThread(100000);
	if ( MainThreadID    >= 0 ){ sceKernelDeleteThread( MainThreadID ); }

	return( 0 );
}
