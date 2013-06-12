/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/


#ifndef _LOG_H_
#define _LOG_H_

#include "libsyndicate.h"

// logging functions
FILE* log_init(const char* logpath);
int log_shutdown( FILE* logfile );
void logmsg( FILE* logfile, const char* msg, ... );
int logerr( FILE* logfile, const char* msg, ... );

#endif
