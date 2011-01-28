/*
 * render3d.c
 *
  *
 * Copyright (C) 2010 André Borrmann
 *
 * This program is free software;
 * you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation;
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program;
 * if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <pspkernel.h>
#include "debug.h"
#include "hook.h"
#include <pspgu.h>
#include <pspgum.h>
#include <psptypes.h>
#include <stdio.h>
#include <config.h>
//#include <pspge.h>
//#define TRACE_VIEW_MODE
#define ERROR_LOG
/*
 * the usual steps while working with GE:
 * 1. Enqueue -> Set the start pointer and the first stall address of the Command-List
 *    to the GE
 * 2. UpdateStall -> after several things are done the stall address (end) of the current
 *    command list will be set. This causes the current commands being processed by the GE
 *    up to the stall address. This address will be used as start address for the next update
 * 3. finish -> set command 11 (finish) and 12 (end) to the command list and update stall
 * 4. Synch -> whait for the GE having executed the list passed completely...
 *
 * Other usual steps would be:
 * 1. setup and fill the GE list
 * 2. Enqueue -> set the stall address to 0 which directly passes the current display list
 *    to the hardware
 * 3. Synch -> wait for this list to be processed by hardware
 */

/* Function types for the hooking stuff */
//add list on the end
int (*sceGeListEnQueue_Func)(const void *, void *, int, PspGeListArgs *) = NULL;
//add list on the head
int (*sceGeListEnQueueHead_Func)(const void *, void *, int, PspGeListArgs *) = NULL;
//remove list
int (*sceGeListDeQueue_Func)(int) = NULL;
//list synchro
int (*sceGeListSync_Func)(int, int) = NULL;
/* * Update the stall address for the specified queue. */
int (*sceGeListUpdateStallAddr_Func)(int, void *) = NULL;
/* Wait for drawing to complete.*/
int (*sceGeDrawSync_Func)(int) = NULL;
/*set framebuffer to display new data from off screen drawing */
int (*sceDisplaySetFrameBuf_Func)( void *, int, int, int ) = NULL;
/* set GE callback function */
int (*sceGeSetCallback_Func)(void *) = NULL;

int (*sceGeGetMtx_Func)(int , void *) = NULL;
int (*sceGeSaveContext_Func)(void *) = NULL;
int (*sceGeRestoreContext_Func)(void *) = NULL;

extern configData currentConfig;
extern char draw3D;

#define BUFF_SIZE 100 // size of the own GE Displaylist buffer

enum eRotationAxis{
	ERA_Y = 'Y',
    ERA_X = 'X',
    ERA_Z = 'Z'
} eRotationAxis;

/* Data declaration to support the stuff */
static int numerek = 0;

unsigned int* MYlocal_list = 0;
unsigned int* nextStart_list = 0;
unsigned int* stall_list = 0;

unsigned int current_list_addres = 0;
unsigned int stall_addres = 0;
unsigned int adress_arr[100];
unsigned int baseOffset_arr[100];

unsigned int frameBuff[2] = { 0, 0 }; //draw buffer
unsigned int frameBuffW[2] = { 0, 0 }; //draw buff height bits and buffer width

int adress_number = 0;
int can_parse = 0;

char state = 0;
char afterSync = 0;
char viewMatrixState = 0;

struct Vertex {
	u32 color;
	u16 x, y, z;
	u16 pad;
};

SceUID memid, memid2;
//void *userMemory;
void *geList3D[2];
//char list1Ready = 0;
//char list2Ready = 0;
char listPassedComplete = 0;

//this indicates whether up coming view matrices should be changed or not
//it was inside render3D first, but as this may be called by iterations of
//updateStallAddr calls the framebuffer controlling this flag will not part of this list
//in any case. The flag will be reset during call of listEnqueue
short manipulate = 0;

ScePspFMatrix4 view = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f,
		0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };

//pointer to the original values of the viewMatrix-Data within the command list
unsigned int* cmdViewMatrix[12];
unsigned int* cmdWorldMatrix[12];
unsigned int getFrameBuffCount = 0;
short frameBuffCount = 0;
short viewMatrixCount = 0;
short clearCount = 0;
/*
 * do the tracing stuff on a display list. All possible commands are listed
 * here and written to the logfile
 */
void traceGeCmd(unsigned int* geList){
	char texto[150];
	int command = 0;
	unsigned int argument = 0;
	int argif = 0;
	float fargument;

	command = (*geList) >> 24;
	argument = (*geList) & 0xffffff;

	sprintf(texto, "%X:", (unsigned int)geList);
	debuglog(texto);

	switch (command){
		case 0x08:
			sprintf(texto, "jump to %X\r\n", (unsigned int)(*geList));
			debuglog(texto);
			break;
		case 0x09:
			debuglog("conditional jump\r\n");
			//conditional jump - not implemented yet
			break;
		case 0x10:
			//base address
			sprintf(texto, "BASE %X\r\n", argument);
			debuglog(texto);
			break;
		case 0x13:
			//base offset
			sprintf(texto, "BASE-Offset\r\n");
			debuglog(texto);
			break;
		case 0x14:
			//base origin
			sprintf(texto, "BASE Origin\r\n");
			debuglog(texto);
			break;
		case 0x12:
			sprintf(texto, "VertexType %X\r\n", (unsigned int)(*geList));
			debuglog(texto);
			break;
		case 0x0a:
			sprintf(texto, "call %X\r\n", (unsigned int)(*geList));
			debuglog(texto);
			break;
		case 0x0b:
			// returning from call, retrieve last adress from stack
			sprintf(texto, "return call\r\n");
			debuglog(texto);
			break;
		case 0x40:
			sprintf(texto,"Texture Matrix strobe %X\r\n", (*geList));
			debuglog(texto);
			break;
		case 0x41:
			debuglog("Texture Matrix data\r\n");
			break;
		case 0x3a:
			sprintf(texto,"World Matrix strobe %X\r\n", (*geList));
			debuglog(texto);
			break;
		case 0x3b:
			argif = argument << 8;
			memcpy(&fargument, &argif, 4);
			sprintf(texto, "World Matrix data: %.4f\r\n", fargument);
			debuglog(texto);
			break;
		case 0x3e:
			sprintf(texto,"Projection Matrix strobe %X\r\n", (*geList));
			debuglog(texto);
			break;
		case 0x3f:
			argif = argument << 8;
			memcpy(&fargument, &argif, 4);
			sprintf(texto, "Projection Matrix data: %.4f\r\n", fargument);
			debuglog(texto);
			break;
		case 0x04:
			sprintf(texto,"draw primitive %X\r\n" , argument);
			debuglog(texto);
			break;
		case 0x3c:
			debuglog("view matrix strobe\r\n");
			break;
		case 0x3d:
			//view matrix upload
			argif = argument << 8;
			memcpy(&fargument, &argif, 4);
			sprintf(texto, "ViewMatrix Item %.4f\r\n", fargument);
			debuglog(texto);
			break;
		case 0x15:
			sprintf(texto,"DrawRegion Start:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x16:
			sprintf(texto,"DrawRegion End:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x9c:
			sprintf(texto,"Framebuffer:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x9d:
			sprintf(texto,"Framewidth:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x9e:
			sprintf(texto,"Depthbuffer:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x9f:
			sprintf(texto,"DepthbufferWidth:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xd2:
			sprintf(texto,"Pixelformat:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xcf:
			sprintf(texto,"Fog-Color:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xcd:
			sprintf(texto,"Fog-End:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xce:
			sprintf(texto,"Fog-Range:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xdd:
			sprintf(texto,"Stencil-Op:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xdc:
			sprintf(texto,"StencilFunc:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xe6:
			sprintf(texto,"Logical-Op:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xe7:
			sprintf(texto,"DepthMask:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x9b:
			sprintf(texto,"FrontFace?:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xe8:
			sprintf(texto,"PixelMask(RGB):%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xe9:
			sprintf(texto,"PixelMask(Alpha):%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x0e:
			sprintf(texto,"Signal Interrupt:%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xe2:
		case 0xe3:
		case 0xe4:
		case 0xe5:
			sprintf(texto,"DitherMatrix:%X\r\n", *geList);
			debuglog(texto);
			break;

		case 0xd3:
			sprintf(texto,"clear flags %X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x5b:
			sprintf(texto, "Specular power%X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x42:
			sprintf(texto, "VScale X %X\r\n", *geList);
			debuglog(texto);
			break;
		case 0x43:
			sprintf(texto, "VScale Y %X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xc7:
			sprintf(texto, "TextureWrap %X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xcb:
			sprintf(texto, "TextureFlush %X\r\n", *geList);
			debuglog(texto);
			break;
		case 0xcc:
			sprintf(texto, "TextureSync %X\r\n", *geList);
			debuglog(texto);
			break;


		default:
			sprintf(texto, "%X\r\n", *geList);
			debuglog(texto);
			break;

	}
}
/*
 * do the GE-List default stuff in that function
 */
unsigned int base = 0;
unsigned int npc;
unsigned int baseOffset = 0;

char handleDefaultGeCmd(unsigned int** geList){
	int command = 0;
	unsigned int argument = 0;
	char txt[100];

	command = (**geList) >> 24;
	argument = (**geList) & 0xffffff;
/*#ifdef DEBUG_MODE
	char txt[100];
	sprintf(txt,"handle default cmd: %X-%X\r\n", (unsigned int)(*geList),(unsigned int)(**geList) );
	debuglog(txt);
#endif*/
	switch (command) {
		case 0x08:
			//jump to a new display list address
			npc = ((base | argument) + baseOffset) & 0xFFFFFFFC;
			//jump to new address
			(*geList) = (unsigned int*) (npc | 0x40000000);
			break;
		case 0x09:
			sprintf(txt, "conditional jump:%X-%X | base=%X\r\n", (unsigned int)(*geList), (unsigned int)(**geList), base);
			debuglog(txt);
			//conditional jump - not implemented yet
			(*geList)++;
			break;
		case 0x10:
			//base address
			base = (argument << 8) & 0xff000000;
			(*geList)++;
			break;
		case 0x13:
			//base offset
			baseOffset = (argument << 8);
			(*geList)++;
			break;
		case 0x14:
			//base origin
			baseOffset = (unsigned int)(*geList) - 1;
			(*geList)++;
			break;
		case 0x0a:
			// display list call, put the current address to a stack
			npc = ((base | argument) + baseOffset) & 0xFFFFFFFC;

			//save adress
			adress_arr[adress_number] = (unsigned int)(*geList);
			baseOffset_arr[adress_number] = baseOffset;
			adress_number++;

			(*geList) = (unsigned int*) (npc | 0x40000000);
			break;
		case 0x0b:
			// returning from call, retrieve last address from stack
			adress_number--;
			(*geList) = (unsigned int*) adress_arr[adress_number];
			baseOffset = baseOffset_arr[adress_number];
			(*geList)++;
			break;
		default:
			return 0; //unhandled command
	}

	return 1; //handled
}
/* help method extracts the framebuffer from the display list */
void getFrameBuffFromList(unsigned int* list, unsigned int* frameB, unsigned int* frameW, void* stall){
	char parse = 1;
	char txt[100];
	unsigned int* local_list;

#ifdef DEBUG_MODE
		sprintf(txt, "getFramebuff List: %X Stall: %X\r\n", (unsigned int)list, (unsigned int)stall);
		debuglog(txt);
#endif
	adress_number = 0; //reset adress counter for call's/jumps in display list
	base = 0;
	local_list = list;
	while (parse == 1){
#ifdef TRACE_MODE
		traceGeCmd(local_list);
#endif
		//as long as the current command in the list could not be handled by the default
		//function continue parsing for the data we are currently interested in
		if (!handleDefaultGeCmd(&local_list)){
			if ((*local_list) >> 24 == 0x9C){
				*frameB = (*local_list);
				list++;
				*frameW = (*local_list);
				parse = 0;
			}
			if ((*local_list) >> 24 == 12)
				parse = 0;
			if (stall != 0 && (unsigned int)local_list > (unsigned int)stall)
				parse = 0;

			local_list++;
		}
	}
	//count the tries we would like to get the framebuffer
	getFrameBuffCount++;
}

/*
 * rotate the current view where the origin of the rotation
 * will be a fix point in front of the camera represented by the view
 * matrix
 */
static void Rotate3D(ScePspFMatrix4* view, float angle){
#ifdef TRACE_VIEW_MODE
	char text[150];
#endif
	ScePspFMatrix4 inverse;
	//inverse the matrix to get the camera orientation in world space
	view->w.w = 1.0f;
	gumFullInverse(&inverse, view);
	ScePspFVector3 origin;

	//TEST for GripShift, don't rotate the identity matrix
	//but why does this cause any issue there...
	//the screen is streched on the up-directison on the screen and a bit moved
	//to the right...looks pretty strange...
/*	if (currentConfig.rotateIdentity == 0){
#ifdef DEBUG_MODE
			debuglog("don't rotate identity\r\n");
#endif

		if (inverse.x.x == 1.0f &&
			inverse.y.y == 1.0f &&
			inverse.z.z == 1.0f) {
			return;
		}
	}
	*/
	/*
	 * 49CF6378:ViewMatrix Item 0.0000x
49CF637C:ViewMatrix Item 0.0000y
49CF6380:ViewMatrix Item -1.0000z

49CF6384:ViewMatrix Item -1.0000x
49CF6388:ViewMatrix Item 0.0000y
49CF638C:ViewMatrix Item 0.0000z

49CF6390:ViewMatrix Item 0.0000x
49CF6394:ViewMatrix Item 1.0000y
49CF6398:ViewMatrix Item 0.0000z
	 */

	origin.y = (inverse.w.y - inverse.z.y*currentConfig.rotationDistance);
	origin.z = (inverse.w.z - inverse.z.z*currentConfig.rotationDistance);
	origin.x = (inverse.w.x - inverse.z.x*currentConfig.rotationDistance);

#ifdef TRACE_VIEW_MODE
	sprintf(text, "View-X-Vector: %.3f|%.3f|%.3f|%.3f\r\n", inverse.x.x, inverse.x.y, inverse.x.z, inverse.x.w);
	debuglog(text);
	sprintf(text, "View-Y-Vector: %.3f|%.3f|%.3f|%.3f\r\n", inverse.y.x, inverse.y.y, inverse.y.z, inverse.y.w);
	debuglog(text);
	sprintf(text, "View-Z-Vector: %.3f|%.3f|%.3f|%3.f\r\n", inverse.z.x, inverse.z.y, inverse.z.z, inverse.z.w);
	debuglog(text);
	sprintf(text, "View-Pos: %.3f|%.3f|%.3f|%.3f\r\n", inverse.w.x, inverse.w.y, inverse.w.z, inverse.w.w);
	debuglog(text);
	sprintf(text, "VRO extracted: %.3f|%.3f|%.3f\r\n", origin.x, origin.y, origin.z);
	debuglog(text);
#endif
	//move the camera to a position that makes the origin the 0-point of the coord system
	gumTranslate(view, &origin);
#ifdef TRACE_VIEW_MODE
	ScePspFMatrix4 inverseD;
	gumFullInverse(&inverseD, view);
	sprintf(text, "ViewPos after move: %.3f|%.3f|%.3f\r\n", inverseD.w.x, inverseD.w.y, inverseD.w.z);
	debuglog(text);
#endif
	//now rotate the view
/*	switch (currentConfig.rotationAxis){
	case ERA_Y:
		gumRotateY(view, angle);
		break;
	case ERA_X:
		gumRotateX(view, angle);
		break;
	case ERA_Z:
		gumRotateZ(view, angle);
		break;
	}
	*/
	// TEST start: don't rotate a fixed world axis, but rotate around the view-matrix UP-vector (Y-Axis)
	// as the viewmatrix might be already rotated on 1 or more axis
	//setup the rotation matrix
	float rSin, rCos,r1Cos, roll;
	rSin = sinf(angle);
	rCos = cosf(angle);
	r1Cos = 1 - rCos;
	ScePspFMatrix4 rotMatrix =
			{{inverse.y.x*inverse.y.x*(r1Cos)+rCos, inverse.y.x*inverse.y.y*(r1Cos)-inverse.y.z*rSin, inverse.y.x*inverse.y.z*(r1Cos)+inverse.y.y*rSin, 0.0f},
			 {inverse.y.x*inverse.y.y*(r1Cos)+inverse.y.z*rSin, inverse.y.y*inverse.y.y*(r1Cos)+rCos, inverse.y.y*inverse.y.z*(r1Cos)-inverse.y.x*rSin, 0.0f},
			 {inverse.y.x*inverse.y.z*(r1Cos)-inverse.y.y*rSin, inverse.y.y*inverse.y.z*(r1Cos)+inverse.y.x*rSin, inverse.y.z*inverse.y.z*(r1Cos)+rCos, 0.0f},
			 {0.0f, 0.0f, 0.0f, 1.0f}
			};
	//ScePspFMatrix4 tempView;
	gumMultMatrix(view, view, &rotMatrix);

	//view = tempView;
	//move back the view to the initial place of the origin point
	origin.x = -origin.x;
	origin.y = -origin.y;
	origin.z = -origin.z;
	gumTranslate(view, &origin);

#ifdef TRACE_VIEW_MODE
	ScePspFMatrix4 inverseD2;
	gumFullInverse(&inverseD2, view);
	sprintf(text, "final View-Pos: %.3f|%.3f|%.3f|%.3f\r\n", inverseD2.w.x, inverseD2.w.y, inverseD2.w.z, inverseD2.w.w);
	debuglog(text);
	sprintf(text, "View-X-Vector: %.3f|%.3f|%.3f|%.3f\r\n", inverseD2.x.x, inverseD2.x.y, inverseD2.x.z, inverseD2.x.w);
	debuglog(text);
	sprintf(text, "View-Y-Vector: %.3f|%.3f|%.3f|%.3f\r\n", inverseD2.y.x, inverseD2.y.y, inverseD2.y.z, inverseD2.y.w);
	debuglog(text);
	sprintf(text, "View-Z-Vector: %.3f|%.3f|%.3f|%.3f\r\n", inverseD2.z.x, inverseD2.z.y, inverseD2.z.z, inverseD2.z.w);
	debuglog(text);

#endif
}

/*
 * just call this if we would like to trace specials GE commands or the whole list
 */
void traceGeList(unsigned int* currentList){

	short parse = 1;
	unsigned int *list;
	int command = 0;
	unsigned int argument = 0;

	if (nextStart_list == 0)
		list = currentList;
	else
		list = nextStart_list;

	if (list == 0)
		return;

	char txt[150];
    sprintf(txt,"Trace GE-List: %X\r\n", list );
	debuglog(txt);

	base = 0;
	adress_number = 0; //reset address counter for call's/jumps in display list
	baseOffset = 0;
	//make sure nothing of the GE list is sitting somewhere in the cache...
	sceKernelDcacheWritebackInvalidateAll();
	while (parse){
		traceGeCmd(list);

		if (!handleDefaultGeCmd(&list)){

			command = (*list) >> 24;
			argument = (*list) & 0xffffff;

			if (command != 12){
				switch (command){
				case 0x3c:
					//view matrix strobe
					sprintf(txt, "ViewMatrix : %X\r\n", (*list));
					debuglog(txt);
					break;
				}
			} else {
				parse = 0;
			}

			if (stall_list && list >= stall_list) {
				parse = 0;
			}

			list++;
		}
	}
}
/*
 * do the first stage of the 3D rendering. This is intended to be used
 * to make sure the first set pixel mask is kept.
 */
void Render3dStage1(unsigned int* currentList){

	short parse = 1;
	unsigned int *list;
	int command = 0;
	unsigned int argument = 0;
#ifdef DEBUG_MODE
    char txt[150];
    sprintf(txt,"render3d stage 1: %u, %X\r\n", state, frameBuff[state-1] );
	debuglog(txt);
#endif
	if (nextStart_list == 0)
		list = currentList;
	else
		list = nextStart_list;

	if (list == 0)
		return;
	base = 0;
	adress_number = 0; //reset address counter for call's/jumps in display list
	baseOffset = 0;
	while (parse){
#ifdef TRACE_MODE
		traceGeCmd(list);
#endif
		if (!handleDefaultGeCmd(&list)){

			command = (*list) >> 24;
			argument = (*list) & 0xffffff;

			if (command != 12){
				switch (command){
					case 0x9c:
						//current framebuffer
						//if this framebuffer matches the current one we do activate the manipulation
						//otherwise the stuff is drawn into a off-screen buffer which
						//we are currently not interested in
						if ((*list) == frameBuff[0] || (*list) == frameBuff[1])
							manipulate = 1;
						else
							manipulate = 0;
						break;

					case 0x0e:
						//do not throw the signal on the first run, will be thrown on the second one
						//(*list) = 0x0; //NOP
						//debuglog("interupt 1.\r\n");
						break;

					case 0xe8:
						//prevent pixelmask settings as they "destroy" the 3D renderings
						if (manipulate == 1 && currentConfig.keepPixelmaskOrigin == 0){
							(*list) = 0x0;//(unsigned int) (0xE8 << 24) | (currentConfig.color1);
						}
						break;
					case 0xe9:
						//prevent pixelmask settings as they "destroy" the 3D renderings
						if (manipulate == 1 && currentConfig.keepPixelmaskOrigin == 0){
							(*list) = 0x0;//(unsigned int) (0xE8 << 24) | 0x0; //NOP
						}
						break;

					case 0xd3:
						//clear flags
						//clear seem to be done in the following way:
						//1. get some vertex memory from display list
						//2. set clear flags
						//3. draw something
						//4. reset clear flag
						//as we would like to prevent the list from clearing
						//we set some of the steps to be NOP ...
						if (manipulate == 1){
#ifdef DEBUG_MODE
							debuglog("clear flag\r\n");
#endif
							(*list) &= ((unsigned int) (0xD3 << 24) | (((GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT) << 8) | 0x01)); //clear flag -> prevent color clear!
						} //if manipulate == 1

						break;

				}

			} else {
				parse = 0;
			}
			list++;
		}//!handleDefaultCmd...
		//in case there is a call in the display list it may target
		//beyond the stall adress as it goes to an other display list
		//we should stop in this case only if there is no open call left
		if (stall_list && list >= stall_list
			&& adress_number < 1 ) {
			parse = 0;
		}
	}
}

/*
 * do the second stage of the 3D rendering. This is intended to keep the pixelmask settings
 * as well to grab the view matrix and manipulate the same to get the requested effect
 */
void Render3dStage2(unsigned int* currentList){
	char parse = 1;
	unsigned int *list;
	int command = 0;
	unsigned int argument = 0;
	float fargument = 0.0f;
	int argif = 0;
	short viewItem = 0;
	short modelItem = 0;
	short projItem = 0;

#ifdef DEBUG_MODE
	char txt[150];
	sprintf(txt,"render3d stage 2: %u, %X\r\n", state, frameBuff[state-1] );
	debuglog(txt);
#endif

	if (nextStart_list == 0)
		list = currentList;
	else
		list = nextStart_list;

	if (list == 0)
		return;

	adress_number = 0; //reset adress counter for call's/jumps in display list
	base = 0;
	baseOffset = 0;
	while (parse){
#ifdef TRACE_MODE
		traceGeCmd(list);
#endif
		if (!handleDefaultGeCmd(&list)){
			command = (*list) >> 24;
			argument = (*list) & 0xffffff;

			if (command != 12){
				switch (command){
					case 0x9c:
						//current framebuffer
						//if this framebuffer matches the current one we do activate the manipulation
						//otherwise the stuff is drawn into a off-screen buffer which
						//we are currently not interested in
						if ((*list) == frameBuff[0] || (*list) == frameBuff[1])
							manipulate = 1;
						else
							manipulate = 0;

						frameBuffCount++;
						break;

					case 0x0e:
						//do not throw the signal on the second run
						//(*list) = 0x0; //NOP
						//debuglog("interrupt 2.\r\n");
						break;
					case 0xe8:
						//prevent pixelmask settings as they "destroy" the 3D renderings
						if (manipulate == 1 && currentConfig.keepPixelmaskOrigin == 0){
							(*list) = 0x0;//(unsigned int) (0xE8 << 24) | (currentConfig.color2);
						}
						break;
					case 0xe9:
						//pixelmask alpha
						//prevent pixelmask settings as they "destroy" the 3D renderings
						if (manipulate == 1 && currentConfig.keepPixelmaskOrigin == 0){
							(*list) = 0x0;//(unsigned int) (0xE9 << 24) | (0x0); //NOP
						}
						break;
/*
					case 0x3a:
						modelItem = 0;
						sprintf(txt, "ModelMtx:%X\r\n", (unsigned int)*list);
						debuglog(txt);
						break;
					case 0x3b:
						//store the address to the worldMatrix-Data
						cmdWorldMatrix[modelItem] = list;
						//view matrix upload
						argif = argument << 8;
						memcpy(&fargument, &argif, 4);
						switch (modelItem) {
						case 0:
							view.x.x = fargument;
							break;
						case 1:
							view.x.y = fargument;
							break;
						case 2:
							view.x.z = fargument;
							break;
						case 3:
							view.y.x = fargument;
							break;
						case 4:
							view.y.y = fargument;
							break;
						case 5:
							view.y.z = fargument;
							break;
						case 6:
							view.z.x = fargument;
							break;
						case 7:
							view.z.y = fargument;
							break;
						case 8:
							view.z.z = fargument;
							break;
						case 9:
							view.w.x = fargument;
							break;
						case 10:
							view.w.y = fargument;
							break;
						case 11:
							view.w.z = fargument;
							break;
						}

						modelItem++;
						//if we have gone through all items of the matrix we could recalculate the
						//same and store the data back
						if (modelItem == 12) {
							Rotate3D(&view,-currentConfig.rotationAngle);
							//gumRotateY(&view, -currentConfig.rotationAngle);

							(*cmdWorldMatrix[0]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.x.x)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[1]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.x.y)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[2]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.x.z)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[3]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.y.x)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[4]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.y.y)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[5]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.y.z)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[6]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.z.x)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[7]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.z.y)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[8]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.z.z)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[9]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.w.x)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[10]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.w.y)) >> 8) & 0xffffff);
							(*cmdWorldMatrix[11]) = (unsigned int) (command << 24)
									| (((*((u32*) &view.w.z)) >> 8) & 0xffffff);
						}
						break;
						*/
/*					case 0x3e:
						projItem = 0;
						break;
					case 0x3f:
						if (projItem == 1){
							//as there seem to be no view matrix some times we try to
							//move the model along the X-Axis to get the effect wanted
							argif = argument << 8;
							memcpy(&fargument, &argif, 4);
							fargument+=0.5f;
							(*list) = (unsigned int) (command << 24) | (((*((u32*) &fargument)) >> 8) & 0xffffff);
						}
						projItem++;
						break;
*/
					case 0x3c:
						//view matrix strobe
						viewItem = 0;
						viewMatrixCount++;
						break;
					case 0x3d:
						if (manipulate == 1){
							//store the address to the viewMatrix-Data
							cmdViewMatrix[viewItem] = list;
							//view matrix upload
							argif = argument << 8;
							memcpy(&fargument, &argif, 4);
							switch (viewItem) {
							case 0:
								view.x.x = fargument;
								break;
							case 1:
								view.x.y = fargument;
								break;
							case 2:
								view.x.z = fargument;
								break;
							case 3:
								view.y.x = fargument;
								break;
							case 4:
								view.y.y = fargument;
								break;
							case 5:
								view.y.z = fargument;
								break;
							case 6:
								view.z.x = fargument;
								break;
							case 7:
								view.z.y = fargument;
								break;
							case 8:
								view.z.z = fargument;
								break;
							case 9:
								view.w.x = fargument;
								break;
							case 10:
								view.w.y = fargument;
								break;
							case 11:
								view.w.z = fargument;
								break;
							}

							viewItem++;
							//if we have gone through all items of the matrix we could recalculate the
							//same and store the data back
							if (viewItem == 12) {
								Rotate3D(&view, -currentConfig.rotationAngle);

								(*cmdViewMatrix[0]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.x.x)) >> 8) & 0xffffff);
								(*cmdViewMatrix[1]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.x.y)) >> 8) & 0xffffff);
								(*cmdViewMatrix[2]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.x.z)) >> 8) & 0xffffff);
								(*cmdViewMatrix[3]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.y.x)) >> 8) & 0xffffff);
								(*cmdViewMatrix[4]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.y.y)) >> 8) & 0xffffff);
								(*cmdViewMatrix[5]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.y.z)) >> 8) & 0xffffff);
								(*cmdViewMatrix[6]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.z.x)) >> 8) & 0xffffff);
								(*cmdViewMatrix[7]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.z.y)) >> 8) & 0xffffff);
								(*cmdViewMatrix[8]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.z.z)) >> 8) & 0xffffff);
								(*cmdViewMatrix[9]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.w.x)) >> 8) & 0xffffff);
								(*cmdViewMatrix[10]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.w.y)) >> 8) & 0xffffff);
								(*cmdViewMatrix[11]) = (unsigned int) (command << 24)
										| (((*((u32*) &view.w.z)) >> 8) & 0xffffff);
							}
						} //if manipulate
						break;
					case 0xd3:
						//clear flags
						//clear seem to be done in the following way:
						//1. get some vertex memory from display list
						//2. set clear flags
						//3. draw something
						//4. reset clear flag
						//as we would like to prevent the list from clearing
						//we set some of the steps to be NOP ...
//						if (manipulate == 1){
							(*list) &= ((unsigned int) (0xD3 << 24) | (((GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT) << 8) | 0x01)); //clear flag -> prevent color clear!
//						} //if manipulate == 1
						clearCount++;

						break;
				}
			} else {
				parse = 0;
			}
			list++;
		}//!handleDefaultGeCmd()
		//!handleDefaultCmd...
		//in case there is a call in the display list it may target
		//beyond the stall adress as it goes to an other display list
		//we should stop in this case only if there is no open call left
		if (stall_list && list >= stall_list
			&& adress_number < 1 ) {
			parse = 0;
		}
	}
}

int sceGeListUpdateStallAddr3D(int qid, void *stall) {
	//this is where the display list seem to be passed to the GE
	//for processing
	//the provided stall adress is the current end of the display list
	//this would be the starting address for the GE list passed next, therefore
	//we do not need to run against the whole list each time
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	char txt[150];

	if (draw3D > 0) {
		sprintf(txt, "Update Stall Called: %d, Stall: %X\r\n", qid, stall);
		debuglog(txt);
	}
#endif
	stall_list = (unsigned int*) stall;
	int ret;

	if (draw3D == 3) {
		//while update stall is called we do the 3D-Stage1
		if (currentConfig.needStage1 == 1){
			//make sure there is nothing located in the cache
			sceKernelDcacheWritebackInvalidateAll();
			Render3dStage1(MYlocal_list);
			sceKernelDcacheWritebackInvalidateAll();
		}
	}
	nextStart_list = (unsigned int*) stall;
	pspSdkSetK1(k1);
	ret = sceGeListUpdateStallAddr_Func(qid, stall);

	return ret;
}

/*
 * setup an own display list to clear the whole screen
 */
unsigned int* clearScreen(unsigned int* geList, int listId) {

	unsigned int* local_list = geList;
	struct Vertex* vertices;

	//clear the screen by drawing black to the whole screen
	//setup a sprite
	//get the memory for vertices from GU list
	//first calculate address after the vertex data
	unsigned int* nextList;
	int size = 2 * sizeof(struct Vertex);
	//do a bit of 4 byte alignment of size
	size += 3;
	size += ((unsigned int) (size >> 31)) >> 30;
	size = (size >> 2) << 2;
	nextList = (unsigned int*) (((unsigned int) local_list) + size + 8);
	//store the new pointer as jump target in display list
	(*local_list) = (16 << 24) | ((((unsigned int) nextList) >> 8) & 0xf0000);
	local_list++;
	(*local_list) = (8 << 24) | (((unsigned int) nextList) & 0xffffff);
	local_list++;

	//set the pointer to vertex data
	vertices = (struct Vertex*) local_list;

	//set the real list pointer
	local_list = nextList;

	//pass to GE
//	sceGeListUpdateStallAddr_Func(listId, local_list);

	vertices[0].color = 0x0;
	vertices[0].x = 0;
	vertices[0].y = 0;
	vertices[0].z = 0x0;

	vertices[1].color = 0x0;
	vertices[1].x = 480;
	vertices[1].y = 272;
	vertices[1].z = 0x0;
	//reset pixel mask
	(*local_list) = (unsigned int) (0xE8 << 24) | (0x000000);
	local_list++;
	//pixel mask alpha
	(*local_list) = (unsigned int) (0xE9 << 24) | (0x000000);
	local_list++;
	//start clear: setting clear flag
	(*local_list) = (unsigned int) (0xD3 << 24) | (((GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT) << 8)
			| 0x01);
	local_list++;

	//set vertex type
	(*local_list) = (unsigned int) (18 << 24) | (GU_COLOR_8888
			|GU_VERTEX_16BIT | GU_TRANSFORM_2D);
	local_list++;
	//pass adress to vertex data part 1
	(*local_list) = (unsigned int) (16 << 24) | ((((unsigned int) vertices)
			>> 8) & 0xf0000);
	local_list++;
	//pass adress to vertex data part 2
	(*local_list) = (unsigned int) (1 << 24) | ((((unsigned int) vertices))
			& 0xffffff);
	local_list++;
	//pass drawing primitive and vertex count
	(*local_list) = (unsigned int) (4 << 24) | (GU_SPRITES << 16 | 2);
	local_list++;

	//pass to GE
	sceGeListUpdateStallAddr_Func(listId, local_list);

	//reset the clear flag
	(*local_list) = (unsigned int) (0xD3 << 24) | (0x0);
	local_list++;
	return local_list;
}

/*
 * setup a display list to prepare the 3D rendering
 * this is: clear screen, set current PixelMask
 */
unsigned int* prepareRender3D(unsigned int listId, unsigned int* list, short buffer, unsigned int pixelMask, short withClear, short rot){

#ifdef DEBUG_MODE
	debuglog("prepare 3D - clear/set pixel mask\r\n");
#endif
	if (withClear){
#ifdef DEBUG_MODE
		char txt[100];
		sprintf(txt, "clear screen on buffer %d:%X\r\n", buffer, frameBuff[buffer]);
		debuglog(txt);
#endif
		(*list) = (frameBuff[buffer]);
		list++;
		(*list) = (frameBuffW[buffer]);
		list++;
		list = clearScreen(list, listId);
	}
	//set pixel mask - do not write the color provided (red/cyan)
	(*list) = (unsigned int) (0xE8 << 24) | (pixelMask);
	list++;
	(*list) = (unsigned int) (0xE9 << 24) | (0x000000);
	list++;

	if (currentConfig.addViewMtx == 1 && rot == 1){
#ifdef DEBUG_MODE
		debuglog("add viewMatrix to list\r\n");
#endif
		gumLoadIdentity(&view);
		Rotate3D(&view, currentConfig.rotationAngle);
		(*list) = (unsigned int) 0x3c << 24;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.x.x)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.x.y)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.x.z)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.y.x)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.y.y)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.y.z)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.z.x)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.z.y)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.z.z)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.w.x)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.w.y)) >> 8) & 0xffffff);
		list++;
		(*list) = (unsigned int) (0x3d << 24)
										| (((*((u32*) &view.w.z)) >> 8) & 0xffffff);
		list++;
	}

	//finish the ge list
	(*list) = 15 << 24;
	list++;
	(*list) = 12 << 24;
	list++;

	return list;
}

/*
 * this is where a new display list will be started
 * as seen in retail games, the stall address seem to be set to 0 to
 * pass complete list directly to the hardware for processing. Otherwise the
 * stall address is set to be the same as the GE list starting address.
 * This is used if the sceGeListUpdateStallAddr which does set the pointer
 * within the same GE list step by step forward to pass the other bits to
 * the hardware
 */
int sceGeListEnQueue3D(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
	int k1 = pspSdkSetK1(0);
	int listId;
	short n;
	unsigned int* local_list_s;
	unsigned int* local_list;
#ifdef DEBUG_MODE
	char text[150];
	if (draw3D > 0) {
	//	printf("Enqueue Called: %X, Stall: %X\r\n", list, stall);
		sprintf(text, "Enqueue Called: %X, Stall: %X, cbid: %d, Args: %X\r\n", (unsigned int)list, (unsigned int)stall, cbid, (unsigned int)arg);
		debuglog(text);
/*		if (arg != 0){
			sprintf(text, "Args size=%d\r\n", arg->size);
			debuglog(text);
			for (n=0;n<arg->size;n++){
				sprintf(text, "%u:%u\r\n", n,((unsigned char*)&arg->context)[n]);
				debuglog(text);
			}
		}*/
	}
#endif
	//each time the enqueue was called we reset the next start address for all up coming
	//updateStall calls
	nextStart_list = 0;
	manipulate = 0; //reset the manipulation
	MYlocal_list = (unsigned int*) (((unsigned int) list) | 0x40000000);

	if (draw3D == 1){
		//start new 3d mode, reset some data
		frameBuff[0] = 0;
		frameBuffW[0] = 0;
		frameBuff[1] = 0;
		frameBuffW[1] = 0;

		state = 1;
		afterSync = 0;
		draw3D = 2;
		getFrameBuffCount = 0;
#ifdef DEBUG_MODE
			debuglog("GeListEnqueue - draw3D 1\r\n");
#endif
	}else if (draw3D == 2) {
			numerek = 0;
#ifdef DEBUG_MODE
			debuglog("GeListEnqueue - draw3D 2\r\n");
#endif
		//if we do have the approach where the stall is set to 0 and
		//therefore no updateStallAddr to be used
		if (list != stall && stall == 0){
			listPassedComplete = 1;
		}
	} else if (draw3D == 3 || draw3D == 4) {
		draw3D = 3; //make sure we switch from 4 to 3 to start at the right point in stallUpdate!
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - draw3D 3\r\n");
#endif
		listPassedComplete = 0;
		//enqueue mean starting new list...we would like to add some initial settings
		//first
		//in case Enqueue is called more than once while one draw pass is executed
		//we check for the flag drawSync which is set once the last draw has finished
		local_list_s = (unsigned int*) (((unsigned int) geList3D[0]) | 0x40000000);
		listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0,0);
		//prepare 3d-render: clear screen and set pixel mask - do not write red
		local_list_s = prepareRender3D(listId, local_list_s, state-1, currentConfig.color1, currentConfig.clearScreen, 0);
		sceGeListUpdateStallAddr_Func(listId, local_list_s);
		//sceGeListSync_Func(listId, 0);

		if (stall == 0){
			listPassedComplete = 1;
			//now manipulate the GE list to change the view-Matrix
			numerek++;
			if (currentConfig.needStage1 == 1){
				//make sure there is nothing located in the cache
				sceKernelDcacheWritebackInvalidateAll();
				Render3dStage1((unsigned int*)MYlocal_list);
				//make sure there is nothing located in the cache
				sceKernelDcacheWritebackInvalidateAll();
			}
			//	sceKernelIcacheInvalidateAll();

			//try to render the second frame at the same time to overlay the
			//current draw
			//pass current list to hardware and wait until it was processed

			listId = sceGeListEnQueue_Func(MYlocal_list, 0, cbid, arg);
			//sceGeListSync_Func(listId, 0);

			local_list_s = (unsigned int*) (((unsigned int) geList3D[1]) | 0x40000000);
			listId = sceGeListEnQueue_Func(local_list_s, local_list_s, cbid, arg);

			local_list_s = prepareRender3D(listId, local_list_s, state-1, currentConfig.color2, 0, 1);
			sceGeListUpdateStallAddr_Func(listId, local_list_s);
			//sceGeListSync_Func(listId, 0);
			// do the second run
			//make sure there is nothing located in the cache
			sceKernelDcacheWritebackInvalidateAll();
			frameBuffCount = 0;
			viewMatrixCount = 0;
			clearCount = 0;
			Render3dStage2(MYlocal_list);
#ifdef DEBUG_MODE
		sprintf(text, "viewCount: %d, frameBuffCount: %d, clearCount: %d\r\n", viewMatrixCount, frameBuffCount, clearCount);
		debuglog(text);
#endif

			//make sure there is nothing located in the cache
			sceKernelDcacheWritebackInvalidateAll();
			//sceKernelIcacheInvalidateAll();

		}else if (list != stall){
			//while update stall is called we do the 3D-Stage1
			if (currentConfig.needStage1 == 1){
				//make sure there is nothing located in the cache
				sceKernelDcacheWritebackInvalidateAll();
				Render3dStage1(MYlocal_list);
				sceKernelDcacheWritebackInvalidateAll();
			}
			nextStart_list = (unsigned int*) stall;
		}
		afterSync = 0;
	} else if (draw3D == 9) {
		//stop 3D mode requested...set back the pixel mask
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - draw3D 9\r\n");
#endif
		local_list_s = (unsigned int*) (((unsigned int) geList3D[0])| 0x40000000);
		listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0, 0);

		local_list_s = prepareRender3D(listId, local_list_s, 1, 0x000000, 0, 0);
		sceGeListUpdateStallAddr_Func(listId, local_list_s);
		sceGeListSync_Func(listId, 0);

		draw3D = 0;
	}

	//set addres to some other int variable
	current_list_addres = (unsigned int) &list;
	//
	stall_addres = (unsigned int) &stall;
	stall_list = 0;

	pspSdkSetK1(k1);
	int ret = sceGeListEnQueue_Func(list, stall, cbid, arg);
	return (ret);
}

static int sceGeListEnQueue3DHead(const void *list, void *stall, int cbid,
		PspGeListArgs *arg) {
#ifdef DEBUG_MODE
	debuglog("GeListEnQueueHead\n");
#endif
	int ret = sceGeListEnQueueHead_Func(list, stall, cbid, arg);
	return (ret);
}

static int MYsceGeListDeQueue(int qid) {
#ifdef DEBUG_MODE
	debuglog("GeListDeQueue\n");
#endif
	//canceling current list
	int ret = sceGeListDeQueue_Func(qid);
	return (ret);
}

int sceGeListSync3D(int qid, int syncType) {
	//psp is reseting all video commands right here
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	char txt[100];
	if (draw3D > 0){
		sprintf(txt, "GeListSync %d, %d\r\n", qid, syncType );
		debuglog(txt);
	}
#endif
	//after each sync check for certain bits of the geList
//	if (draw3D > 0)
//		traceGeList(MYlocal_list);

	//we do wait until the list is synchronized
	//reset the next start...
	nextStart_list = 0;
	pspSdkSetK1(k1);
	int ret = sceGeListSync_Func(qid, syncType);
	return (ret);
}

int sceGeDrawSync3D(int syncType) {
	//we do wait until drawing complete and starting a new displaylist
	//reset the next Start address
	int k1 = pspSdkSetK1(0);
	unsigned int* local_list;

	//after each sync check for certain bits of the geList
	//if (draw3D > 0)
		//traceGeList(MYlocal_list);

#ifdef DEBUG_MODE
	char text[150];
	if (draw3D > 0) {
		sceKernelDelayThread(10);
		sprintf(text, "Draw Synch Called: 3D= %d, type=%d\r\n", draw3D, syncType );
		debuglog(text);
	}
#endif

	nextStart_list = 0;
	//if the list was not passed complete, but in chunks using update stall
	//we would need to pass the whole list a second time with the different pixelmask...
	if (draw3D == 3 && listPassedComplete == 0){
#ifdef DEBUG_MODE
		sprintf(text, "pass last list after stall 2nd time %X\r\n", (unsigned int)MYlocal_list);
		debuglog(text);
#endif
		unsigned int * local_list_s = (unsigned int*) (((unsigned int) geList3D[1])| 0x40000000);
		int listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0, 0);
		//set pixel mask and clear screen if necessary - do not draw cyan
		local_list_s = prepareRender3D(listId, local_list_s, state - 1, currentConfig.color2, 0, 1);
		sceGeListUpdateStallAddr_Func(listId, local_list_s);

		manipulate = 0;
		local_list = (unsigned int*)MYlocal_list;
		//make sure there is nothing sitting in the cache
		sceKernelDcacheWritebackInvalidateAll();
		stall_list = 0; //ensure the whole list is processed...
		frameBuffCount = 0;
		viewMatrixCount = 0;
		clearCount = 0;
		Render3dStage2((unsigned int*)local_list);
#ifdef DEBUG_MODE
		sprintf(text, "viewCount: %d, frameBuffCount: %d, clearCount: %d\r\n", viewMatrixCount, frameBuffCount, clearCount);
		debuglog(text);
#endif

		//make sure there is nothing sitting in the cache
		sceKernelDcacheWritebackInvalidateAll();
		//sceKernelIcacheInvalidateAll();
		listId = sceGeListEnQueue_Func(local_list, 0, 0, 0);
		//sceGeListSync_Func(listId, 0);

		//listPassedComplete = 1;

	}
	pspSdkSetK1(k1);
	int ret = sceGeDrawSync_Func(syncType);
	MYlocal_list = 0; // reset the current GE list as it get invalid but
	                  // several drawSync Calls were seen after one enqueue...
	return ret;
}

/*
 * setting the display'd framebuffer to a new address
 */
int sceDisplaySetFrameBuf3D( void * frameBuffer, int buffWidth, int pixelFormat, int syncMode){
	unsigned int temp;
#ifdef DEBUG_MODE
	char txt[150];

	if (draw3D > 0){
		sprintf(txt, "setDisplayBuff - %X, %u, %u, %u\r\n", frameBuffer, buffWidth, pixelFormat, syncMode);
		debuglog(txt);
		//sceKernelDelayThread(100);
	}
#endif

	//this should be the only place where the games will switch between the framebuffers being
	//displayed...getting the different draw buffers here
	if (draw3D == 2){
		temp = ((unsigned int)0x9c << 24) | ((unsigned int)frameBuffer & 0x00ffffff);
#ifdef DEBUG_MODE
		sprintf(txt, "current buffer: %X\r\n", temp);
		debuglog(txt);
#endif
		if ((frameBuff[0] == 0) && (temp != frameBuff[0])){
			frameBuff[0] = temp;
			frameBuffW[0] = ((unsigned int)0x9d << 24) | (unsigned int)buffWidth | (((unsigned int)frameBuffer & 0xff000000) >> 8);
#ifdef DEBUG_MODE
			sprintf(txt, "set buffer 1: %X\r\n", temp);
			debuglog(txt);
#endif

		} else {
			frameBuff[1] = temp;
			frameBuffW[1] = ((unsigned int)0x9d << 24) | (unsigned int)buffWidth | (((unsigned int)frameBuffer & 0xff000000) >> 8);
#ifdef DEBUG_MODE
			sprintf(txt, "set buffer 2: %X\r\n", temp);
			debuglog(txt);
#endif
		}
		if (frameBuff[0] != 0 && frameBuff[1] != 0 && frameBuff[1] != frameBuff[0]){
			draw3D = 3;
#ifdef DEBUG_MODE
			sprintf(txt, "both frame buffer set: %X, %X\r\n", frameBuff[0], frameBuff[1]);
			debuglog(txt);
#endif
		}

	}
	if (draw3D == 3){

		//sceKernelDelayThread(1000);
		temp = ((unsigned int)0x9c << 24) | ((unsigned int)frameBuffer & 0xffffff);
		if (frameBuff[0] == temp)
			state = 2;
		else
			state = 1;
	}
	int ret = sceDisplaySetFrameBuf_Func(frameBuffer, buffWidth, pixelFormat, syncMode);
	return ret;
}

 void (*GeCallback_Func)(int, void *) = NULL;

void GeCallback3D(int id, void* arg){
#ifdef DEBUG_MODE
	char txt[100];
	sprintf(txt, "Callback called: %d\r\n", (unsigned int)id);
	debuglog(txt);
#endif
	if (GeCallback_Func != NULL)
		GeCallback_Func(id, arg);
}

int sceGeSetCallback3D(PspGeCallbackData *cb){
	//PspGeCallbackData cb_d = *cb;
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	char txt[100];
	sprintf(txt, "GeSetCallback: %X\r\n", (unsigned int)cb);
	debuglog(txt);
	sprintf(txt, "GeSetCallback data: Signal:%X - Arg:%X, Finish:%X\ - Arg:%X\r\n", (unsigned int)cb->signal_func,(unsigned int)cb->signal_arg, (unsigned int)cb->finish_func, (unsigned int)cb->finish_arg);
	debuglog(txt);
#endif
	//GeCallback_Func = cb->signal_func;
	//cb_d.signal_func = GeCallback3D;

	pspSdkSetK1(k1);
	int ret = sceGeSetCallback_Func(cb);
	return ret;
}

int sceGeGetMtx3D(int type, void *matrix){
#ifdef DEBUG_MODE
	char txt[100];
	sprintf(txt, "GeGetMtx: %d, %X\r\n", type, (unsigned int)matrix);
	debuglog(txt);
#endif
	int ret = sceGeGetMtx_Func(type, matrix);
	return ret;

}

int sceGeSaveContext3D(void *context){
#ifdef DEBUG_MODE
	char txt[100];
	sprintf(txt, "GeSaveCopntex: %X\r\n", (unsigned int)context);
	debuglog(txt);
#endif
	int ret = sceGeSaveContext_Func(context);
	return ret;

}

int sceGeRestoreContext3D(void *context){
#ifdef DEBUG_MODE
	char txt[100];
	sprintf(txt, "GeRestoreContext: %X\r\n", (unsigned int)context);
	debuglog(txt);
#endif
	int ret = sceGeRestoreContext_Func(context);
	return ret;

}
/*------------------------------------------------------------------------------*/
/* hookDisplay																	*/
/*------------------------------------------------------------------------------*/
void hookFunctions(void) {

#ifdef DEBUG_MODE
	debuglog("get user memory\n");
#endif
	memid = sceKernelAllocPartitionMemory(2, "myGElist1", 0,
			sizeof(unsigned int) * BUFF_SIZE, NULL);
	memid2 = sceKernelAllocPartitionMemory(2, "myGElist2", 0,
				sizeof(unsigned int) * BUFF_SIZE, NULL);

	geList3D[0] = sceKernelGetBlockHeadAddr(memid);
	geList3D[1] = sceKernelGetBlockHeadAddr(memid2);

#ifdef DEBUG_MODE
	debuglog("Start hooking display\n");
#endif

	SceModule *module = sceKernelFindModuleByName( "sceDisplay_Service" );
	if ( module == NULL ){
#ifdef ERROR_LOG
		debuglog("unable to find sceDisplay_Service module\r\n");
#endif

		return;
	}

	if ( sceDisplaySetFrameBuf_Func == NULL )
	{
		sceDisplaySetFrameBuf_Func = HookNidAddress( module, "sceDisplay", 0x289D82FE );
		void *hook_addr = HookSyscallAddress( sceDisplaySetFrameBuf_Func );
		if (hook_addr != NULL){
		HookFuncSetting( hook_addr, sceDisplaySetFrameBuf3D );
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceDisplaySetFrameBuf\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceDisplaySetFrameBuf\r\n");
#endif
		}

	}


	SceModule *module2 = sceKernelFindModuleByName("sceGE_Manager");
	if (module2 == NULL) {
#ifdef ERROR_LOG
		debuglog("unable to find sceGE_Manager module\r\n");
#endif
		return;
	}

	if (sceGeListEnQueue_Func == NULL) {
		sceGeListEnQueue_Func = HookNidAddress(module2, "sceGe_user",
				0xAB49E76A);
		void *hook_addr = HookSyscallAddress(sceGeListEnQueue_Func);
		if (hook_addr != NULL){
			HookFuncSetting(hook_addr, sceGeListEnQueue3D);
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceGeListEnqueue\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListEnQueue\r\n");
#endif
		}
	}

	if (sceGeListEnQueueHead_Func == NULL) {
		sceGeListEnQueueHead_Func = HookNidAddress(module2, "sceGe_user",
				0x1C0D95A6);
		void *hook_addr = HookSyscallAddress(sceGeListEnQueueHead_Func);
		if (hook_addr != NULL){
			HookFuncSetting(hook_addr, sceGeListEnQueue3DHead);
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceGeListEnqueueHead\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListEnQueueHead\r\n");
#endif
		}
	}

	if (sceGeListDeQueue_Func == NULL) {
		sceGeListDeQueue_Func = HookNidAddress(module2, "sceGe_user",
				0x5FB86AB0);
		void *hook_addr = HookSyscallAddress(sceGeListDeQueue_Func);
		if (hook_addr != NULL){
				HookFuncSetting(hook_addr, MYsceGeListDeQueue);
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceGeListDequeue\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListDeQueue\r\n");
#endif
		}
	}

	if (sceGeListSync_Func == NULL) {
		sceGeListSync_Func = HookNidAddress(module2, "sceGe_user", 0x03444EB4);
		void *hook_addr = HookSyscallAddress(sceGeListSync_Func);
		if (hook_addr != NULL){
			HookFuncSetting(hook_addr, sceGeListSync3D);
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceGeListSync\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListSync\r\n");
#endif
		}
	}

	if (sceGeListUpdateStallAddr_Func == NULL) {
		sceGeListUpdateStallAddr_Func = HookNidAddress(module2, "sceGe_user",
				0xE0D68148);
		void *hook_addr = HookSyscallAddress(sceGeListUpdateStallAddr_Func);
		if (hook_addr != NULL){
			HookFuncSetting(hook_addr, sceGeListUpdateStallAddr3D);
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceGeListUpdateStallAddr\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListUpdateStall\r\n");
#endif
		}
	}
	if (sceGeDrawSync_Func == NULL) {
		sceGeDrawSync_Func = HookNidAddress(module2, "sceGe_user", 0xB287BD61);
		void *hook_addr = HookSyscallAddress(sceGeDrawSync_Func);
		if (hook_addr != NULL){
			HookFuncSetting(hook_addr, sceGeDrawSync3D);
#ifdef DEBUG_MODE
	debuglog("Invalidating the cache after hooking sceGeDrawSync\r\n");
#endif
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
		}else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeDrawSync\r\n");
#endif
		}
	}

	if (sceGeSetCallback_Func == NULL) {
			sceGeSetCallback_Func = HookNidAddress(module2, "sceGe_user", 0xA4FC06A4);
			void *hook_addr = HookSyscallAddress(sceGeSetCallback_Func);
			if (hook_addr != NULL){
				HookFuncSetting(hook_addr, sceGeSetCallback3D);
	#ifdef DEBUG_MODE
		debuglog("Invalidating the cache after hooking sceGeSetCallback\r\n");
	#endif
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
			}else {
	#ifdef ERROR_LOG
				debuglog("unable to hook sceGeSetCallback\r\n");
	#endif
			}
		}

	if (sceGeGetMtx_Func == NULL) {
			sceGeGetMtx_Func = HookNidAddress(module2, "sceGe_user", 0x57C8945B);
			void *hook_addr = HookSyscallAddress(sceGeGetMtx_Func);
			if (hook_addr != NULL){
				HookFuncSetting(hook_addr, sceGeGetMtx3D);
	#ifdef DEBUG_MODE
		debuglog("Invalidating the cache after hooking sceGeGetMtx\r\n");
	#endif
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
			}else {
	#ifdef ERROR_LOG
				debuglog("unable to hook sceGeGetMtx\r\n");
	#endif
			}
		}

	if (sceGeSaveContext_Func == NULL) {
			sceGeSaveContext_Func = HookNidAddress(module2, "sceGe_user", 0x438A385A);
			void *hook_addr = HookSyscallAddress(sceGeSaveContext_Func);
			if (hook_addr != NULL){
				HookFuncSetting(hook_addr, sceGeSaveContext3D);
	#ifdef DEBUG_MODE
		debuglog("Invalidating the cache after hooking sceGeSaveContext\r\n");
	#endif
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
			}else {
	#ifdef ERROR_LOG
				debuglog("unable to hook sceGeSaveContext\r\n");
	#endif
			}
		}

	if (sceGeRestoreContext_Func == NULL) {
			sceGeRestoreContext_Func = HookNidAddress(module2, "sceGe_user", 0x0BF608FB);
			void *hook_addr = HookSyscallAddress(sceGeRestoreContext_Func);
			if (hook_addr != NULL){
				HookFuncSetting(hook_addr, sceGeRestoreContext3D);
	#ifdef DEBUG_MODE
		debuglog("Invalidating the cache after hooking sceGeRestoreContext\r\n");
	#endif
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
			}else {
	#ifdef ERROR_LOG
				debuglog("unable to hook sceGeRestoreContext\r\n");
	#endif
			}
		}




#ifdef DEBUG_MODE
	debuglog("End hooking display\r\n");
#endif
}

/*
void unhookFunctions(void) {
#ifdef DEBUG_MODE
	debuglog("Start unhooking display\n");
#endif
	sceKernelFreePartitionMemory(memid);

	//reverse hooking GE modules
	SceModule *module2 = sceKernelFindModuleByName("sceGE_Manager");
	if (module2 == NULL) {
		return;
	}

	if (sceGeListEnQueue_Func != NULL) {
		void *hook_addr = HookSyscallAddress(sceGeListEnQueue_Func);
		HookFuncSetting(hook_addr, sceGeListEnQueue_Func);
	}

	if (sceGeListEnQueueHead_Func != NULL) {
		void *hook_addr = HookSyscallAddress(sceGeListEnQueueHead_Func);
		HookFuncSetting(hook_addr, sceGeListEnQueueHead_Func);
	}

	if (sceGeListDeQueue_Func != NULL) {
		void *hook_addr = HookSyscallAddress(sceGeListDeQueue_Func);
		HookFuncSetting(hook_addr, sceGeListDeQueue_Func);
	}

	if (sceGeListSync_Func != NULL) {
		void *hook_addr = HookSyscallAddress(sceGeListSync_Func);
		HookFuncSetting(hook_addr, sceGeListSync_Func);
	}

	//another test
	if (sceGeListUpdateStallAddr_Func != NULL) {
		void *hook_addr = HookSyscallAddress(sceGeListUpdateStallAddr_Func);
		HookFuncSetting(hook_addr, sceGeListUpdateStallAddr_Func);
	}

	if (sceGeDrawSync_Func != NULL) {
		void *hook_addr = HookSyscallAddress(sceGeDrawSync_Func);
		HookFuncSetting(hook_addr, sceGeDrawSync_Func);
	}
#ifdef DEBUG_MODE
	debuglog("End unhooking display\n");
#endif
}
*/
