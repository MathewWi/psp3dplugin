#include <pspiofilemgr.h>
#include <pspsysmem.h>
#include <stdio.h>
#include "debug.h"

#define LOG_STDOUT

int debuglog(const char * string)
{
  // Append Data
#ifdef LOG_STDOUT
	#define PSPLINK_OUT 1

	sceIoWrite(PSPLINK_OUT, string, strlen(string));
	return 0;//fprintf (stdout,string);
	//return strlen(string);
#else
  return appendBufferToFile(LOGFILE, (void*)string, strlen(string));
#endif
}
#ifndef LOG_STDOUT
int appendBufferToFile(const char * path, void * buffer, int buflen)
{
  // Written Bytes
  int written = 0;
 
  // Open File for Appending
  SceUID file = sceIoOpen(path, PSP_O_APPEND | PSP_O_CREAT | PSP_O_WRONLY, 0777);
  // Opened File
  if(file >= 0)
  {
    // Write Buffer
    written = sceIoWrite(file, buffer, buflen);
   
    // Close File
    sceIoClose(file);
  }
 
  // Return Written Bytes
  return written;
}
#endif
