/*
 * gameinfo.c
 *
  *
 * Copyright (C) 2010 André Borrmann/ C4TurD4Y
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
#include <pspinit.h>
#include <pspsdk.h>
#include <pspumd.h>
#include <stdio.h>
#include "debug.h"

typedef struct
{
    u32 version;       //Should be 0x01010000
    u32 keyTable;      //Offset of Key Table
    u32 valueTable;    //Offset of Value Table
    u32 itemsNum;      //Number of Data Items
} ParamSfo;

typedef struct
{
    u16 nameOffset;    //Offset of Key Name in Key Table
    u8  dataAlignment; //Always 04
    u8  valueType;     //Data type of value (0 - binary; 2 - text; 4 - Number (32))
    u32 valueSize;     //Size of Value data
    u32 valueSizeWithPadding; //Size of Value data and Padding
    u32 valueOffset;   //Offset of Data Value in Data Table
} Key;

int psfGetKey(const char * keyName, const char * data, char * value)
{
    int seek = 0;
    int foo = 0;

    ParamSfo info;
    memset(&info, 0, sizeof(info));
    Key key;
    memset(&key, 0, sizeof(key));

    //if (pspPSFCheck(data) == -1) return -1;
    seek = 4;
    memcpy(&info, &data[4], sizeof(info));
    seek += sizeof(info);
    for (foo = 0; foo < info.itemsNum; foo++)
    {
        memcpy(&key, &data[seek], sizeof(key));
        if (!strcmp(&data[info.keyTable+key.nameOffset], keyName))
        {
            memcpy(value, &data[info.valueTable+key.valueOffset], key.valueSize);
            return key.valueType;
        }
        seek += sizeof(key);
    }
    return -2;
}

char gameid[16] = "\0";
char gametitle[256] = "\0";

int getGameInfo()
{
    char * execFile = sceKernelInitFileName();
    char * foo;
    unsigned paramOffset;
    unsigned iconOffset;
    int fd;
    int size = 0;
    int i, ret;

    if (!execFile) return -1;
#ifdef DEBUG_MODE
    debuglog(execFile);
    debuglog("\r\n");
#endif
    memset(gameid,0x0,16);

    if (!strcmp("disc0:/PSP_GAME/SYSDIR/EBOOT.BIN", execFile) || !strcmp("disc0:/PSP_GAME/SYSDIR/BOOT.BIN", execFile))
    {

        i = sceUmdCheckMedium();
#ifdef DEBUG_MODE
        char txt[100];
        sprintf(txt, "UmdCheck:%i\r\n", i);
        debuglog(txt);
#endif
        if(i == 0)
        {
            ret = sceUmdWaitDriveStat(UMD_WAITFORDISC);
#ifdef DEBUG_MODE
        sprintf(txt, "UmdWaitForDisc:%i\r\n", ret);
        debuglog(txt);
#endif
        }
        ret = sceUmdActivate(1, "disc0:"); // Mount UMD to disc0: file system
#ifdef DEBUG_MODE
        sprintf(txt, "UmdActivate:%i\r\n", ret);
        debuglog(txt);
#endif
        ret = sceUmdWaitDriveStat(UMD_WAITFORINIT);
#ifdef DEBUG_MODE
        sprintf(txt, "UmdWaitForInit:%i\r\n", ret);
        debuglog(txt);
#endif
        fd = sceIoOpen("disc0:/PSP_GAME/PARAM.SFO", PSP_O_RDONLY, 0777); //IOASSIGN_RDONLY
        if (fd < 0){
#ifdef DEBUG_MODE
    debuglog("unable to open PARAM.SFO\r\n");
#endif
        } else {
			size = sceIoLseek(fd, 0, SEEK_END);
			sceIoLseek(fd, 0, SEEK_SET);
			if (size <= 0)
			{
				sceIoClose(fd);
				return -2;
			}
        }
    }

    else

    {
        fd = sceIoOpen(execFile, PSP_O_RDONLY, 0777);
        if (fd < 0){
#ifdef DEBUG_MODE
    debuglog("unable to open executable\r\n");
#endif
        } else {
			size = sceIoLseek(fd, 0, SEEK_END);
			sceIoLseek(fd, 0, SEEK_SET);
			if (size <= 0)
			{
				sceIoClose(fd);
				return -3;
			}
			sceIoLseek(fd, 8, SEEK_SET);
			sceIoRead(fd, &paramOffset, 4);
			sceIoRead(fd, &iconOffset, 4);
			size = iconOffset - paramOffset;
			sceIoLseek(fd, paramOffset, SEEK_SET);
        }
    }
    SceUID memid = sceKernelAllocPartitionMemory(2, "paramsfo_buf", PSP_SMEM_Low, size+1, NULL);
    foo = sceKernelGetBlockHeadAddr(memid);
    memset(foo, 0, size+1);

	if (fd){
		sceIoRead(fd, foo, size+1);
		sceIoClose(fd);

		psfGetKey("DISC_ID", foo, gameid);
		psfGetKey("TITLE", foo, gametitle);
		sceKernelFreePartitionMemory(memid);
	}
    return 0;
}
