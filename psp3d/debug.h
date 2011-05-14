#ifndef PSP_DEBUG_H
#define PSP_DEBUG_H

//debug mode does activate the logging of the function entries
//to get an idea which order the current game is calling them
//#define DEBUG_MODE
//trace mode does activate logging all GE-List entries
//this is to get an idea how the current game is using the same
//#define TRACE_MODE
//#define TRACE_LIST_MODE


#define LOGFILE "ms0:/psp3d.log"
#define LOGFILEGO "ef0:/psp3d.log"

typedef struct LogData{
	const char* txt;
	unsigned int p1, p2, p3;
}LogData;

// Debug Log
int debuglog(const char * txt);//, ...);
int debuglog_special(LogData* log);
int debuglog_async();
#endif 
