/*
 * config.c
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
#include <pspiofilemgr.h>
#include <pspgu.h>
#include <pspctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "debug.h"

extern char draw3D;
extern char gametitle[256];

configData currentConfig;                // bbggrr    bbggrr
colorMode colorModes[MAX_COL_MODE*2] = { {0x0000ff, 0xffff00},
										 {0x00ff00, 0xff00ff},
										 {0x00ffff, 0xff0000},
										 {0xff00ff, 0xffff00},

										 {0xffff00, 0x0000ff},
										 {0xff00ff, 0x00ff00},
										 {0xff0000, 0x00ffff},
										 {0xffff00, 0xff00ff}};

short isNumber(const char z, short base){
	if (z == '0') return 0;
	if (z == '1') return 1;
	if (z == '2') return 2;
	if (z == '3') return 3;
	if (z == '4') return 4;
	if (z == '5') return 5;
	if (z == '6') return 6;
	if (z == '7') return 7;
	if (z == '8') return 8;
	if (z == '9') return 9;
	if (base == 16){
		if (z == 'a' || z == 'A') return 10;
		if (z == 'b' || z == 'B') return 11;
		if (z == 'c' || z == 'C') return 12;
		if (z == 'd' || z == 'D') return 13;
		if (z == 'e' || z == 'E') return 14;
		if (z == 'f' || z == 'F') return 15;
	}
	return -1;
}
unsigned int charToUi(const char* text){
	unsigned int temp = 0;
	unsigned int base = 10;
	unsigned short i;
	short num;
	for (i = 0;i <strlen(text);i++){
		if (text[i] == 'x' || text[i] == 'X'){
			temp = 0;
			base = 16;
		} else {
			num = isNumber(text[i], base);
			if (num >= 0){
				temp*=base;
				temp+=num;
			} else {
				//once we got the first non number char we return the number until now
				return temp;
			}
		}
	}
	return temp;
}

float charToF(const char* text){
	float temp = 0;
	float temp2 = 0;
	unsigned int expo = 0;
	short num;
	unsigned short i;

	for (i = 0;i<strlen(text);i++){
		num = isNumber(text[i], 10);
		//numbers before the decimal char
		if (num >= 0){
			if (expo == 0){
				temp*=10;
				temp+=num;
			} else {
				temp2 = num;
				temp2/=expo;
				temp+=temp2;
				expo*=10;
			}
		} else {
			//decimal sign set expo to 10 first
			if (text[i] == '.' && expo == 0){
				expo = 10;
			}
			else
				//no further acceptable char, return number till now
				return temp;
		}
	}

	return temp;
}

unsigned int readLine(int fd, char* buffer, short width){
	unsigned int byte = 0;
	short endl = 0;
	while (!endl){
		if (sceIoRead(fd, &buffer[byte], 1) <= 0) return 0;
		if (buffer[byte] == '\r' ||
			buffer[byte] == '\n'){
			//if the line end char was \r we assume it is followed by \n
			//read that dummy byte
			if (buffer[byte] == '\r')
				sceIoRead(fd, &buffer[byte], 1);
			endl = 1;
			buffer[byte] = '\0';
		}
		byte++;
		if (byte >= width){
			buffer[byte] = '\0';
			endl = 1;
		}
	}
	return byte;
}

unsigned char loadCustomConfig();

int readConfigFile(const char* gameTitle){
	int fd, bytes, ret;
#ifdef DEBUG_MODE
	char txt[150];
#endif
	char cfgLine[60];
	short sectionFound = 0;
	short defaultFound = 0;
	short feof = 0;
	//check for cutom config being present...only load deliverd if nothing exists...
	if (loadCustomConfig()) return 1;
// set default values in case there are sections missing in the file or it could
// not be read proberbly
	currentConfig.rotationDistance = 0.0f;//9.0f;
	currentConfig.rotationAngle = 0.75f*GU_PI/180.0f;
	currentConfig.clearScreen = 1;
	currentConfig.rotateIdentity = 1;
	currentConfig.needStage1 = 1;
	currentConfig.addViewMtx = 0;
	currentConfig.keepPixelmaskOrigin = 0;
	currentConfig.lateHook = 1;
	currentConfig.rotAllTime = 0;
	currentConfig.ignoreEnqueueCount = 1;
	currentConfig.activationBtn = 0x800000; //PSP_CTRL_NOTE
	currentConfig.colorMode = 0;
	currentConfig.showStat = 0;
	currentConfig.colFlip = 0;
	currentConfig.flipFlop = 0;
	currentConfig.fixedFrameBuffer = 0;

	fd = sceIoOpen("ms0:/seplugins/psp3d.cfg",PSP_O_RDONLY, 0777);
	//for PSPGo Support check if ms0 failed for ef0...
	if (fd < 0)
		fd = sceIoOpen("ef0:/seplugins/psp3d.cfg",PSP_O_RDONLY, 0777);

	//fd = -1;
	int i;
	if (fd >= 0){

		//get the section at least 3 times
		//first: default
		//second: game specific
		for (i=0;i<2;i++){
			//read the next line of the file, until we have found the
			//right game section
			while (!sectionFound && !feof){
				if (readLine(fd, cfgLine, 50) == 0){
					feof = 1;
				} else {
					if (cfgLine[0] == '['){
						//this is the start character of the section, ] indicates
						//the end and in between we do have the name
						if (strncmp(&cfgLine[1], gameTitle, strlen(cfgLine)-2) == 0) {
							sectionFound = 1;
							defaultFound = 0;
						} else if (strncmp(&cfgLine[1], "DEFAULT", strlen(cfgLine)-2) == 0){
							sectionFound = 1;
							defaultFound = 1;
						}
					}
				}
			}
			// we know the section the config data is stored
			if (sectionFound){
	#ifdef DEBUG_MODE
				sprintf(txt, "SectionFound: %.30s\r\n", cfgLine);
				debuglog(txt);
	#endif
				//extract the necessary data
				while (!feof){
					bytes = readLine(fd, cfgLine, 50);
					if (bytes == 0 || cfgLine[0] == '['){
						feof = 1;
					}else{
			#ifdef DEBUG_MODE
						sprintf(txt, "Current Line: %.50s\r\n", cfgLine);
						debuglog(txt);
			#endif

						//if (strncmp(cfgLine, "ROT_AXIS=", 9) == 0)
							//currentConfig.rotationAxis = cfgLine[9];
						if (strncmp(cfgLine, "ROT_POINT=", 10)==0)
							//sscanf(&cfgLine[10], "%f", &currentConfig.rotationDistance);
							currentConfig.rotationDistance = charToF(&cfgLine[10]);
							//currentConfig.rotationDistance = 8.0f;
						if (strncmp(cfgLine, "ROT_ANGLE=", 10)==0){
							//sscanf(&cfgLine[10], "%f", &currentConfig.rotationAngle);
							currentConfig.rotationAngle = charToF(&cfgLine[10]);
							currentConfig.rotationAngle = currentConfig.rotationAngle*GU_PI/180.0f;
						}
						if (strncmp(cfgLine, "ROT_CLEAR=", 10)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.clearScreen = charToUi(&cfgLine[10]);

						if (strncmp(cfgLine, "PIXELMASK=", 10)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.keepPixelmaskOrigin = charToUi(&cfgLine[10]);

						if (strncmp(cfgLine, "STAGE1=", 7)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.needStage1 = charToUi(&cfgLine[7]);

						if (strncmp(cfgLine, "LATEHOOK=", 9)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.lateHook = charToUi(&cfgLine[9]);

						if (strncmp(cfgLine, "ROT_ALL=", 8)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.rotAllTime = charToUi(&cfgLine[8]);

						if (strncmp(cfgLine, "ROT_IDENTITY=", 13)==0)
							//sscanf(&cfgLine[13], "%d", &currentConfig.rotateIdentity);
							currentConfig.rotateIdentity = charToUi(&cfgLine[13]);

						if (strncmp(cfgLine, "ENQ_ICOUNT=", 11)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.ignoreEnqueueCount = charToUi(&cfgLine[11]);

						if (strncmp(cfgLine, "3D_FLIPFLOP=", 12)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.flipFlop = charToUi(&cfgLine[12]);

						if (strncmp(cfgLine, "FIXED_FB=", 9)==0)
							//sscanf(&cfgLine[10], "%d", &currentConfig.clearScreen);
							currentConfig.fixedFrameBuffer = charToUi(&cfgLine[9]);

						if (strncmp(cfgLine, "BTN_ACTIVATION=", 15) == 0)
							currentConfig.activationBtn = charToUi(&cfgLine[15]);
						if (strncmp(cfgLine, "COLOR_MODE=", 11) == 0){
							switch (cfgLine[11]){
							case 'R':
								currentConfig.colorMode = 0;
								break;
							case 'G':
								currentConfig.colorMode = 1;
								break;
							case 'Y':
								currentConfig.colorMode = 2;
								break;
							}
						}

					}
				}
	#ifdef DEBUG_MODE
				debuglog("config complete\r\n");
	#endif
			}
			if (feof && bytes == 0){
#ifdef DEBUG_MODE
			debuglog("file end reached\r\n");
#endif
				//we've already come to file end...leave..
				i = 2;
			} else if (defaultFound == 1){
#ifdef DEBUG_MODE
			debuglog("default section found...try 2nd time game\r\n");
#endif
				//default section found, continue reading
				feof = 0;
				sectionFound = 0;
				defaultFound = 0;
			} else if (sectionFound == 1) {
				//real section found...leave...
#ifdef DEBUG_MODE
			debuglog("game specific - all ready..\r\n");
#endif
				i = 2;
			}
		}

		ret = sceIoClose(fd);
#ifdef DEBUG_MODE
		debuglog("config file closed\r\n");
#endif

	} else {
#ifdef DEBUG_MODE
		debuglog("unable to read config\r\n");
#endif
	}

/*#ifdef DEBUG_MODE
//		char txt[150];
		sprintf(txt, "Rot-Distance:%.3f, Angle(rad):%.3f, Clear:%d, Identity:%d, Activation: %X\r\n", currentConfig.rotationDistance, currentConfig.rotationAngle, currentConfig.clearScreen, currentConfig.rotateIdentity, currentConfig.activationBtn);
		debuglog(txt);
#endif
*/
	return 1;
}

unsigned char loadCustomConfig(){
	int fd, bytes, ret;
	char cfgLine[90];
	short sectionFound = 0;
	short defaultFound = 0;
	short feof = 0;

	fd = sceIoOpen("ms0:/seplugins/psp3d_c.cfg", PSP_O_RDONLY, 0777);
	//for PSPGo Support check if ms0 failed for ef0...
	if (fd < 0)
		fd = sceIoOpen("ef0:/seplugins/psp3d_c.cfg", PSP_O_RDONLY, 0777);

	if (fd >= 0){
		sceIoLseek(fd, 0, PSP_SEEK_SET);
// file could be access try to find an existing game section
		while (!sectionFound && !feof){
			if (readLine(fd, cfgLine, 90) == 0){
				feof = 1;
			} else {
				if (cfgLine[0] == '['){
					//this is the start character of the section, ] indicates
					//the end and in between we do have the name
					if (strncmp(&cfgLine[1], gametitle, strlen(cfgLine)-2) == 0) {
						sectionFound = 1;
					}
				}
			}
		}
		if (sectionFound){
			sceIoRead(fd, &currentConfig, sizeof(currentConfig));
		}
		sceIoClose(fd);
	}
	if (sectionFound) return 1;
	else return 0;
}
/*
 * store current configuration to the file
 */
void saveCustomConfig(){
	int fd, bytes, ret;
	char cfgLine[90];
	short sectionFound = 0;
	short defaultFound = 0;
	short feof = 0;

	fd = sceIoOpen("ms0:/seplugins/psp3d_c.cfg",PSP_O_APPEND | PSP_O_CREAT | PSP_O_RDWR, 0777);
	//for PSPGo Support check if ms0 failed for ef0...
	if (fd < 0)
		fd = sceIoOpen("ef0:/seplugins/psp3d_c.cfg",PSP_O_APPEND | PSP_O_CREAT | PSP_O_RDWR, 0777);

	if (fd >= 0){
		sceIoLseek(fd, 0, PSP_SEEK_SET);
// file could be access try to find an existing game section
		while (!sectionFound && !feof){
			if (readLine(fd, cfgLine, 90) == 0){
				feof = 1;
			} else {
				if (cfgLine[0] == '['){
					//this is the start character of the section, ] indicates
					//the end and in between we do have the name
					if (strncmp(&cfgLine[1], gametitle, strlen(cfgLine)-2) == 0) {
						sectionFound = 1;
					}
				}
			}
		}
		if (!sectionFound){
			//no current game section in the file, create one
			sceIoLseek(fd, 0, PSP_SEEK_END);
			cfgLine[0] = '[';
			strcpy(&cfgLine[1], gametitle);
			strncpy(&cfgLine[strlen(gametitle)+1], "]\r\n\0", 4);
			sceIoWrite(fd, cfgLine, strlen(cfgLine));
			sceIoWrite(fd, &currentConfig, sizeof(currentConfig));
			sceIoWrite(fd, "\r\n", 2);
		}
		sceIoClose(fd);
	}
}

SceUID threadList[128];
SceUID self;
int    threadCount;
short menuX = 150;

unsigned char pauseGame(){
#ifdef DEBUG_MODE
	debuglog("Pause the game for Menu\r\n");
#endif
//suspend all threads appart from myself
	self = sceKernelGetThreadId();
	if (sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread, threadList, 128, &threadCount) < 0)
		return 0;

	int i;
	for (i=0;i<threadCount;i++){
		if (threadList[i] != self)
			sceKernelSuspendThread(threadList[i]);
	}
	return 1;
}

void continueGame(){
#ifdef DEBUG_MODE
	debuglog("Resume the game from Menu\r\n");
#endif
//continue all threads to run...
	int i;
	for (i=0;i<threadCount;i++){
		if (threadList[i] != self)
			sceKernelResumeThread(threadList[i]);
	}
}
/*
 * pluginMenu to interactive change the configuration settings
 */
unsigned char pluginMenuInit(){
#ifdef DEBUG_MODE
	debuglog("Init Plugin Menu\r\n");
#endif
	if (pauseGame()){
		blit_setup( );
		blit_set_color(0x00ffffff, 0xff000000);
		char i;
		for (i=0;i<14;i++){
			blit_string(menuX-10,90+i*8, "                             ");
		}
		return 1;
	}
	return 0;
}

short menuSelected = 0;
void pluginMenuDisplay(){
	//display the current Menu settings
	char txt[100];

	blit_setup( );
	blit_set_color(0x00ffffff, 0xff000000);
	blit_string(menuX,100, "PSP 3D-Plugin Menu");
	if (menuSelected == 0)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	if (draw3D == 3 || draw3D == 1){
		blit_string(menuX,120, "  3D activated          ");
	} else {
		blit_string(menuX,120, "  3D de-activated       ");
	}

	if (menuSelected == 1)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	switch (currentConfig.colorMode){
	case 0:
		sprintf(txt, "  Color mode: red/cyan    ");
		break;
	case 1:
		sprintf(txt, "  Color mode: green/pink  ");
		break;
	case 2:
		sprintf(txt, "  Color mode: yellow/blue ");
		break;
	case 3:
		sprintf(txt, "  Color mode: red/green   ");
		break;
	case 4:
		sprintf(txt, "  Color mode: cyan/red    ");
		break;
	case 5:
		sprintf(txt, "  Color mode: pink/green  ");
		break;
	case 6:
		sprintf(txt, "  Color mode: blue/yellow ");
		break;
	case 7:
		sprintf(txt, "  Color mode: green/red   ");
		break;
	default:
		sprintf(txt, "                          ");
		break;
	}
	blit_string(menuX,130, txt);

	if (menuSelected == 2)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	float angle = currentConfig.rotationAngle*180.0f/GU_PI;
	char ah = (char)angle;
	short al = angle*1000 - (short)ah*1000;
	sprintf(txt, "  3d angle: %d.%.3d      ", ah, al);
	blit_string(menuX,140, txt);

	if (menuSelected == 3)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	char ph = (char)currentConfig.rotationDistance;
	short pl = currentConfig.rotationDistance*1000 - (short)ph*1000;
	sprintf(txt, "  3d position: %d.%.3d     ", ph, pl);
	blit_string(menuX,150, txt);

	if (menuSelected == 4)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	if (currentConfig.colFlip)
		blit_string(menuX,160, "  Anaglyph colors flipped ");
	else
		blit_string(menuX,160, "  Anaglyph colors normal  ");

	if (menuSelected == 5)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	blit_string(menuX,180, "  Save Settings     ");

	if (menuSelected == 6)
		blit_set_color(0x00ffffff, 0xffff0000);
	else
		blit_set_color(0x00ffffff, 0xff000000);
	blit_string(menuX,190, "  Load Settings     ");
}

unsigned char pluginMenuHandleButton(unsigned int button){
	if (button & PSP_CTRL_START){
		continueGame();
		return 0;
	}
	if (button & PSP_CTRL_DOWN){
		menuSelected++;
		if (menuSelected > 6)
			menuSelected = 0;
	}
	if (button & PSP_CTRL_UP){
		menuSelected--;
		if (menuSelected < 0)
			menuSelected = 6;
	}
	if (button & PSP_CTRL_CROSS){
		switch (menuSelected){
		case 5:
			continueGame();
			saveCustomConfig();
			pluginMenuInit();
			break;
		case 6:
			continueGame();
			loadCustomConfig();
			pluginMenuInit();
			break;
		}
	}
	if (button & PSP_CTRL_RIGHT){
		switch (menuSelected){
		case 0:
			if (draw3D == 3 || draw3D == 1)
				draw3D = 9;
			else
				draw3D = 1;
			break;
		case 1:
			currentConfig.colorMode++;
			if (currentConfig.colFlip == 0){
				if (currentConfig.colorMode >= MAX_COL_MODE)
					currentConfig.colorMode = 0;
			} else {
				if (currentConfig.colorMode >= MAX_COL_MODE*2)
					currentConfig.colorMode = MAX_COL_MODE;
			}
			break;
		case 2:
			currentConfig.rotationAngle += 0.25f*GU_PI/180.0f;
			break;
		case 3:
			currentConfig.rotationDistance += 1.0f;
			break;
		case 4:
			if (currentConfig.colFlip){
				currentConfig.colFlip = 0;
				currentConfig.colorMode -= MAX_COL_MODE;
			}else{
				currentConfig.colFlip = 1;
				currentConfig.colorMode += MAX_COL_MODE;
			}
		}
	}

	if (button & PSP_CTRL_LEFT){
		switch (menuSelected){
		case 0:
			if (draw3D == 3 || draw3D == 1)
				draw3D = 9;
			else
				draw3D = 1;
			break;
		case 1:
			currentConfig.colorMode--;
			if (currentConfig.colFlip == 0){
				if (currentConfig.colorMode < 0 )
					currentConfig.colorMode = MAX_COL_MODE-1;
			} else {
				if (currentConfig.colorMode < MAX_COL_MODE)
					currentConfig.colorMode = (MAX_COL_MODE*2)-1;
			}
			break;
		case 2:
			currentConfig.rotationAngle -= 0.25f*GU_PI/180.0f;
			break;
		case 3:
			currentConfig.rotationDistance -= 1.0f;
			break;
		case 4:
			if (currentConfig.colFlip){
				currentConfig.colFlip = 0;
				currentConfig.colorMode -= MAX_COL_MODE;
			}else{
				currentConfig.colFlip = 1;
				currentConfig.colorMode += MAX_COL_MODE;
			}
		}
	}
	return 1;
}
