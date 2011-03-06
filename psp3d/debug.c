#include <pspiofilemgr.h>
#include <pspsysmem.h>
#include <stdio.h>
#include "debug.h"

//#define LOG_STDOUT

int debuglog(const char* txt)//, ...)
{
  /*  char buff[256];
    va_list args;
    va_start(args, txt);
    vsprintf(buff,txt,args);
    va_end(args);
    */
  // Append Data
#ifdef LOG_STDOUT
	#define PSPLINK_OUT 1

	sceIoWrite(PSPLINK_OUT, string, strlen(string));
	return 0;//fprintf (stdout,string);
	//return strlen(string);
#else
  return appendBufferToFile((void*)txt, strlen(txt));
#endif
}
#ifndef LOG_STDOUT
int appendBufferToFile(void * buffer, int buflen)
{
  // Written Bytes
  int written = 0;
 
  // Open File for Appending
  SceUID file = sceIoOpen(LOGFILE, PSP_O_APPEND | PSP_O_CREAT | PSP_O_WRONLY, 0777);
  //if fail try GO file
//  if (file < 0)
//	  file = sceIoOpen(LOGFILEGO, PSP_O_APPEND | PSP_O_CREAT | PSP_O_RDWR, 0777);
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
