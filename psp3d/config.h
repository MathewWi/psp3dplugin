/*
 * config.h
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

#ifndef CONFIG_H_
#define CONFIG_H_

#define MAX_COL_MODE 4

typedef struct {
	unsigned int clearScreen;
	unsigned int rotateIdentity;
	unsigned int needStage1;
	unsigned int activationBtn;
	unsigned int addViewMtx;
	unsigned int keepPixelmaskOrigin;
	unsigned int lateHook;
	unsigned int rotAllTime;
	unsigned int ignoreEnqueueCount;
	unsigned int flipFlop;
	unsigned int fixedFrameBuffer;
	float rotationDistance;
	float rotationAngle;
	char showStat;
	char colorMode;
	unsigned char colFlip;
	unsigned int placeHolder[20];
	//char rotationAxis;
}configData;

typedef struct colorMode{
	unsigned int color1;
	unsigned int color2;
}colorMode;

extern colorMode colorModes[MAX_COL_MODE*2];
extern int readConfigFile(const char* gameTitle);

extern unsigned char pluginMenuInit();
extern void pluginMenuDisplay();
extern unsigned char pluginMenuHansleButton(unsigned int button);
#endif /* CONFIG_H_ */
