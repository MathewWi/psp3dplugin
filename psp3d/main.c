/*------------------------------------------------------------------------------*/
/* PSP3D Plugin 																*/
/*------------------------------------------------------------------------------*/
#include <pspkernel.h>
#include <pspsdk.h>
#include <pspctrl.h>
//#include <pspctrl_kernel.h>
//#include <psppower.h>
//#include <pspdisplay.h>
#include <pspdisplay_kernel.h>
//#include <psputilsforkernel.h>
//#include <pspsysmem_kernel.h>
//#include <pspdebug.h>
#include <pspgu.h>
//#include <pspsdk.h>
//#include <pspkernel.h>
#include <systemctrl.h>
//#include <string.h>
//#include <stdio.h>
//#include <malloc.h>
#include "hook.h"
#include "debug.h"
#include "gameinfo.h"
#include "config.h"
#include "render3d.h"
/*------------------------------------------------------------------------------*/
/* module info																	*/
/*------------------------------------------------------------------------------*/
PSP_MODULE_INFO( "3dPlugin", 0x1000, 1, 1 );//PSP_MODULE_USER, 1, 1);//PSP_MODULE_KERNEL, 1, 1 );

char running = 1;
SceUID MainThreadID = -1;
extern configData currentConfig;
char draw3D = 0;

//PSP_NO_CREATE_MAIN_THREAD();
/*
STMOD_HANDLER previous = NULL;

void *framebuf = 0;
char draw3D = 0;

#define MAX_THREAD	64

static int MainThread( SceSize args, void *argp )
{
	unsigned int paddata_old = 0;
	char hooked = 0;
	char dispHooked = 0;
	char noteHandled = 0;
	char txt[100];
	SceCtrlData paddata;
#ifdef DEBUG_MODE
	debuglog("Plugin started\r\n");
#endif

	while (sceKernelFindModuleByName("sceDisplay_Service") == NULL)
	{
	    sceKernelDelayThread(20);
	}

#ifdef DEBUG_MODE
	debuglog("Display module loaded\r\n");
#endif

	while (sceKernelFindModuleByName("sceGE_Manager") == NULL)
	{
	    sceKernelDelayThread(20);
	}

#ifdef DEBUG_MODE
	debuglog("GE Manager loaded\r\n");
#endif

	readConfigFile("space");//gametitle);

	sceKernelDelayThread(100);
	pspDebugScreenInit();

	if (currentConfig.lateHook == 0){
		hookFunctions();
		hooked = 1;
	}

#ifdef DEBUG_MODE
	debuglog("Start main loop\r\n");
	//set a default activation button while in debug/trace mode
	currentConfig.activationBtn = 0x800000; //note key//0x400000; // screen key
#endif
	
	while(running)
	{
		sceCtrlPeekBufferPositive(&paddata, 1);
		
		if(paddata.Buttons != paddata_old)
		{
			//
			// special keys to change behavior of plugin
			//
			if (draw3D == 3){
				noteHandled = 0;
				//sprintf(txt, "Buttons : %X\r\n", paddata.Buttons);
				//debuglog(txt);
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)){
					currentConfig.showStat = 1;
				} else {
					currentConfig.showStat = 0;
				}

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_LEFT))
					currentConfig.rotationAngle -= 0.25f*GU_PI/180.0f;
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_RIGHT))
					currentConfig.rotationAngle += 0.25f*GU_PI/180.0f;

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_UP))
					currentConfig.rotationDistance -= 1.0f;
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_DOWN))
					currentConfig.rotationDistance += 1.0f;

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | currentConfig.activationBtn)){
					currentConfig.colorMode++;
					if (currentConfig.colFlip == 0){
						if (currentConfig.colorMode >= MAX_COL_MODE)
							currentConfig.colorMode = 0;
					} else {
						if (currentConfig.colorMode >= MAX_COL_MODE*2)
							currentConfig.colorMode = MAX_COL_MODE;
					}
					noteHandled = 1;
				}

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE)){
					if (currentConfig.colFlip == 1){
						currentConfig.colFlip = 0;
						currentConfig.colorMode -= MAX_COL_MODE;
					}else{
						currentConfig.colFlip = 1;
						currentConfig.colorMode += MAX_COL_MODE;
					}
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
					hooked = 1;
					dispHooked = 1;
//					debuglog("config ready\r\n");
					//sceKernelDelayThread(10000);
				}
				if (dispHooked == 0){
	//				hookDisplayOnly();
					dispHooked = 1;
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
*/

/*------------------------------------------------------------------------------*/
/* main thread
/*------------------------------------------------------------------------------*/
int MainThread( SceSize args, void *argp ){
	char hooked = 0;
	char noteHandled = 0;
	SceCtrlData paddata;
	unsigned int lastButtons = 0;

#ifdef DEBUG_MODE
	debuglog("Plugin started\r\n");
#endif
	readConfigFile("space");

	if (currentConfig.lateHook == 0){
		hookFunctions();
		hooked = 1;
	}

#ifdef DEBUG_MODE
	debuglog("Start main loop\r\n");
	//set a default activation button while in debug/trace mode
	currentConfig.activationBtn = 0x800000; //note key//0x400000; // screen key
#endif
	while (running){
		sceCtrlPeekBufferPositive(&paddata, 1);

		if(paddata.Buttons != lastButtons)
		{
			//
			// special keys to change behavior of plugin
			//
			if (draw3D == 3){
				noteHandled = 0;
				//sprintf(txt, "Buttons : %X\r\n", paddata.Buttons);
				//debuglog(txt);
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)){
					currentConfig.showStat = 1;
				} else {
					currentConfig.showStat = 0;
				}

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_LEFT))
					currentConfig.rotationAngle -= 0.25f*GU_PI/180.0f;
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_RIGHT))
					currentConfig.rotationAngle += 0.25f*GU_PI/180.0f;

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_UP))
					currentConfig.rotationDistance -= 1.0f;
				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_DOWN))
					currentConfig.rotationDistance += 1.0f;

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | currentConfig.activationBtn)){
					currentConfig.colorMode++;
					if (currentConfig.colFlip == 0){
						if (currentConfig.colorMode >= MAX_COL_MODE)
							currentConfig.colorMode = 0;
					} else {
						if (currentConfig.colorMode >= MAX_COL_MODE*2)
							currentConfig.colorMode = MAX_COL_MODE;
					}
					noteHandled = 1;
				}

				if ((paddata.Buttons & 0xffffff) == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE)){
					if (currentConfig.colFlip == 1){
						currentConfig.colFlip = 0;
						currentConfig.colorMode -= MAX_COL_MODE;
					}else{
						currentConfig.colFlip = 1;
						currentConfig.colorMode += MAX_COL_MODE;
					}
				}
			}
			//press "note" button and magick begin
			if(paddata.Buttons & currentConfig.activationBtn && noteHandled == 0)
			{
#ifdef DEBUG_MODE
				debuglog("ActivationButton pressed\n");
#endif
				//activate 3D rendering...
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
			lastButtons = paddata.Buttons;
		}
		sceKernelDelayThread(10000);
	}
	return 0;
}
/*------------------------------------------------------------------------------*/
/* module_start																	*/
/*------------------------------------------------------------------------------*/
int module_start( SceSize args, void *argp )
{
#ifdef DEBUG_MODE
	debuglog("3d-plugin module_start\r\n");
#endif
	//get the current game info
	int gi_result = getGameInfo();
	if (gi_result < 0){
		debuglog("Error getting game info\r\n");
	}
/*#ifdef DEBUG_MODE
	char text[200];
	sprintf(text, "Game ID:%s\r\n", gameid);
	debuglog(text);
	sprintf(text, "Game Title:%.100s\r\n", gametitle);
	debuglog(text);
#endif
*/

	MainThreadID = sceKernelCreateThread( "PSP3DPlugin", MainThread, 25, 0x10000, 0, NULL );
	if ( MainThreadID >= 0 )
	{
		running = 1;
		sceKernelStartThread( MainThreadID, args, argp );
	}

	return 0;
}

/*------------------------------------------------------------------------------*/
/* module_stop																	*/
/*------------------------------------------------------------------------------*/
int module_stop( SceSize args, void *argp )
{
	running = 0;
	//if ( MainThreadID    >= 0 ){ sceKernelDeleteThread( MainThreadID ); }

	return 0;
}
