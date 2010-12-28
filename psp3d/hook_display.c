/*------------------------------------------------------------------------------*/
/* hook_display																	*/
/*------------------------------------------------------------------------------*/
#include <pspkernel.h>
#include "debug.h"
#include "hook.h"
#include <pspgu.h>
#include <pspgum.h>
#include <psptypes.h>
#include <malloc.h>
#include <stdio.h>
//trace mode does activate logging all GE-List entries
//this is to get an idea how the current game is using the same
//#define TRACE_MODE
//#define TRACE_VIEW_MODE
//debug mode does activate the logging of the function entries
//to get an idea which order the current game is calling them
//#define DEBUG_MODE
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
 * 1. setup an fill the GE list
 * 2. Enqueue -> set the stall adress to 0 which directly passes the current display list
 *    to the hardware
 * 3. Synch -> wait for this list to be processed by hardware
 */

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


static int numerek = 0;

unsigned int* MYlocal_list = 0;
unsigned int* nextStart_list = 0;
unsigned int* stall_list = 0;

unsigned int current_list_addres = 0;
unsigned int stall_addres = 0;
unsigned int adress_arr[200];
unsigned int baseOffset_arr[200];
unsigned int baseOffset = 0;
unsigned int frameBuff[2] = { 0, 0 }; //draw buffer
unsigned int frameBuffW[2] = { 0, 0 }; //draw buff height bits and buffer width

enum eRotationAxis{
	ERA_Y = 0,
    ERA_X = 1,
    ERA_Z = 2
} eRotationAxis;

int adress_number = 0;
int can_parse = 0;

//void * offScreenPtr = 0;
char state = 0;
char afterSync = 0;
//int viewMatrixKnown = 1;
char viewMatrixState = 0;
struct Vertex {
	u32 color;
	u16 x, y, z;
	u16 pad;
};
SceUID memid, memid2;
void *userMemory, *userMemory2;
char list1Ready = 0;
char list2Ready = 0;

#define BUFF_SIZE 128
//static unsigned int buffer[BUFF_SIZE];

#define ROTATE_LEFT (float)(GU_PI*-1.75f/180.0f)
#define ROTATE_RIGHT (float)(GU_PI*1.75f/180.0f)

static ScePspFMatrix4 view = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f,
		0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };

//pointer to the original values of the viewMatrix-Data within the command list
static unsigned int* cmdViewMatrix[12];
static unsigned int getFrameBuffCount = 0;

static void getFrameBuffFromList(unsigned int* list, unsigned int* frameB, unsigned int* frameW, void* stall){
	char parse = 1;
	char txt[100];
#ifdef DEBUG_MODE
		sprintf(txt, "getFramebuff List: %X Stall: %X\r\n", (unsigned int)list, (unsigned int)stall);
		debuglog(txt);
#endif
	while (parse == 1){
#ifdef TRACE_MODE
		sprintf(txt, "getFramebuff from List: %X - %X\r\n", list, (*list));
		debuglog(txt);
#endif
		if ((*list) >> 24 == 0x9C){
			*frameB = (*list);
			list++;
			*frameW = (*list);

			//parse = 0;
		}
		if ((*list) >> 24 == 12)
			parse = 0;
		if (stall != 0 && (unsigned int)list > (unsigned int)stall)
			parse = 0;

		list++;
	}
	getFrameBuffCount++;
}

/*
 * rotate the current view where the origin of the rotation
 * will be a fix point in front of the camera represented by the view
 * matrix
 */
static void Rotate3D(ScePspFMatrix4* view, float angle, float zDistance, short axis){
	char text[200];
	ScePspFMatrix4 inverse;
	//inverse the matrix to get the camera orientation in world space
	view->w.w = 1.0f;
	gumFastInverse(&inverse, view);
	ScePspFVector3 origin;

	origin.y = (inverse.w.y - inverse.z.y*zDistance);
	origin.z = (inverse.w.z - inverse.z.z*zDistance);
	origin.x = (inverse.w.x - inverse.z.x*zDistance);

#ifdef TRACE_VIEW_MODE
	sprintf(text, "View-X-Vector: %.3f|%.3f|%.3f\r\n", inverse.x.x, inverse.x.y, inverse.x.z);
	debuglog(text);
	sprintf(text, "View-Y-Vector: %.3f|%.3f|%.3f\r\n", inverse.y.x, inverse.y.y, inverse.y.z);
	debuglog(text);
	sprintf(text, "View-Z-Vector: %.3f|%.3f|%.3f\r\n", inverse.z.x, inverse.z.y, inverse.z.z);
	debuglog(text);
	sprintf(text, "View-Pos: %.3f|%.3f|%.3f\r\n", inverse.w.x, inverse.w.y, inverse.w.z);
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
	switch (axis){
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
	//move back the view to the initial place of the origin point
	origin.x = -origin.x;
	origin.y = -origin.y;
	origin.z = -origin.z;
	gumTranslate(view, &origin);

#ifdef TRACE_VIEW_MODE
	ScePspFMatrix4 inverseD2;
	gumFullInverse(&inverseD2, view);
	sprintf(text, "final View-Pos: %.3f|%.3f|%.3f\r\n", inverseD2.w.x, inverseD2.w.y, inverseD2.w.z);
	debuglog(text);
	sprintf(text, "View-X-Vector: %.3f|%.3f|%.3f\r\n", inverseD2.x.x, inverseD2.x.y, inverseD2.x.z);
	debuglog(text);
	sprintf(text, "View-Y-Vector: %.3f|%.3f|%.3f\r\n", inverseD2.y.x, inverseD2.y.y, inverseD2.y.z);
	debuglog(text);
	sprintf(text, "View-Z-Vector: %.3f|%.3f|%.3f\r\n", inverseD2.z.x, inverseD2.z.y, inverseD2.z.z);
	debuglog(text);

#endif

}

static int Render3D(unsigned int *currentList, short rotateLeft) {
	u8 jump_or_call = 0;
	short viewItem = 0;
	short frameBuffCount = 0;
	short viewMatrixCount = 0;
	short clearCount = 0;
	short manipulate = 0;

	int list_parsing = 1;
	unsigned int base = 0;
	unsigned int npc;

	baseOffset = 0;
	char texto[50];
	unsigned int *list;
	if (nextStart_list == 0)
		list = currentList;
	else
		list = nextStart_list;

	if (list == 0)
		return;

#ifdef DEBUG_MODE
	sprintf(texto,"Render3D called, state %d, left %d\r\n", state, rotateLeft );
	debuglog(texto);
#endif

	while (list_parsing == 1) {
		int command = 0;
		unsigned int argument = 0;
		int argif = 0;
		float fargument = 0.0f;

		//current addres
		current_list_addres = (unsigned int)&list;

		command = (*list) >> 24;
		argument = (*list) & 0xffffff;

		if (command != 12) {
#ifdef TRACE_MODE
			//dump the list contents while debuging:
			sprintf(texto,"Display List Entry %X:",(unsigned int)list);
			debuglog(texto);
#endif
			//searching the command list for the viewMatrix as
			//we would like to manipulate the same to allow 3D taking place
			switch (command) {

			case 0x08:
				//jump to a new display list address
				npc = ((base | argument) + baseOffset) & 0xFFFFFFFC;
				//jump to new address
				list = (unsigned int*) npc;
				jump_or_call = 1;
#ifdef TRACE_MODE
				sprintf(texto, "jump to %X\r\n", (unsigned int)list);
				debuglog(texto);
#endif
				break;
			case 0x09:
				//debuglog("conditional jump\r\n");
				//conditional jump - not implemented yet
				break;
			case 0x10:
				//base address
#ifdef TRACE_MODE
				sprintf(texto, "BASE\r\n");
				debuglog(texto);
#endif
				base = (argument << 8) & 0xff000000;
				break;
			case 0x13:
				//base offset
#ifdef TRACE_MODE
				sprintf(texto, "BASE-Offset\r\n");
				debuglog(texto);
#endif
				baseOffset = (argument << 8);
				break;
			case 0x14:
				//base origin
#ifdef TRACE_MODE
				sprintf(texto, "BASE Origin\r\n");
				debuglog(texto);
#endif
				baseOffset = list - 1;
				break;
			case 0x0a:
				// display list call, put the current address to a stack
				npc = ((base | argument) + baseOffset) & 0xFFFFFFFC;

				//save adres
				adress_arr[adress_number] = list;
				baseOffset_arr[adress_number] = baseOffset;
				adress_number++;

				list = (unsigned int*) npc;
				jump_or_call = 1;
#ifdef TRACE_MODE
				sprintf(texto, "call %X\r\n", (unsigned int)list);
				debuglog(texto);
#endif
				break;
			case 0x0b:
				// returning from call, retrieve last adress from stack
				adress_number--;
				list = (unsigned int*) adress_arr[adress_number];

				baseOffset = baseOffset_arr[adress_number];
#ifdef TRACE_MODE
				sprintf(texto, "return call\r\n");
				debuglog(texto);
#endif
				break;
			case 0x40:
#ifdef TRACE_MODE
				debuglog("Texture Matrix strobe\r\n");
#endif
				//will be the model matrix select
				break;
			case 0x41:
#ifdef TRACE_MODE
				debuglog("Texture Matrix data\r\n");
#endif
				//will be the model matrix select
				break;
			case 0x3a:
#ifdef TRACE_MODE
				debuglog("Model Matrix strobe\r\n");
#endif
				//will be the model matrix select
				break;
			case 0x3b:
#ifdef TRACE_MODE
				debuglog("Model Matrix data\r\n");
#endif
				//model matrix upload
				break;
			case 0x3e:
#ifdef TRACE_MODE
				debuglog("Projection Matrix strobe\r\n");
#endif
				//will be the model matrix select
				break;

			case 0x04:
#ifdef TRACE_MODE
				debuglog("draw primitive\r\n");
#endif
				break;
			case 0x3c:
				//view matrix strobe
#ifdef TRACE_MODE
				debuglog("view matrix strobe\r\n");
#endif
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
#ifdef TRACE_VIEW_MODE
					sprintf(texto, "ViewMatrix Item %d:%.4f\r\n", viewItem, fargument);
					debuglog(texto);
#endif
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
						if (rotateLeft == 1) {
							Rotate3D(&view, ROTATE_LEFT, 7.0f, ERA_Y);
						} else if (rotateLeft == 0) {
							Rotate3D(&view, ROTATE_RIGHT, 7.0f, ERA_Y);
						}

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
			case 0x15:
#ifdef TRACE_MODE
				sprintf(texto,"DrawRegion Start:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0x16:
#ifdef TRACE_MODE
				sprintf(texto,"DrawRegion End:%X\r\n", *list);
				debuglog(texto);
#endif
				break;

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
#ifdef TRACE_MODE
				sprintf(texto,"Framebuffer:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0x9d:
				//framebuffer width
#ifdef TRACE_MODE
				sprintf(texto,"Framewidth:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0x9e:
#ifdef TRACE_MODE
				sprintf(texto,"Depthbuffer:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0x9f:
#ifdef TRACE_MODE
				sprintf(texto,"DepthbufferWidth:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xd2:
#ifdef TRACE_MODE
				sprintf(texto,"Pixelformat:%X\r\n", *list);
				debuglog(texto);
#endif
				//set the pixelformat to NOP as we have set thet to pass only specific stuff (red/cyan) straight away
				//(*list) = 0x0;
				break;
			case 0xcf:
#ifdef TRACE_MODE
				sprintf(texto,"Fog-Color:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xcd:
#ifdef TRACE_MODE
				sprintf(texto,"Fog-End:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xce:
#ifdef TRACE_MODE
				sprintf(texto,"Fog-Range:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xdd:
#ifdef TRACE_MODE
				sprintf(texto,"Stencil-Op:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xdc:
#ifdef TRACE_MODE
				sprintf(texto,"StencilFunc:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xe6:
#ifdef TRACE_MODE
				sprintf(texto,"Logical-Op:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xe7:
#ifdef TRACE_MODE
				sprintf(texto,"DepthMask:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0x9b:
#ifdef TRACE_MODE
				sprintf(texto,"FrontFace?:%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xe8:
#ifdef TRACE_MODE
				sprintf(texto,"PixelMask(RGB):%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0xe9:
#ifdef TRACE_MODE
				sprintf(texto,"PixelMask(Alpha):%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			case 0x0e:
#ifdef TRACE_MODE
				sprintf(texto,"Signal Interrupt:%X\r\n", *list);
				debuglog(texto);
#endif
/*				//the signal as part of the display list does trigger something currently
				//not visible for me - I guess something like a call back or similar
				//this currently seem to bypass all stuff prepared here for 3D-Rendering
				//therefore we do switch off 3D rendering if encountered a signal in the
				//display list...
				draw3D = 9;
#ifdef ERROR_LOG
				debuglog("GE-Signal in display list. 3D mode switched off\r\n");
#endif
*/
				break;

			case 0xe2:
			case 0xe3:
			case 0xe4:
			case 0xe5:
#ifdef TRACE_MODE
				sprintf(texto,"DitherMatrix:%X\r\n", *list);
				debuglog(texto);
#endif
				break;

			case 0xd3:
				//clear flags
#ifdef TRACE_MODE
				sprintf(texto,"clear flags %X\r\n", *list);
				debuglog(texto);
#endif
				//clear seem to be done in the following way:
				//1. get some vertex memory from display list
				//2. set clear flags
				//3. draw something
				//4. reset clear flag
				//as we would like to prevent the list from clearing
				//we set some of the steps to be NOP ...
				if (manipulate == 1){
					(*list) &= ((unsigned int) (0xD3 << 24) | (((GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT) << 8) | 0x01)); //clear flag -> prevent color clear!
					/*list++;
					(*list) = 0x0; //vertex type -> NOP
					list++;
					(*list) = 0x0; //BASE -> NOP
					list++;
					(*list) = 0x0; //vertexlist -> NOP
					list++;
					(*list) = 0x0; //draw Prim->NOP
					list++; //this would point to the second clear flag setting,
							//however, this would be jumped over as we would increase later on anyway
					*/
				} //if manipulate == 1
				clearCount++;

				break;

			default:
#ifdef TRACE_MODE
				sprintf(texto, "%X\r\n", *list);
				debuglog(texto);
#endif
				break;
			}
		} else {
			if (command == 12 && can_parse == 1) {
				list_parsing = 0;
				can_parse = 2;
			} else {
				list_parsing = 0;
			}
		} //command != 12

		//do increase not if we just came out of a call/jump
		//other wise we would not get the first entry we just jumped to...
		if (jump_or_call == 0)
			list++;
		else
			jump_or_call = 0;

		if (stall_list && list >= stall_list) {
			list_parsing = 0;
		}

	} //while list_parsing
#ifdef DEBUG_MODE
	sprintf(texto, "ViewCount: %d, FrameBuffers: %d, ClearCount: %d\r\n", viewMatrixCount, frameBuffCount, clearCount);
	debuglog(texto);
#endif
	return viewMatrixCount;
}

int sceGeListUpdateStallAddr_fake(int qid, void *stall) {
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
	unsigned int* list;
	int ret;
	unsigned char i;
	if (draw3D == 2) {
		if (state == 1 && frameBuff[0] == 0) {
			list = MYlocal_list;
			//try to get the current frame buffer as we need to pass it in our own
			//display list as first entry
			//assuming the first entry will be the framebuffer setting
#ifdef DEBUG_MODE
			debuglog("UpdateStall: try getting buffer 1\r\n");
#endif
			getFrameBuffFromList(list, &frameBuff[0], &frameBuffW[0], stall);
		} else if (state == 2 && frameBuff[1] == 0) {
			list = MYlocal_list;
			//try to get the current frame buffer as we need to pass it in our own
			//display list as first entry
			//for what ever reason it could be that the state will be 2 and the framebuffer
			//seem to be the same passed, but we need to wait for a different one...
#ifdef DEBUG_MODE
			debuglog("UpdateStall: try getting buffer 2\r\n");
#endif
			getFrameBuffFromList(list, &frameBuff[1], &frameBuffW[1], stall);
			if (frameBuff[0] == frameBuff[1])
				frameBuff[1] = 0;
		}
		if (frameBuff[0] != 0 && frameBuff[1] != 0) {
#ifdef DEBUG_MODE
			sprintf(txt, "both framebuffer set, %X, %X\n", frameBuff[0],
					frameBuff[1]);
			debuglog(txt);
#endif
			draw3D = 3;
		}
		if (getFrameBuffCount > 100){
			//we have made more than 100 tries to get 2 different framebuffer
			//we give up and stop 3D mode	//not possible to get 2 different draw buffers, switch off 3D mode
			draw3D = 0;
		}
	}
	if (draw3D == 3) {
#ifdef DEBUG_MODE
		debuglog("Update Stall - draw3D 3\r\n");
#endif
		Render3D(MYlocal_list, state-1);
	}

	nextStart_list = (unsigned int*) stall;
	pspSdkSetK1(k1);
	ret = sceGeListUpdateStallAddr_Func(qid, stall);

	return ret;
}

static unsigned int* clearScreen(unsigned int* geList, int listId) {

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
	sceGeListUpdateStallAddr_Func(listId, local_list);

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
static unsigned int* prepareRender3D(unsigned int listId, unsigned int* list, short buffer, unsigned int pixelMask, short withClear){
	(*list) = (frameBuff[buffer]);
	list++;
	(*list) = (frameBuffW[buffer]);
	list++;

	if (withClear){
#ifdef DEBUG_MODE
		char txt[100];
		sprintf(txt, "clear screen on buffer %d:%X\r\n", buffer, frameBuff[buffer]);
		debuglog(txt);
#endif
		list = clearScreen(list, listId);
	}
	//set pixel mask - do not write the color provided (red/cyan)
	(*list) = (unsigned int) (0xE8 << 24) | (pixelMask);
	list++;
	(*list) = (unsigned int) (0xE9 << 24) | (0x000000);
	list++;

	//finish the ge list
	(*list) = 15 << 24;
	list++;
	(*list) = 12 << 24;
	list++;

	return list;
}

/*
 * this is where a new display list will be startet
 * as seen in retail games, the stall address seem to be set to 0 to
 * pass complete list directly to the hardware for processing. Otherwise the
 * stall address is set to be the same as the GE list starting address
 *
 * this is used if the sceGeListUpdateStallAddr which does set the pointer
 * within the same GE list step by step forward to pass the other bits to
 * the hardware
 */
int MYsceGeListEnQueue(const void *list, void *stall, int cbid, PspGeListArgs *arg) {
	int k1 = pspSdkSetK1(0);
	int listId;
	unsigned int* local_list_s;
	unsigned int* local_list;
#ifdef DEBUG_MODE
	char text[150];

	if (draw3D > 0) {
	//	printf("Enqueue Called: %X, Stall: %X\r\n", list, stall);
		sprintf(text, "Enqueue Called: %X, Stall: %X\r\n", list, stall);
		debuglog(text);
	}
#endif
	if (draw3D == 2){
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - draw3D 2\r\n");
#endif
		//if we do have the approach where the stall is set to 0 and
		//there seem no updateStallAddr to be used
		if (list != stall && stall == 0){
			//the display list will be directly thrown to the hardware
			//in the first place we try to find the display buffer
			if (frameBuff[0] == 0  && state == 1) {
				local_list = (unsigned int*)list;
				//try to get the current frame buffer as we need to pass it in our own
				//display list as first entry
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - try to get Framebuffer 1\r\n");
#endif
				getFrameBuffFromList(local_list, &frameBuff[0], &frameBuffW[0], 0);
#ifdef DEBUG_MODE
				sprintf(text,"sceGeListEnqueue - get Buffer 1 %X\r\n", frameBuff[0] );
				debuglog(text);
#endif
			} else if (frameBuff[1] == 0 && state == 2) {
					local_list = (unsigned int*)list;
					//try to get the current frame buffer as we need to pass it in our own
					//display list as first entry
#ifdef DEBUG_MODE
					debuglog("GeListEnqueue - try to get Framebuffer 1\r\n");
#endif
					getFrameBuffFromList(local_list, &frameBuff[1], &frameBuffW[1], 0);
					//we really should have 2 different frame buffer
#ifdef DEBUG_MODE
					sprintf(text,"sceGeListEnqueue - get Buffer 2 %X\r\n", frameBuff[1] );
					debuglog(text);
#endif
					if (frameBuff[0] == frameBuff[1])
						frameBuff[1] = 0;

				}
				if (frameBuff[0] != 0 && frameBuff[1] != 0) {
#ifdef DEBUG_MODE
					sprintf(text, "both framebuffer set, %X, %X\n", frameBuff[0], frameBuff[1]);
					debuglog(text);
#endif
					draw3D = 3;
				}
			}
	}
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
	} else if (draw3D == 3) {
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - draw3D 3\r\n");
#endif

		//enqueue mean starting new list...we would like to add some initial settings
		//first

		//in case Enqueue is called more than once while one draw pass is executed
		//we check for the flag drawSync which is set once the last draw has finished
		if (state == 1 && afterSync == 1) {

			local_list_s = (unsigned int*) (((unsigned int) userMemory)
					| 0x40000000);//(((unsigned int)list;//gList)| 0x40000000);
			listId = sceGeListEnQueue_Func(local_list_s, local_list_s, cbid, arg);
			//prepare 3d-render: clear screen and set pixel mask
			local_list_s = prepareRender3D(listId, local_list_s, 0, 0x0000ff, 1);
			sceGeListUpdateStallAddr_Func(listId, local_list_s);
		} else if (state == 2 && afterSync == 1) {
			local_list_s = (unsigned int*) (((unsigned int) userMemory2)
					| 0x40000000);//list;//(unsigned int*)(((unsigned int)gList)| 0x40000000);
			listId = sceGeListEnQueue_Func(local_list_s, local_list_s, cbid, arg);
			//prepare 3d-render:clear screen and set pixel mask
			//if the display list is not passed at once we flip pixel filter each frame
			//otherwise we could render red/cyan overlayed in one frame
			if (stall == 0)
				local_list_s = prepareRender3D(listId, local_list_s, 1, 0x0000ff, 1);
			else
				local_list_s = prepareRender3D(listId, local_list_s, 1, 0xffff00, 1);
			sceGeListUpdateStallAddr_Func(listId, local_list_s);
		}

		//now manipulate the GE list to change the view-Matrix
		if (stall == 0 && afterSync == 1){
			numerek++;
			//if we would pass the list directly to the hardware we do manipulate the one first
			//rotate view left
			if (Render3D((unsigned int*)list, 1) <= 0){
#ifdef DEBUG_MODE
				debuglog("add view matrix using own display list\r\n");
#endif
				//if there was no viewMatrix part of the displayList we need to pass the viewMatrix
				//with our own display list
				gumLoadIdentity(&view);
				Rotate3D(&view, ROTATE_LEFT, 7.0f, ERA_Y);
				(*local_list_s) = (unsigned int) (0x3c << 24);
				local_list_s++;

				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.x.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.x.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.x.z)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.y.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.y.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.y.z)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.z.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.z.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.z.z)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.w.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.w.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.w.z)) >> 8) & 0xffffff);
				local_list_s++;

				sceGeListUpdateStallAddr_Func(listId, local_list_s);
			}

			//try to render the second frame at the same time to overlay the
			//current draw
			//pass current list to hardware and wait until it was processed
			listId = sceGeListEnQueue_Func(list, 0, cbid, arg);
			sceGeListSync_Func(listId, 0);

			if (state == 1){
				local_list_s = (unsigned int*) (((unsigned int) userMemory)
						| 0x40000000);
				listId = sceGeListEnQueue_Func(local_list_s, local_list_s, cbid, arg);

				local_list_s = prepareRender3D(listId, local_list_s, 0, 0xffff00, 0);
				sceGeListUpdateStallAddr_Func(listId, local_list_s);

			} else if (state == 2){
				local_list_s = (unsigned int*) (((unsigned int) userMemory2)
						| 0x40000000);
				listId = sceGeListEnQueue_Func(local_list_s, local_list_s, cbid, arg);

				local_list_s = prepareRender3D(listId, local_list_s, 1, 0xffff00, 0);
				sceGeListUpdateStallAddr_Func(listId, local_list_s);

			}

			//rotate View right
			if (Render3D((unsigned int*)list, 0) <= 0){
#ifdef DEBUG_MODE
				debuglog("add view matrix using own display list\r\n");
#endif
				//if there was no viewMatrix part of the displayList we need to pass the viewMatrix
				//with our own display list
				gumLoadIdentity(&view);
				Rotate3D(&view, ROTATE_RIGHT, 7.0f, ERA_Y);
				(*local_list_s) = (unsigned int) (0x3c << 24);
				local_list_s++;

				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.x.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.x.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.x.z)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.y.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.y.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.y.z)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.z.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.z.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.z.z)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.w.x)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.w.y)) >> 8) & 0xffffff);
				local_list_s++;
				(*local_list_s) = (unsigned int) (0x3d << 24) | (((*((u32*) &view.w.z)) >> 8) & 0xffffff);
				local_list_s++;

				sceGeListUpdateStallAddr_Func(listId, local_list_s);
			}
			//sceKernelDcacheWritebackInvalidateAll();
		}
		afterSync = 0;
	} else if (draw3D == 9) {
		//stop 3D mode requested...set back the pixel mask
#ifdef DEBUG_MODE
		debuglog("GeListEnqueue - draw3D 9\r\n");
#endif
		local_list_s = (unsigned int*) (((unsigned int) userMemory)	| 0x40000000);
		listId = sceGeListEnQueue_Func(local_list_s, local_list_s, cbid, arg);

		local_list_s = prepareRender3D(listId, local_list_s, 1, 0x000000, 0);
		sceGeListUpdateStallAddr_Func(listId, local_list_s);

		draw3D = 0;
	}

	MYlocal_list = (unsigned int*) list;
	//set addres to some other int variable
	current_list_addres = (unsigned int) &list;
	//
	stall_addres = (unsigned int) &stall;
	stall_list = 0;

	pspSdkSetK1(k1);
	int ret = sceGeListEnQueue_Func(list, stall, cbid, arg);
	return (ret);
}

static int MYsceGeListEnQueueHead(const void *list, void *stall, int cbid,
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

static int MYsceGeListSync(int qid, int syncType) {
	//psp is reseting all video commands right here
#ifdef DEBUG_MODE
	debuglog("GeListSync\r\n");
#endif
	//we do wait until the list is synchronized
	//reset the next start...
	nextStart_list = 0;
	int ret = sceGeListSync_Func(qid, syncType);
	return (ret);
}

int MYsceGeDrawSync(int syncType) {
	//we do wait until drawing complete and starting a new displaylist
	//reset the next Start address
	int k1 = pspSdkSetK1(0);
#ifdef DEBUG_MODE
	char text[50];

	if (draw3D > 0) {
		sprintf(text, "Draw Synch Called: 3D= %d\r\n", draw3D);
		debuglog(text);
	}
#endif

	nextStart_list = 0;
	//after each render cycle switch the state
	if (state == 1){
		state = 2;
	}else{
		state = 1;
	}

	afterSync = 1;

	pspSdkSetK1(k1);
	int ret = sceGeDrawSync_Func(syncType);

	return ret;
}
/*------------------------------------------------------------------------------*/
/* hookDisplay																	*/
/*------------------------------------------------------------------------------*/
void hookDisplay(void) {
	char txt[50];
#ifdef DEBUG_MODE
	debuglog("Start hooking display\n");
#endif
	//dynamic hook testing
	int result = 0;

#ifdef DEBUG_MODE
	debuglog("get user memory\n");
#endif
	memid = sceKernelAllocPartitionMemory(2, "myGElist1", 0,
			sizeof(unsigned int) * BUFF_SIZE, NULL);
	userMemory = sceKernelGetBlockHeadAddr(memid);

	memid2 = sceKernelAllocPartitionMemory(2, "myGElist2", 0,
				sizeof(unsigned int) * BUFF_SIZE, NULL);
	userMemory2 = sceKernelGetBlockHeadAddr(memid2);

	//hook GE modules
	//sceGeListEnQueue_Func = ApiHookByNid("sceGE_Manager", "sceGe_user", 0xAB49E76A, MYsceGeListEnQueue);
	//sceGeDrawSync_Func = ApiHookByNid("sceGE_Manager", "sceGe_user", 0xB287BD61, MYsceGeDrawSync);


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
		if (hook_addr != NULL)
			HookFuncSetting(hook_addr, MYsceGeListEnQueue);
		else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListEnQueue\r\n");
#endif
		}
	}

	if (sceGeListEnQueueHead_Func == NULL) {
		sceGeListEnQueueHead_Func = HookNidAddress(module2, "sceGe_user",
				0x1C0D95A6);
		void *hook_addr = HookSyscallAddress(sceGeListEnQueueHead_Func);
		if (hook_addr != NULL)
			HookFuncSetting(hook_addr, MYsceGeListEnQueueHead);
		else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListEnQueueHead\r\n");
#endif
		}
	}

	if (sceGeListDeQueue_Func == NULL) {
		sceGeListDeQueue_Func = HookNidAddress(module2, "sceGe_user",
				0x5FB86AB0);
		void *hook_addr = HookSyscallAddress(sceGeListDeQueue_Func);
		if (hook_addr != NULL)
				HookFuncSetting(hook_addr, MYsceGeListDeQueue);
		else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListDeQueue\r\n");
#endif
		}
	}

	if (sceGeListSync_Func == NULL) {
		sceGeListSync_Func = HookNidAddress(module2, "sceGe_user", 0x03444EB4);
		void *hook_addr = HookSyscallAddress(sceGeListSync_Func);
		if (hook_addr != NULL)
			HookFuncSetting(hook_addr, MYsceGeListSync);
		else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListSync\r\n");
#endif
		}
}

	//another test
	if (sceGeListUpdateStallAddr_Func == NULL) {
		sceGeListUpdateStallAddr_Func = HookNidAddress(module2, "sceGe_user",
				0xE0D68148);
		void *hook_addr = HookSyscallAddress(sceGeListUpdateStallAddr_Func);
		if (hook_addr != NULL)
			HookFuncSetting(hook_addr, sceGeListUpdateStallAddr_fake);
		else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeListUpdateStall\r\n");
#endif
		}
}
	if (sceGeDrawSync_Func == NULL) {
		sceGeDrawSync_Func = HookNidAddress(module2, "sceGe_user", 0xB287BD61);
		void *hook_addr = HookSyscallAddress(sceGeDrawSync_Func);
		if (hook_addr != NULL)
			HookFuncSetting(hook_addr, MYsceGeDrawSync);
		else {
#ifdef ERROR_LOG
			debuglog("unable to hook sceGeDrawSync\r\n");
#endif
		}
}
#ifdef DEBUG_MODE
	debuglog("End hooking display\n");
#endif
}

void unhook_display(void) {
#ifdef DEBUG_MODE
	debuglog("Start unhooking display\n");
#endif
	sceKernelFreePartitionMemory(memid);
	sceKernelFreePartitionMemory(memid2);

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
