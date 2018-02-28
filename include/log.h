#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> //for spinlock
#include <sys/syscall.h> //for SYS_gettid
#include <inttypes.h> //for PRId64
#include <stdarg.h> //for va_start

extern FILE *logfile;
extern pthread_spinlock_t spinlock;
extern char banner[4096];

int64_t print_timer(void);
int print_banner(void);
int log_open(char *statsDir_relative);
void log_close(void);
void StackFS_trace(const char *format, ...);
