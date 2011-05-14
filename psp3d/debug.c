#include <stdio.h>
#include <pspiofilemgr.h>
#include <pspsysmem.h>
#include "debug.h"

//#define LOG_STDOUT
extern SceUID debugSema;
char asyncLog[100][200];
unsigned short logEntry = 0;
int debuglog(const char* txt)//, ...)
{
  /*  char buff[256];
    va_list args;
    va_start(args, txt);
    vsprintf(buff,txt,args);
    va_end(args);
    */
	if (sceKernelIsIntrContext() == 1){
		//while running within interrupt we could not write directly
		if (logEntry > 90)
			logEntry = 1;
		strncpy(asyncLog[logEntry], txt, strlen(txt));
		logEntry++;
		sceKernelSignalSema(debugSema, 1);
	}
  // Append Data
#ifdef LOG_STDOUT
	#define PSPLINK_OUT 1

	sceIoWrite(PSPLINK_OUT, txt, strlen(txt));
	return 0;//fprintf (stdout,string);
	//return strlen(string);
#else
  return appendBufferToFile((void*)txt, strlen(txt));
#endif

	return 0;
}

typedef struct logPara{
	const char* text;
}logPara;
int logThread( SceSize args, void *argp ){
	debuglog(((logPara*)argp)->text);
	sceKernelDelayThread(1000);
	return sceKernelExitDeleteThread(0);
}

const char* logMessage;
LogData logData;

int debuglog_special(LogData* log){
	logData = (*log);
//signal the debugging thread to do the logfile writing
	sceKernelSignalSema(debugSema, 1);

	return 0;
}

int debuglog_async(){
	char buff[256];
	//sprintf(buff, logData.txt, logData.p1, logData.p2, logData.p3);
	debuglog(asyncLog[logEntry-1]);
	//if (logData.p1 != 0)
		//traceGeList((unsigned int*)logData.p1);
	return 0;
}

#ifndef LOG_STDOUT
int appendBufferToFile(void * buffer, int buflen)
{
  // Written Bytes
  int written = 0;
 
  // Open File for Appending
  SceUID file = sceIoOpen(LOGFILE, PSP_O_APPEND | PSP_O_CREAT | PSP_O_WRONLY, 0777);
  //if fail try GO file
  if (file < 0)
	  file = sceIoOpen(LOGFILEGO, PSP_O_APPEND | PSP_O_CREAT | PSP_O_WRONLY, 0777);
  // Opened File
  if(file >= 0)
  {
    // Write Buffer
    sceIoLseek(file, 0, PSP_SEEK_END);
    written = sceIoWrite(file, buffer, buflen);
   
    // Close File
    sceIoClose(file);
  }
 
  // Return Written Bytes
  return written;
}
#endif
