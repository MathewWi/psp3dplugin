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
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "debug.h"

short isNumber(const char z){
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
	return -1;
}
unsigned int charToUi(const char* text){
	unsigned int temp = 0;
	unsigned short i;
	short num;
	for (i = 0;i <strlen(text);i++){
		num = isNumber(text[i]);
		if (num >= 0){
			temp*=10;
			temp+=num;
		} else {
			//once we got the first non number char we return the nbumber until now
			return temp;
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
	char txt[100];

	for (i = 0;i<strlen(text);i++){
		num = isNumber(text[i]);
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

int readConfig(configData* cfg, const char* gameTitle){
	int fd, bytes, ret;
#ifdef DEBUG_MODE
	char txt[150];
#endif
	char cfgLine[110];
	short sectionFound = 0;
	short feof = 0;
// set default values in case there are sections missing in the file or it could
// not be read proberbly
	cfg->rotationAxis = 'Y';
	cfg->rotationDistance = 7.0f;
	cfg->rotationAngle = 3.5f;
	cfg->clearScreen = 1;
	cfg->rotateIdentity = 1;
	cfg->activationBtn = 0x800000; //PSP_CTRL_NOTE

	fd = sceIoOpen("ms0:/seplugins/psp3d.cfg",PSP_O_RDONLY, 0777);
	if (fd >= 0){
		//read the next line of the file, until we have found the
		//right game section
		while (!sectionFound && !feof){
			if (readLine(fd, cfgLine, 100) == 0){
				feof = 1;
			} else {
				if (cfgLine[0] == '['){
					//this is the start character of the section, ] indicates
					//the end and in between we do have the name
					if (strncmp(&cfgLine[1], gameTitle, strlen(cfgLine)-2) == 0
						|| strncmp(&cfgLine[1], "DEFAULT", strlen(cfgLine)-2) == 0){
						sectionFound = 1;
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
				bytes = readLine(fd, cfgLine, 100); //should be the axis
				if (bytes == 0 || cfgLine[0] == '['){
					feof = 1;
				}else{
#ifdef DEBUG_MODE
					sprintf(txt, "Current Line: %.50s\r\n", cfgLine);
					debuglog(txt);
#endif

					if (strncmp(cfgLine, "ROT_AXIS=", 9) == 0)
						cfg->rotationAxis = cfgLine[9];
					if (strncmp(cfgLine, "ROT_POINT=", 10)==0)
						//sscanf(&cfgLine[10], "%f", &cfg->rotationDistance);
						cfg->rotationDistance = charToF(&cfgLine[10]);
						//cfg->rotationDistance = 8.0f;
					if (strncmp(cfgLine, "ROT_ANGLE=", 10)==0){
						//sscanf(&cfgLine[10], "%f", &cfg->rotationAngle);
						cfg->rotationAngle = charToF(&cfgLine[10]);
						cfg->rotationAngle = cfg->rotationAngle*GU_PI/180.0f;
					}
					if (strncmp(cfgLine, "ROT_CLEAR=", 10)==0)
						//sscanf(&cfgLine[10], "%d", &cfg->clearScreen);
						cfg->clearScreen = charToUi(&cfgLine[10]);
					if (strncmp(cfgLine, "ROT_IDENTITY=", 13)==0)
						//sscanf(&cfgLine[13], "%d", &cfg->rotateIdentity);
						cfg->rotateIdentity = charToUi(&cfgLine[13]);
					if (strncmp(cfgLine, "BTN_ACTIVATION=", 15) == 0)
						cfg->activationBtn = charToUi(&cfgLine[15]);
				}
			}
#ifdef DEBUG_MODE
			debuglog("config complete\r\n");
#endif
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

#ifdef DEBUG_MODE
		sprintf(txt, "Rotation:%c, Distance:%.3f, Angle(rad):%.3f, Clear:%d, Identity:%d, Activation: %X\r\n", cfg->rotationAxis, cfg->rotationDistance, cfg->rotationAngle, cfg->clearScreen, cfg->rotateIdentity, cfg->activationBtn);
		debuglog(txt);
#endif

	return 1;
}
