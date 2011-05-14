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
#include <pspsdk.h>
#include <pspkernel.h>
#include <systemctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psptypes.h>
#include <stdio.h>
#include <math.h>
#include <pspvfpu.h>
#include "debug.h"
#include "config.h"
#include "hook.h"
#include "blit.h"
//#define TRACE_VIEW_MODE
//#define TRACE_LIST_MODE
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
int (*sceDisplayWaitVblankStart_Func)() = NULL;
int (*sceDisplayWaitVblank_Func)() = NULL;
/* set GE callback function */
int (*sceGeSetCallback_Func)(void *) = NULL;
void (*GeCallback_Func)(int, void *) = NULL;
int (*sceGeGetMtx_Func)(int , void *) = NULL;
int (*sceGeSaveContext_Func)(void *) = NULL;
int (*sceGeRestoreContext_Func)(void *) = NULL;

extern configData currentConfig;
extern char draw3D;

#define BUFF_SIZE 100 // size of the own GE Displaylist buffer
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

char state = 1;
char renderPass = 0;
char afterSync = 0;
char viewMatrixState = 0;
char countEnqueueWithOutDisplay = 0;
struct Vertex {
	u32 color;
	u16 x, y, z;
	u16 pad;
};

SceUID memid, memid2, memid3;
//void *userMemory;
void *geList3D[2];
PspGeCallbackData *geCallbacks_orig;

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
short stopCount;
short pixelSize = 0;
short pixelMaskCount = 0;
unsigned char drawSync = 0;
void *enqueueArg = 0;
typedef struct Interrupts{
	unsigned int addr;
	unsigned int value;
}Interrupts;

unsigned int maxInterrupts = 0;
unsigned int countInterrupts = 0;

Interrupts interrupts[64];
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
	unsigned int* local_list = (*geList);

	command = (*local_list) >> 24;
	argument = (*local_list) & 0xffffff;
#ifdef TRACE_LIST_MODE
	char txt[100];
	sprintf(txt,"handle default cmd: %X-%X\r\n", (unsigned int)(*geList),(unsigned int)(**geList) );
	debuglog(txt);
#endif
	switch (command) {
		case 0x08:
			//jump to a new display list address
			npc = ((base | argument) + baseOffset) & 0xFFFFFFFC;
			//jump to new address
			(*geList) = (unsigned int*) ((npc) | 0x40000000);
			break;
		case 0x09:
			//sprintf(txt, "conditional jump:%X-%X | base=%X\r\n", (unsigned int)(*geList), (unsigned int)(**geList), base);
			//debuglog(txt);
			//conditional jump - not implemented yet
			//just do the jump...
			npc = ((base | argument) + baseOffset) & 0xFFFFFFFC;
			//jump to new address
			(*geList) = (unsigned int*) ((npc) | 0x40000000);
			//(*geList)++;
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

			(*geList) = (unsigned int*) ((npc) | 0x40000000);
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
/*
 * rotate the current view where the origin of the rotation
 * will be a fix point in front of the camera represented by the view
 * matrix
 */
void Rotate3D(ScePspFMatrix4* view, float angle){
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
	if (currentConfig.rotateIdentity == 0){
#ifdef DEBUG_MODE
			debuglog("don't rotate identity\r\n");
#endif

		if (inverse.x.x == 1.0f &&
			inverse.y.y == 1.0f &&
			inverse.z.z == 1.0f) {
			return;
		}
	}
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
	float rSin, rCos,r1Cos;//, roll;
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
 * try using VFPU to do all the mathematic
 */
/*
struct pspvfpu_context *vfpu_context = 0;

void Rotate3D_VFPU(ScePspFMatrix4* view, const float angle){
#ifdef TRACE_VIEW_MODE
	char text[150];
#endif
	//set the VFPU context to be used - make sure we are free to use up to 4
	//matrixes
	ScePspFMatrix4 inverse;
	ScePspFVector3 origin;
	if ( vfpu_context == 0)
		vfpu_context = pspvfpu_initcontext();

	pspvfpu_use_matrices(vfpu_context, 0, VMAT0 | VMAT1 | VMAT2 | VMAT3);
	__asm__ volatile(
			//load the matrix into the VFPU registers M000
			"ulv.q C000, 0+%0\n" //X
			"ulv.q C010,16+%0\n" //Y
			"ulv.q C020,32+%0\n" //Z
			"ulv.q C030,48+%0\n" //W

			//invert the matrix and store to M100
			"vmidt.q M100\n" //identity mtx
			"vmmov.t M100, E000\n"
			"vneg.t  C200, C030\n"
			"vtfm3.t C130, M000, C200\n"
			//for gripshift we need to check for identity matrix --> @TODO

			//get the view's origin, we can use M200 and M300:
			//origin.y = (inverse.w.y - inverse.z.y*currentConfig.rotationDistance);
			//origin.z = (inverse.w.z - inverse.z.z*currentConfig.rotationDistance);
			//origin.x = (inverse.w.x - inverse.z.x*currentConfig.rotationDistance);
			"mtv %3, S220\n"
			"vscl.t C210, C020, S220\n"
			"vsub.t C200, C030, C210\n" //C200 is now the origin
			"sv.q C200, 0+%3\n"

			//move the camera to the origin
			"vmidt.q M300\n"	//identity mtx
			"vmov.t  C330, C200\n"
			"vmmul.q M200, M000, M300\n" //view at origin is now in M200
			//how can we setup the rotation matrix as we need it ?
			//300=110*110*(1-c)+c
			//310=110*111*(1-c)+112*s
			//320=110*113*(1-c)-111*s
			//330=0.0f
			//we need to do some matrix/vector multiplications to achieve this i guess....

			//return inverse,view matrix,origin
			"sv.q C200, 0+%0\n"
			"sv.q C210,16+%0\n"
			"sv.q C220,32+%0\n"
			"sv.q C230,48+%0\n"

			"sv.q C100, 0+%1\n"
			"sv.q C110,16+%1\n"
			"sv.q C120,32+%1\n"
			"sv.q C130,48+%1\n"

			:"+m"(*view), "=m"(inverse), "=m"(origin):"r"(currentConfig.rotationDistance) );

	// TEST start: don't rotate a fixed world axis, but rotate around the view-matrix UP-vector (Y-Axis)
	// as the viewmatrix might be already rotated on 1 or more axis
	//setup the rotation matrix
	float rSin, rCos,r1Cos;//, roll;
	rSin = sinf(angle);
	rCos = cosf(angle);
	r1Cos = 1 - rCos;
	ScePspFMatrix4 rotMatrix =
			{{inverse.y.x*inverse.y.x*(r1Cos)+rCos            , inverse.y.x*inverse.y.y*(r1Cos)-inverse.y.z*rSin, inverse.y.x*inverse.y.z*(r1Cos)+inverse.y.y*rSin, 0.0f},
			 {inverse.y.x*inverse.y.y*(r1Cos)+inverse.y.z*rSin, inverse.y.y*inverse.y.y*(r1Cos)+rCos            , inverse.y.y*inverse.y.z*(r1Cos)-inverse.y.x*rSin, 0.0f},
			 {inverse.y.x*inverse.y.z*(r1Cos)-inverse.y.y*rSin, inverse.y.y*inverse.y.z*(r1Cos)+inverse.y.x*rSin, inverse.y.z*inverse.y.z*(r1Cos)+rCos            , 0.0f},
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
*/
/*
 * just call this if we would like to trace specials GE commands or the whole list
 */
unsigned int* traceGeList(unsigned int* currentList){

	short parse = 1;
	unsigned int *list;
	unsigned int vbase = 0;
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
	void* vertex;
	adress_number = 0; //reset address counter for call's/jumps in display list
	baseOffset = 0;
	unsigned short v;
	typedef struct ColorIVertex{
		u32 color;
		u16 x, y, z;
		u16 pad;
	} ColorIVertex;

	while (parse){
		traceGeCmd(list);

		if (!handleDefaultGeCmd(&list)){

			command = (*list) >> 24;
			argument = (*list) & 0xffffff;

			if (command != 12){
				switch (command){
				case 0x9c:
					sprintf(txt,"Framebuffer:%X\r\n", *list);
					debuglog(txt);
					break;
				case 0x9d:
					sprintf(txt,"Framewidth:%X\r\n", *list);
					debuglog(txt);
					break;
				case 0x9e:
					sprintf(txt,"Depthbuffer:%X\r\n", *list);
					debuglog(txt);
					break;
				case 0x9f:
					sprintf(txt,"DepthbufferWidth:%X\r\n", *list);
					debuglog(txt);
					break;
				case 0xd2:
					sprintf(txt,"Pixelformat:%X\r\n", *list);
					debuglog(txt);
					break;
				case 0x3c:
					//view matrix strobe
					sprintf(txt, "ViewMatrix : %X\r\n", (*list));
					debuglog(txt);
					break;
				case 0x10:
					vbase = (argument << 8) & 0xff000000;
					break;
				case 0x01:
					vertex = (void*)((unsigned int)base | argument);
					break;
				case 0x04:
					sprintf(txt, "Vertex-Data:%X, count: %d\r\n", vertex, argument & 0xffff);
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
	return list;
}


unsigned int finishEntry;
/*
 * do the first stage of the 3D rendering. This is intended to be used
 * to make sure the first set pixel mask is kept.
 */
unsigned int* Render3dStage1(unsigned int* currentList){

	short parse = 1;
	unsigned int *list;
	int command = 0;
	unsigned int argument = 0;
	char txt[150];

	if (nextStart_list == 0)
		list = currentList;
	else
		list = nextStart_list;

	if (list == 0)
		return 0;

#ifdef DEBUG_MODE
    sprintf(txt,"render3d stage 1: %u, %X, list: 0x%X\r\n", state, frameBuff[state-1], list );
	debuglog(txt);
#endif

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
#ifdef DEBUG_MODE
						sprintf(txt, "Framebuffer: 0x%X\r\n", *list);
						debuglog(txt);
#endif
						frameBuffCount++;
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
#ifdef DEBUG_MODE
						//do not throw the signal on the first run, will be thrown on the second one
						//(*list) = 0x0; //NOP
	//					sprintf(txt, "before inerrupt 1: 0x%X : 0x%X\r\n", list-1, *(list-1));
	//					debuglog(txt);
						sprintf(txt, "inerrupt 1: 0x%X : 0x%X\r\n", list, *list);
						debuglog(txt);
						interrupts[maxInterrupts].addr = list;
						interrupts[maxInterrupts].value = *list;
						//(*list) = 0x0;
						maxInterrupts++;
	//					sprintf(txt, "after 1: 0x%X : 0x%X\r\n", list+1, *(list+1));
	//					debuglog(txt);
#endif
						break;
					case 0x0f:
//						sprintf(txt, "finish 1: 0x%X : 0x%X\r\n", list, *list);
//						debuglog(txt);
//						finishEntry = *list;
			//			*list = 0x0f << 24;
						break;
					case 0xe8:
#ifdef DEBUG_MODE
						sprintf(txt, "Pixelmask:0x%X\r\n", *list);
						debuglog(txt);
#endif
						//prevent pixelmask settings as they "destroy" the 3D renderings
						pixelMaskCount++;
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
					case 0x3c:
						//view matrix strobe
						viewMatrixCount++;
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
							sprintf(txt, "%u clear flag\r\n", state);
							debuglog(txt);
#endif
							(*list) &= ((unsigned int) (0xD3 << 24) | (((GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT) << 8) | 0x01)); //clear flag -> prevent color clear!
						} //if manipulate == 1
						clearCount++;
						break;

				}

			} else {
				parse = 0;
				stopCount++;
#ifdef DEBUG_MODE
				sprintf(txt, "end 1: 0x%X : 0x%X\r\n", list, *list);
				debuglog(txt);
#endif
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
	return list;
}

/*
 * do the second stage of the 3D rendering. This is intended to keep the pixelmask settings
 * as well to grab the view matrix and manipulate the same to get the requested effect
 */
void* Render3dStage2(unsigned int* currentList){
	char parse = 1;
	unsigned int *list;
	int command = 0;
	unsigned int argument = 0;
	float fargument = 0.0f;
	int argif = 0;
	short viewItem = 0;
	short modelItem = 0;
	short projItem = 0;

	char txt[150];
#ifdef DEBUG_MODE
	sprintf(txt,"render3d stage 2: %u, %X\r\n", state, frameBuff[state-1] );
	debuglog(txt);
#endif

	if (nextStart_list == 0)
		list = currentList;
	else
		list = nextStart_list;

	if (list == 0)
		return 0;

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
					/*case 0x0:
						if (interrupts[countInterrupts].addr == list){
							(*list) = interrupts[countInterrupts].value;
							countInterrupts++;
						}
						break;*/
					case 0x9c:
#ifdef DEBUG_MODE
						sprintf(txt, "Framebuffer: 0x%X\r\n", *list);
						debuglog(txt);
#endif
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
#ifdef DEBUG_MODE
						sprintf(txt, "inerrupt 2: 0x%X\r\n", *list);
						debuglog(txt);
#endif
						break;
					case 0x0f:
//						sprintf(txt, "finish 2: 0x%X : 0x%X\r\n", list, *list);
	//					debuglog(txt);
		//				*list = finishEntry;
						break;
					case 0xe8:
						//prevent pixelmask settings as they "destroy" the 3D renderings
						pixelMaskCount++;
//						if (*list == 0xe8ffffff){
							//(*list) = (unsigned int) (0xE8 << 24) | (colorModes[currentConfig.colorMode].color2);
//						}else {
							if (manipulate == 1 && currentConfig.keepPixelmaskOrigin == 0){
								(*list) = (unsigned int) (0xE8 << 24) | (colorModes[currentConfig.colorMode].color2);
							}
//						}
						break;
					case 0xe9:
						//pixelmask alpha
						//pixelMaskCount++;
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
						//some games need to rotate while offscreen drawing
						if ((manipulate == 0 && currentConfig.rotAllTime == 2)
							|| (manipulate == 1 && currentConfig.rotAllTime == 0)
							|| currentConfig.rotAllTime == 1){
						//if (manipulate == 1 || currentConfig.rotAllTime == 1){
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
						} //if manipulate || rotAllTime
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
							(*list) &= ((unsigned int) (0xD3 << 24) | (((GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT) << 8) | 0x01)); //clear flag -> prevent color clear!
						} //if manipulate == 1
						clearCount++;

						break;
				}
			} else {
				parse = 0;
				stopCount--;
/*#ifdef DEBUG_MODE
				sprintf(txt, "finish 2: 0x%X : 0x%X\r\n", list, *list);
				debuglog(txt);
#endif*/
			}
			list++;
			if (parse == 0){
/*#ifdef DEBUG_MODE
				sprintf(txt, "%u List after End:%X - stopCount:%d\r\n", state, (unsigned int)(*list), stopCount);
				debuglog(txt);
#endif*/
				if (stopCount > 0)
					parse = 1;
			}
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

	return list;
}

/*
 * setup an own display list to clear the whole screen
 */
unsigned int* clearScreen(unsigned int* geList, unsigned int clearFlags) {

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

	vertices[0].color = 0x00000000;//0xffff0000;
	vertices[0].x = 0x0;
	vertices[0].y = 0x0;
	vertices[0].z = 0x0;
	//vertices[0].pad = 0xffff;

/*	//stencil position for clear as part of color depend on pixel size
	unsigned int filter;
	switch (pixelSize){
	case 0: filter = 0x0; break;              //PixelFormat: 5650 ?
	case 1: filter = 0x0 | 0xff << 31; break; //PixelFormat: 5551?
	case 2: filter = 0x0 | 0xff << 28; break; //PixelFormat: 4444 ?
	case 3: filter = 0x0 | 0xff << 24; break; //PixelFormat: 8888 ?
	default: filter = 0x0;break;
	}
	*/
	vertices[1].color = 0x00000000;//0xffff0110;//0x9c000200;
	vertices[1].x = 512;
	vertices[1].y = 272;
	vertices[1].z = 0xffff;
	//vertices[1].pad = 0xffff;
	//reset pixel mask
	(*local_list) = (unsigned int) (0xE8 << 24) | (0x000000);
	local_list++;
	//pixel mask alpha
	(*local_list) = (unsigned int) (0xE9 << 24) | (0x000000);
	local_list++;
	//start clear: setting clear flag
	(*local_list) = (unsigned int) (0xD3 << 24) | ((clearFlags << 8) | 0x01);
	local_list++;

	//set vertex type
	(*local_list) = (unsigned int) (18 << 24) | (GU_COLOR_8888 |GU_VERTEX_16BIT | GU_TRANSFORM_2D);
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
	char txt[100];
	sprintf(txt,"%u prepare 3D - clear/set pixel mask list:%X\r\n", state, (unsigned int)list);
	debuglog(txt);
#endif
	unsigned int clearFlags;
	if (withClear){
#ifdef DEBUG_MODE
		sprintf(txt, "%u clear screen on buffer %d:%X\r\n", state, buffer, frameBuff[buffer]);
		debuglog(txt);
#endif
		(*list) = (frameBuff[buffer]);
		list++;
		(*list) = (frameBuffW[buffer]);
		list++;
		if (withClear == 2)
			clearFlags = (GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
		else
			clearFlags = (GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
		list = clearScreen(list, clearFlags);
	}
	//set pixel mask - do not write the color provided (red/cyan)
	(*list) = (unsigned int) (0xE8 << 24) | (pixelMask);
	list++;
	(*list) = (unsigned int) (0xE9 << 24) | (0x000000);
	list++;
/*
	if (currentConfig.addViewMtx == 1){
#ifdef DEBUG_MODE
		debuglog("add viewMatrix to list\r\n");
#endif
		gumLoadIdentity(&view);
#ifdef DEBUG_LOG
		unsigned int geMtx[12];
		sceGeGetMtx(9,geMtx);
		u32 t1;
		float t2;
		t1 = geMtx[0] << 8;
		memcpy(&t2, &t1, 4);
		debuglog("GE ViewMtx:");
		t1 = geMtx[0] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[1] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[2] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f\r\n", t2);
		debuglog(txt);

		t1 = geMtx[3] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[4] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[5] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f\r\n", t2);
		debuglog(txt);

		t1 = geMtx[6] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[7] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[8] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f\r\n", t2);
		debuglog(txt);

		t1 = geMtx[9] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[10] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f,", t2);
		debuglog(txt);
		t1 = geMtx[11] << 8;
		memcpy(&t2, &t1, 4);
		sprintf(txt, "%f\r\n", t2);
		debuglog(txt);
#endif
		if (rot == 1){
#ifdef DEBUG_MODE
		debuglog("rotate matrix\r\n");
#endif
			Rotate3D(&view, currentConfig.rotationAngle);
		}
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
	*/

	//finish the ge list
	(*list) = 15 << 24;
	list++;
	//only finish ! No END - WHY ?
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
u64 lastTick, currentTick;
float tickFrequency;
void *fBuff1 = 0,
     *fBuff2 = 0;

void getFramebufferFromList(unsigned int* geList, unsigned int* fb, unsigned int* fbw){
	short parse = 1;
	unsigned int *list;
	int command = 0;
	unsigned int argument = 0;
	char fb_found = 0,
	     fbw_found = 0;
	list = geList;
	base = 0;
	adress_number = 0; //reset address counter for call's/jumps in display list
	baseOffset = 0;

	while (parse){
		if (!handleDefaultGeCmd(&list)){

			command = (*list) >> 24;
			argument = (*list) & 0xffffff;

			if (command != 12){
				switch (command){
					case 0x9c:
						*fb = (*list);
						fb_found = 1;
						break;
					case 0x9d:
						*fbw = (*list);
						fbw_found = 1;
						break;
				}
			} else {
				parse = 0;
				stopCount++;
			}
			list++;
			if (fb_found && fbw_found)
				parse = 0;
		}//!handleDefaultCmd...
		//in case there is a call in the display list it may target
		//beyond the stall adress as it goes to an other display list
		//we should stop in this case only if there is no open call left
		if ((stall_list && list >= stall_list
			&& adress_number < 1 )) {
			parse = 0;
		}
	}
}

char logText[150];
LogData logData;
unsigned int counter = 0;
typedef struct GeListArgs{
	int size;
	u32 p1;
	u32 p2;
	u32 p3;
	u32 p4;
}GeListArgs;

PspGeCallbackData geCallbacks;
int geCallbackId = -1;

int sceGeListUpdateStallAddr3D(int qid, void *stall) {
	//this is where the display list seem to be passed to the GE
	//for processing
	//the provided stall adress is the current end of the display list
	//this would be the starting address for the GE list passed next, therefore
	//we do not need to run against the whole list each time
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	char txt[150];

/*	if (draw3D > 0) {
		sprintf(txt, "Update Stall Called: %d, Stall: %X\r\n", qid, stall);
		debuglog(txt);
	}
	*/
#endif
	stall_list = (unsigned int*) stall;
	int ret;

	if (draw3D == 3) {
//		traceGeList(MYlocal_list);
		//while update stall is called we do the 3D-Stage1
		//make sure there is nothing located in the cache
		sceKernelDcacheWritebackInvalidateAll();
		if (currentConfig.flipFlop == 1){
			if (state == 1){
				if (currentConfig.needStage1 == 1){
					renderPass = 1;
					Render3dStage1((unsigned int*)MYlocal_list);
				}
			} else {
				renderPass = 2;
				Render3dStage2((unsigned int*)MYlocal_list);
				//geCallbackId = sceGeSetCallback_Func(geCallbacks_orig);
			}
		} else {
			if (currentConfig.needStage1 == 1){
				Render3dStage1(MYlocal_list);
			}
		}
		sceKernelDcacheWritebackInvalidateAll();
	}
	nextStart_list = (unsigned int*) stall;
	pspSdkSetK1(k1);
	ret = sceGeListUpdateStallAddr_Func(qid, stall);

	return ret;
}

/*
 * within GTA this function is triggered as part of an DISPLAY_VBLANK interrupt!
 * there seem to be limitations to some functions in this case...
 * I do currently assume this is where drawing to the display buffer takes place..
 * however, this seem not to be the only Enqueue as the usual enqueues are called differently....
 */
/*
 * FF 7 - CrisisCore uses many signals...
 * Signals are of a special type and an argument
 * 0x0eSSaaaa
 * there is always used signal 01 with different arguments..
 * once argument is 0x64 the PSP crashes if the display list is passed 2nd time during one render step...
 */
/*
 * FIFA 11 calls Enqueue twice...need to ignore the second run (see config) !
 */
short enqueueRun = 0;
int sceGeListEnQueue3D(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
	int k1 = pspSdkSetK1(0);
	int listId;
	short n;
	unsigned int* local_list_s;
	unsigned int* ll_start;
	unsigned int* local_list;
	char text[150];

/*	if (draw3D > 0) {

		if (sceKernelIsIntrContext() == 1){
			//this seem to be the case in GTA when rendering to the screen..
			//assumed is rendering to an off screen area and using this as texture to render
			//to on-screen within a VBlank-Wait interrupt..
			//we do ignore this kind of list from plugin point of view
			//however, this occurs on each vBlank...which would be 60 times per sec.
			//guessing that there have to be a call back in place indicating that the real drawing
			//has taken place...
			//char* txt = "Enqueue-Interrupt: %X, Stall: %X, counter: %d\r\n";
			//strncpy(logText, txt, strlen(txt));
			logData.txt = "Enqueue-Interrupt: %X, Stall: %X, cbid: %d\r\n";
			logData.p1 = (unsigned int)list;
			logData.p2 = (unsigned int)stall;
			logData.p3 = cbid;
			debuglog_special(&logData);

			pspSdkSetK1(k1);
			return sceGeListEnQueue_Func(list, stall, cbid, arg);
		//	debuglog("interrupt context\r\n");
		}
	}
*/
#ifdef DEBUG_MODE
	if (draw3D > 0){
		//	printf("Enqueue Called: %X, Stall: %X\r\n", list, stall);
		sprintf(text, "Enqueue Called: %X, Stall: %X, cbid: %d, Args: %X\r\n", (unsigned int)list, (unsigned int)stall, cbid, (unsigned int)arg);
		debuglog(text);
		/*if (arg != 0){
			GeListArgs* test = (GeListArgs*)arg;
			sprintf(text, "Args1:0x%X, Args2:0x%X, Args3:0x%X, Args4:0x%X, Args5:0x%X\r\n", test->size, test->p1, test->p2, test->p3, test->p4);
			debuglog(text);
		}*/
	}
#endif
	//each time the enqueue was called we reset the next start address for all up coming
	//updateStall calls
	nextStart_list = 0;
	//manipulate = 0; //reset the manipulation
	MYlocal_list = (unsigned int*) (((unsigned int) list) | 0x40000000);
	enqueueArg = arg;

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
		stopCount = 0;
		countEnqueueWithOutDisplay = 0;
#ifdef DEBUG_MODE
			debuglog("GeListEnqueue - draw3D 1\r\n");
#endif
	}else if (draw3D == 2) {
			numerek = 0;
			if (countEnqueueWithOutDisplay >= 0){
				countEnqueueWithOutDisplay++;
			}
			if (countEnqueueWithOutDisplay > 10){
				//we have never seen the sceDisplaySetFrameBuff function, we need to get the
				//different framebuffer from the diplay list..
#ifdef DEBUG_MODE
				debuglog("fallback framebuffer\r\n");
#endif
				getFramebufferFromList((unsigned int*)MYlocal_list, &frameBuff[state-1], &frameBuffW[state-1]);
#ifdef DEBUG_MODE
				sprintf(text, "framebuffer %u : %X\r\n", state, frameBuff[state-1]);
				debuglog(text);
#endif
				if (frameBuff[0] && frameBuff[1]){
#ifdef DEBUG_MODE
					sprintf(text, "Fallback framebuffer found; %X - %X\r\n", frameBuff[0], frameBuff[1]);
					debuglog(text);
#endif
					draw3D = 4;
				}
			}
#ifdef DEBUG_MODE
			debuglog("GeListEnqueue - draw3D 2\r\n");
#endif
		//if we do have the approach where the stall is set to 0 and
		//therefore no updateStallAddr to be used
		if (list != stall && stall == 0){
			listPassedComplete = 1;
		}
		//sceRtcGetCurrentTick(&lastTick);
		//tickFrequency = 1.0f / sceRtcGetTickResolution();
	} else if (draw3D == 3 || draw3D == 4) {
		//make there there is no interupt interfering with the following processing
		//int flags = sceKernelSuspendIntr();
		draw3D = 3; //make sure we switch from 4 to 3 to start at the right point in stallUpdate!

#ifdef DEBUG_MODE
		sprintf(text,"%u GeListEnqueue - draw3D 3\r\n" , state);
		debuglog(text);
#endif

		//traceGeList(list);

		listPassedComplete = 0;
		stopCount = 0;
		renderPass = 1;
		enqueueRun++;
		if (currentConfig.ignoreEnqueueCount && enqueueRun > currentConfig.ignoreEnqueueCount){
			//ignore the second run on FIFA 11
			//debuglog("ignore enqueue run\r\n");
			//reset the pixelmask for this run...
			local_list_s = (unsigned int*) (((unsigned int) geList3D[0])| 0x40000000);
			listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0, 0);

			local_list_s = prepareRender3D(listId, local_list_s, 1, 0x000000, 0, 0);
			sceGeListUpdateStallAddr_Func(listId, local_list_s);
			sceGeListSync_Func(listId, 0);
			pspSdkSetK1(k1);
			return sceGeListEnQueue_Func(list, stall, cbid, arg);
		}
//		unsigned int interruptState = sceKernelCpuSuspendIntr();
		/*if (geCallbackId >= 0){
			debuglog("re-set callback\r\n");
			sceGeUnsetCallback(geCallbackId);
			geCallbackId = sceGeSetCallback_Func(geCallbacks_orig);
			geCallbackId = -2;
		}*/
		//enqueue mean starting new list...we would like to add some initial settings
		//first
		//in case Enqueue is called more than once while one draw pass is executed
		//we check for the flag drawSync which is set once the last draw has finished
		local_list_s = (unsigned int*) (((unsigned int) geList3D[0]) | 0x40000000);
		ll_start = local_list_s;
		//listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0,0);
		//prepare 3d-render: clear screen and set pixel mask - do not write red
		if (currentConfig.flipFlop == 0){
			local_list_s = prepareRender3D(listId, local_list_s, state-1, colorModes[currentConfig.colorMode].color1, currentConfig.clearScreen, 0);
		} else {
			if (state == 1){
				local_list_s = prepareRender3D(listId, local_list_s, state-1, colorModes[currentConfig.colorMode].color1, currentConfig.clearScreen, 0);
			} else{
				local_list_s = prepareRender3D(listId, local_list_s, state-1, colorModes[currentConfig.colorMode].color2, currentConfig.clearScreen, 0);
			}
		}
		//sceGeListUpdateStallAddr_Func(listId, local_list_s);
//		debuglog("enqueue local list\r\n");
		listId = sceGeListEnQueue_Func(ll_start, 0, 0,0);
		//debuglog("sync local list\r\n");
		//sceGeListSync_Func(listId, 0);

		if (stall == 0){
			//traceGeList((unsigned int*)list);
			listPassedComplete = 1;
			//now manipulate the GE list to change the view-Matrix
			if (currentConfig.flipFlop == 0){
				//Gods Eater does work stable only if we switch red/cyan mode each frame
				//and do not pass the display list twice..this does stop rendering for an unknown reason..
				if (currentConfig.needStage1 == 1){// && numerek < 3){
					//make sure there is nothing located in the cache
					sceKernelDcacheWritebackInvalidateAll();
	#ifdef DEBUG_MODE
					sprintf(text, "Stage 1 List 0x%X\r\n", MYlocal_list);
					debuglog(text);
	#endif
					maxInterrupts = 0;
					Render3dStage1((unsigned int*)MYlocal_list);
					//make sure there is nothing located in the cache
					sceKernelDcacheWritebackInvalidateAll();
				}
			}
			if (currentConfig.flipFlop == 1){
				if (state == 1){
					if (currentConfig.needStage1 == 1){
						renderPass = 1;
						Render3dStage1((unsigned int*)MYlocal_list);
						//debuglog("after stage 1 manipulation\r\n");
					//debuglog("pass 1. enqueue\r\n");
	//				sceGeListEnQueue_Func((unsigned int*)list, 0, cbid, arg);
					//renderPass = 2;
					//sceGeListEnQueue_Func((unsigned int*)list, 0, 0, 0);
					//debuglog("set call back again\r\n");
					//geCallbackId = sceGeSetCallback_Func(geCallbacks_orig);
					//debuglog("call back reset\r\n");
					}
				} else {
					renderPass = 2;
					Render3dStage2((unsigned int*)MYlocal_list);
					//geCallbackId = sceGeSetCallback_Func(geCallbacks_orig);
				}
			} else {

				//try to render the second frame at the same time to overlay the
				//current draw
				//pass current list to hardware and wait until it was processed

				//debuglog("pass list 1. time\r\n");
				listId = sceGeListEnQueue_Func((unsigned int*)list, 0, cbid, arg);
				//debuglog("sync 1. pass\r\n");
				sceGeListSync_Func(listId, 0);

				local_list_s = (unsigned int*) (((unsigned int) geList3D[1]) | 0x40000000);
				ll_start = local_list_s;
				//listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0, 0);
				local_list_s = prepareRender3D(listId, local_list_s, state-1, colorModes[currentConfig.colorMode].color2, 2, 1);
				//sceGeListUpdateStallAddr_Func(listId, local_list_s);
				//debuglog("enqueue local list2\r\n");
				listId = sceGeListEnQueue_Func(ll_start, 0, 0,0);
				//debuglog("sync local list2\r\n");
				//sceGeListSync_Func(listId, 0);

				// do the second run
				//make sure there is nothing located in the cache
				sceKernelDcacheWritebackInvalidateAll();
				frameBuffCount = 0;
				viewMatrixCount = 0;
				clearCount = 0;
				pixelMaskCount = 0;
				countInterrupts = 0;
/*#ifdef DEBUG_MODE
			sprintf(text, "Stage 2 List 0x%X\r\n", MYlocal_list);
			debuglog(text);
#endif*/
				Render3dStage2((unsigned int*)MYlocal_list);
				renderPass = 2;
	#ifdef DEBUG_MODE
			sprintf(text, "%u viewCount: %d, frameBuffCount: %d, clearCount: %d, pmCount: %d\r\n", state, viewMatrixCount, frameBuffCount, clearCount, pixelMaskCount);
			debuglog(text);
	#endif
				//make sure there is nothing located in the cache

			}
			sceKernelDcacheWritebackInvalidateAll();
	//		sceKernelCpuResumeIntr(interruptState);
		}else if (list != stall){
			//while update stall is called we do the 3D-Stage1
				//make sure there is nothing located in the cache
				sceKernelDcacheWritebackInvalidateAll();
				if (currentConfig.flipFlop == 1){
					if (state == 1){
						if (currentConfig.needStage1 == 1){
							renderPass = 1;
							Render3dStage1((unsigned int*)MYlocal_list);
						}
					} else {
						renderPass = 2;
						Render3dStage2((unsigned int*)MYlocal_list);
						//geCallbackId = sceGeSetCallback_Func(geCallbacks_orig);
					}
				} else {
					if (currentConfig.needStage1 == 1){
						renderPass = 1;
						Render3dStage1((unsigned int*)MYlocal_list);
					}
				}
				sceKernelDcacheWritebackInvalidateAll();
			nextStart_list = (unsigned int*) ((unsigned int)stall | 0x40000000);
		}
		afterSync = 0;
		//sceKernelResumeIntr(flags);
		//geCallbackId = sceGeSetCallback_Func(geCallbacks_orig);
		//debuglog("set callback\r\n");

	} else if (draw3D == 9) {
		//stop 3D mode requested...set back the pixel mask
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - draw3D 9\r\n");
#endif
		renderPass = 1;
		local_list_s = (unsigned int*) (((unsigned int) geList3D[0])| 0x40000000);
		listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0, 0);

		local_list_s = prepareRender3D(listId, local_list_s, 1, 0x000000, 0, 0);
		sceGeListUpdateStallAddr_Func(listId, local_list_s);
		sceGeListSync_Func(listId, 0);
		draw3D = 0;
		stopCount = 0;
	}

	//set addres to some other int variable
	current_list_addres = (unsigned int) &list;
	//
	stall_addres = (unsigned int) &stall;
	stall_list = 0;
#ifdef DEBUG_MODE
	if (draw3D == 3)
		debuglog("before pass geList using orig. Function\r\n");
#endif
	pspSdkSetK1(k1);
	int ret = sceGeListEnQueue_Func(list, stall, cbid, arg);
	return (ret);
}

int sceGeListEnQueue3DHead(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
#ifdef DEBUG_MODE
	debuglog("GeListEnQueueHead\n");
#endif
	int ret = sceGeListEnQueueHead_Func(list, stall, cbid, arg);
	return (ret);
}

int MYsceGeListDeQueue(int qid) {
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
	drawSync = 1;
	//after each sync check for certain bits of the geList
	//if (draw3D == 3)
		//traceGeList(MYlocal_list);
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
	drawSync = 1;
	//after each sync check for certain bits of the geList
	//if (draw3D == 0)
		//traceGeList(MYlocal_list);
	char text[150];
#ifdef DEBUG_MODE

	if (draw3D > 0) {
		//sceKernelDelayThread(10);
		if (sceKernelIsIntrContext() == 1){
			//this seem to be the case in GTA when rendering to the screen..
			//assumed is rendering to an offscreen area and using this as texture to render
			//to on-screen within a VBlank-Wait interrupt..
			//we do ignore this kind of list...
			char* txt = "DrawSync\r\n";
			strncpy(logText, txt, strlen(txt));
			logData.txt = logText;
			logData.p1 = 0;
			logData.p2 = 0;
			logData.p3 = 0;
			debuglog_special(&logData);
			pspSdkSetK1(k1);
			MYlocal_list = 0;
			int ret = sceGeDrawSync_Func(syncType);
		}
		sprintf(text, "%u Draw Synch Called: 3D= %d, type=%d\r\n",state, draw3D, syncType);
		debuglog(text);
	}
#endif

	nextStart_list = 0;

	//if the list was not passed complete, but in chunks using update stall
	//we would need to pass the whole list a second time with the different pixelmask...
	if (draw3D == 3 && listPassedComplete == 0 && currentConfig.flipFlop == 0){
#ifdef DEBUG_MODE
		sprintf(text, "pass last list after stall 2nd time %X\r\n", (unsigned int)MYlocal_list);
		debuglog(text);
#endif
		//int interupts = pspSdkDisableInterrupts();
		unsigned int * local_list_s = (unsigned int*) (((unsigned int) geList3D[1])| 0x40000000);
		//pspSdkSetK1(k1);
		int listId = sceGeListEnQueue_Func(local_list_s, local_list_s, 0, 0);
//		debuglog("pass 2 enqueue\r\n");
		//k1 = pspSdkSetK1(0);;
		//set pixel mask and clear screen if necessary - do not draw cyan
		local_list_s = prepareRender3D(listId, local_list_s, state - 1, colorModes[currentConfig.colorMode].color2, 2, 1);
		//pspSdkSetK1(k1);
		sceGeListUpdateStallAddr_Func(listId, local_list_s);
//		debuglog("pass 2 stallupdate clear\r\n");
		//k1 = pspSdkSetK1(0);
		manipulate = 0;
		local_list = (unsigned int*)MYlocal_list;
		//make sure there is nothing sitting in the cache
		sceKernelDcacheWritebackInvalidateAll();
		stall_list = 0; //ensure the whole list is processed...
		frameBuffCount = 0;
		viewMatrixCount = 0;
		clearCount = 0;
//		debuglog("before stage 2\r\n");
		void* stall = Render3dStage2((unsigned int*)local_list);
#ifdef DEBUG_MODE
		sprintf(text, "%u viewCount: %d, frameBuffCount: %d, clearCount: %d\r\n", state, viewMatrixCount, frameBuffCount, clearCount);
		debuglog(text);
		sprintf(text, "%u List 2:%X - stall: %X\r\n", state, local_list, stall);
		debuglog(text);
#endif

		//make sure there is nothing sitting in the cache
		sceKernelDcacheWritebackInvalidateAll();
		//sceKernelIcacheInvalidateAll();
		//pspSdkSetK1(k1);
	//	debuglog("pass 2 real list enqueue\r\n");
		listId = sceGeListEnQueue_Func(local_list, 0, 0, enqueueArg);
		//k1 = pspSdkSetK1(0);
		sceGeListSync_Func(listId, 0);
		listPassedComplete = 1;
		//pspSdkEnableInterrupts(interupts);
		sceRtcGetCurrentTick(&currentTick);
		float diff = (currentTick - lastTick)*tickFrequency;
		//if (diff < 0.1f)
			//sceKernelDelayThread(100000);
		//sprintf(text,"tick-difference: %u.%.5u\r\n", (u16)diff, (u16)(diff*100000));
		//debuglog(text);
		lastTick = currentTick;
	}
	manipulate = 0;
	numerek = 0;
	pspSdkSetK1(k1);
	int ret = sceGeDrawSync_Func(syncType);
	MYlocal_list = 0; // reset the current GE list as it get invalid but
	                  // several drawSync Calls were seen after one enqueue...
	return ret;
}

int sceDisplayWaitVblankStart3D(){
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	if (draw3D > 0){
		debuglog("WaitVBlankStart\r\n");
		sceKernelDelayThread(100000);
	}
#endif
	pspSdkSetK1(k1);
	int ret = sceDisplayWaitVblankStart_Func();
	return ret;
}

int sceDisplayWaitVblank3D(){
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	if (draw3D > 0){
		debuglog("WaitVBlank\r\n");
		sceKernelDelayThread(100000);
	}
#endif
	pspSdkSetK1(k1);
	int ret = sceDisplayWaitVblankStart_Func();
	return ret;
}
/*
 * setting the display'd framebuffer to a new address
 */
int sceDisplaySetFrameBuf3D( void * frameBuffer, int buffWidth, int pixelFormat, int syncMode){
	int k1 = pspSdkSetK1(0);
	unsigned int temp;
	char txt[150];
	countEnqueueWithOutDisplay = -1;

#ifdef DEBUG_MODE

	if (draw3D > 0){
		if (sceKernelIsIntrContext() == 1){
			//this seem to be the case in GTA when rendering to the screen..
			//assumed is rendering to an off screen area and using this as texture to render
			//to on-screen within a VBlank-Wait interrupt..
			//we do ignore this kind of list from plugin point of view
			//however, this occurs on each vBlank...which would be 60 times per sec.
			//guessing that there have to be a call back in place indicating that the real drawing
			//has taken place...
			//char* txt = "Enqueue-Interrupt: %X, Stall: %X, counter: %d\r\n";
			//strncpy(logText, txt, strlen(txt));
			logData.txt = "Interrupt:setDisplayBuff - %X, %u, %u\r\n";
			logData.p1 = (unsigned int)frameBuffer;
			logData.p2 = (unsigned int)buffWidth;
			logData.p3 = (unsigned int)pixelFormat;
			debuglog_special(&logData);
		}else{
			sprintf(txt, "setDisplayBuff - %X, %u, %u, %u\r\n", frameBuffer, buffWidth, pixelFormat, syncMode);
			debuglog(txt);
		}
		//sceKernelDelayThread(10000);
	}
#endif
	//int interupts = pspSdkDisableInterrupts();
	//this should be the only place where the games will switch between the framebuffers being
	//displayed...getting the different draw buffers here
	if (draw3D == 2){
		if (fBuff1 == 0) fBuff1 = frameBuffer;
		if (fBuff2 == 0 && fBuff1 != frameBuffer){
		//	draw3D = 3;

		}
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
		if (frameBuff[0] != 0 && frameBuff[1] != 0){
			if (currentConfig.fixedFrameBuffer == 0 && frameBuff[1] != frameBuff[0]){
				draw3D = 3;
			}
			if (currentConfig.fixedFrameBuffer != 0){
				//in case of FiFa 11 the display buffer is always the same...the frame buffer as well
				//TODO:FB for FIFA is fixed..frameBuff[0] = frameBuff[1] = 0x9C044000;
				frameBuff[0] = frameBuff[1] = ((unsigned int) 0x9C << 24) | (currentConfig.fixedFrameBuffer & 0xfffff);
				draw3D = 3;
			}
			if (draw3D == 3){
				//blit_setup( );
#ifdef DEBUG_MODE
			sprintf(txt, "both frame buffer set: %X, %X\r\n", frameBuff[0], frameBuff[1]);
			debuglog(txt);
#endif
			}
		}

	}
	if (draw3D == 3){
		enqueueRun = 0;
		//sceKernelDelayThread(1000);
		pixelSize = pixelFormat;
		temp = ((unsigned int)0x9c << 24) | ((unsigned int)frameBuffer & 0xffffff);
		if (frameBuff[0] == temp)
			state = 2;
		else
			state = 1;
/*
		if (currentConfig.showStat == 1){
			float angle = currentConfig.rotationAngle*180.0f/GU_PI;
			char ah = (char)angle;
			short al = angle*1000 - (short)ah*1000;
			char ph = (char)currentConfig.rotationDistance;
			short pl = currentConfig.rotationDistance*1000 - (short)ph*1000;
			sprintf(txt, "A: %d.%.3d | P: %d.%.3d", ah, al, ph, pl);
			blit_string2(frameBuffer,buffWidth,pixelFormat,1,1, txt);
		}
		*/
	}
    //pspSdkEnableInterrupts(interupts);
	pspSdkSetK1(k1);
	int ret = sceDisplaySetFrameBuf_Func(frameBuffer, buffWidth, pixelFormat, syncMode);
	return ret;
}

void (*geFinishCallBack)(int, void * arg) = NULL;

void GeCallback3D(int id, void* arg){
//#ifdef DEBUG_MODE
	char txt[100];
//#endif
	if (draw3D == 3){
		sprintf(txt, "CB id: %u, pass: %u\r\n", id, renderPass);
		debuglog(txt);
		if (renderPass == 1) return;
		if (renderPass == 2 && GeCallback_Func != NULL) GeCallback_Func(id, arg);
	} else {
		if (GeCallback_Func != NULL) GeCallback_Func(id, arg);
	}
}

int sceGeSetCallback3D(PspGeCallbackData *cb){
	//PspGeCallbackData cb_d = *cb;
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	char txt[100];
	if (sceKernelIsIntrContext() == 1){
		//this seem to be the case in GTA when rendering to the screen..
		//assumed is rendering to an off screen area and using this as texture to render
		//to on-screen within a VBlank-Wait interrupt..
		//we do ignore this kind of list from plugin point of view
		//however, this occurs on each vBlank...which would be 60 times per sec.
		//guessing that there have to be a call back in place indicating that the real drawing
		//has taken place...
		//char* txt = "Enqueue-Interrupt: %X, Stall: %X, counter: %d\r\n";
		//strncpy(logText, txt, strlen(txt));
		logData.txt = "IR - GeSetCallback data: Signal:%X, Finish:%X\r\n";
		logData.p1 = (unsigned int)cb->signal_func;
		logData.p2 = (unsigned int)cb->finish_arg;
		logData.p3 = 0;
		debuglog_special(&logData);
	}else {
	sprintf(txt, "GeSetCallback: %X\r\n", (unsigned int)cb);
	debuglog(txt);
	sprintf(txt, "GeSetCallback data: Signal:%X - Arg:%X, Finish:%X - Arg:%X, mycb:%X\r\n", (unsigned int)cb->signal_func,(unsigned int)cb->signal_arg, (unsigned int)cb->finish_func, (unsigned int)cb->finish_arg, (unsigned int)GeCallback3D);
	debuglog(txt);
	}
#endif
	//store the current callback settings
	(*geCallbacks_orig) = (*cb);
	//geFinishCallBack = cb->finish_func;
	GeCallback_Func = cb->finish_func;
	geCallbacks_orig->finish_func = GeCallback3D;

	pspSdkSetK1(k1);
	int ret = sceGeSetCallback_Func(cb);
	geCallbackId = ret;
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

//unsigned int origLoader[11];
/*------------------------------------------------------------------------------*/
/* hookDisplay																	*/
/*------------------------------------------------------------------------------*/
void hookFunctions(void) {
	char txt[100];
#ifdef DEBUG_MODE
	debuglog("get user memory\r\n");
#endif
	memid = sceKernelAllocPartitionMemory(2, "myGElist1", 0,
			sizeof(unsigned int) * BUFF_SIZE, NULL);
	if (memid < 0){
#ifdef ERROR_LOG
		debuglog("unable to get memory for custom display list1\r\n");
#endif
		draw3D = 0;
		return;
	}
	memid2 = sceKernelAllocPartitionMemory(2, "myGElist2", 0,
				sizeof(unsigned int) * BUFF_SIZE, NULL);
	if (memid2 < 0){
#ifdef ERROR_LOG
		debuglog("unable to get memory for custom display list2\r\n");
#endif
		draw3D = 0;
		return;
	}
	geList3D[0] = sceKernelGetBlockHeadAddr(memid);
	geList3D[1] = sceKernelGetBlockHeadAddr(memid2);

	memid3 = sceKernelAllocPartitionMemory(2, "myGEcallback", 0, 2*sizeof(PspGeCallbackData), NULL);
	geCallbacks_orig = (PspGeCallbackData*)sceKernelGetBlockHeadAddr(memid3);
#ifdef DEBUG_MODE
	debuglog("Start hooking display\r\n");
	sprintf(txt, "HEN Version : %X\r\n", sctrlHENGetVersion());
	debuglog(txt);
#endif

	u32 originalAddr;
/*
	//TEST
	hookAPI("sceDisplay_Service", "sceDisplay", 0x289D82FE, sceDisplaySetFrameBuf3D, HOOK_SYSCALL, origLoader);
	sceDisplaySetFrameBuf_Func = (int(*)(void))origLoader;
	debuglog("after setFrameBuff hook\r\n");
*/

	originalAddr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0x289D82FE);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceDisplaySetFrameBuf3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceDisplaySetFrameBuf_Func = (void*)originalAddr;
//	sprintf(txt, "DisplayBuff addr: %X\r\n", originalAddr);
//	debuglog(txt);
	}else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceDisplaySetFrameBuf\r\n");
#endif
	}

	originalAddr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0x984C27E7);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceDisplayWaitVblankStart3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceDisplayWaitVblankStart_Func = (void*)originalAddr;
//	sprintf(txt, "DisplayBuff addr: %X\r\n", originalAddr);
//	debuglog(txt);
	}else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceDisplayWaitVblankStart\r\n");
#endif
	}

	originalAddr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0x36CDFADE);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceDisplayWaitVblank3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceDisplayWaitVblank_Func = (void*)originalAddr;
//	sprintf(txt, "DisplayBuff addr: %X\r\n", originalAddr);
//	debuglog(txt);
	}else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceDisplayWaitVblank\r\n");
#endif
	}


	originalAddr = sctrlHENFindFunction("sceGE_Manager", "sceGe_user", 0xE0D68148);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceGeListUpdateStallAddr3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceGeListUpdateStallAddr_Func = (void*)originalAddr;
	} else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceGeListUpdateStallAddr\r\n");
#endif
	}

	originalAddr = sctrlHENFindFunction("sceGE_Manager", "sceGe_user", 0xAB49E76A);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceGeListEnQueue3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceGeListEnQueue_Func = (void*)originalAddr;
	} else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceGeListEnQueue\r\n");
#endif
	}

	originalAddr = sctrlHENFindFunction("sceGE_Manager", "sceGe_user", 0x03444EB4);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceGeListSync3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceGeListSync_Func = (void*)originalAddr;
	} else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceGeListSync\r\n");
#endif
	}

	originalAddr = sctrlHENFindFunction("sceGE_Manager", "sceGe_user", 0xB287BD61);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceGeDrawSync3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceGeDrawSync_Func = (void*)originalAddr;
	} else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceGeDrawSync\r\n");
#endif
	}

	originalAddr = sctrlHENFindFunction("sceGE_Manager", "sceGe_user", 0xA4FC06A4);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceGeSetCallback3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceGeSetCallback_Func = (void*)originalAddr;
	} else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceGeSetCallback\r\n");
#endif
	}
/*	debuglog("hook gesavecontext\r\n");
	sceGeSaveContext_Func = sctrlHENFindFunction("sceGE_Manager", "sceGe_user", 0x438A385A);
	sctrlHENPatchSyscall(sceGeSaveContext_Func, sceGeSaveContext3D);
	sceKernelDcacheWritebackInvalidateAll();
	sceKernelIcacheInvalidateAll();
	debuglog("hook done\r\n");
	*/
/*	if (sceGeGetMtx_Func == NULL) {
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

*/


#ifdef DEBUG_MODE
	debuglog("End hooking display\r\n");
#endif
}

void hookDisplayOnly(void) {
	u32 originalAddr;
	originalAddr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", 0x289D82FE);
	if (originalAddr != NULL){
		sctrlHENPatchSyscall(originalAddr, sceDisplaySetFrameBuf3D);
		sceKernelDcacheWritebackInvalidateAll();
		sceKernelIcacheInvalidateAll();
		sceDisplaySetFrameBuf_Func = (void*)originalAddr;
	}else {
#ifdef ERROR_LOG
		debuglog("unable to hook sceDisplaySetFrameBuf\r\n");
#endif
	}
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
