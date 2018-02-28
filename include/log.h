#include <stdio.h>
#include <stdlib.h>

extern FILE *logfile;
int log_open(char *statsDir_relative);
void log_close(void);
void StackFS_trace(const char *format, ...);
