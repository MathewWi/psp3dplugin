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
#include "config.h"
#include "debug.h"

int readLine(int fd, char* buffer, short width){

	short byte = 0;
	char endl = 0;
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
	int fd;
#ifdef DEBUG_MODE
	char txt[100];
#endif
	char cfgLine[100];
	char* cfgEnd;
	short sectionFound = 0;
	short feof = 0;
	fd = sceIoOpen("ms0:/seplugins/psp3d.cfg",PSP_O_RDONLY, 0777);
	if (fd >= 0){
		//read the next line of the file, until we have found the
		//right game section
		while (!sectionFound && !feof){
			if (readLine(fd, cfgLine, 100) <= 0){
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
			sprintf(txt, "SectionFound: %.90s\r\n", cfgLine);
			debuglog(txt);
#endif
			//extract the necessary data
			readLine(fd, cfgLine, 100); //should be the axis
#ifdef DEBUG_MODE
			sprintf(txt, "next line: %.90s\r\n", cfgLine);
			debuglog(txt);
#endif
			if (strncmp(cfgLine, "ROT_AXIS=", 9) == 0){
				cfg->rotationAxis = cfgLine[9];
			}
			readLine(fd, cfgLine, 100); //should be the distance
#ifdef DEBUG_MODE
			sprintf(txt, "next line: %.90s\r\n", cfgLine);
			debuglog(txt);
#endif
			if (strncmp(cfgLine, "ROT_POINT=", 10)==0){
				sscanf(&cfgLine[10], "%f", &cfg->rotationDistance);
			}
			readLine(fd, cfgLine, 100); //should be the clear flag
#ifdef DEBUG_MODE
			sprintf(txt, "next line: %.90s\r\n", cfgLine);
			debuglog(txt);
#endif
			if (strncmp(cfgLine, "ROT_CLEAR=", 10)==0){
				sscanf(&cfgLine[10], "%i", &cfg->clearScreen);
			}
#ifdef DEBUG_MODE
			sprintf(txt, "Rotation:%c, Distance:%f, Clear:%i\r\n", cfg->rotationAxis, cfg->rotationDistance, cfg->clearScreen);
			debuglog(txt);
#endif
		}

		sceIoClose(fd);
	} else {
#ifdef DEBUG_MODE
		debuglog("unable to read config\r\n");
#endif
		cfg->rotationAxis = 'Y';
		cfg->rotationDistance = 7.0f;
	}
	return 1;
}
