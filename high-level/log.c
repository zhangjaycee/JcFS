#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> //for spinlock
#include <sys/syscall.h> //for SYS_gettid
#include <inttypes.h> //for PRId64
#include <stdarg.h> //for va_start
#include <unistd.h> //for syscall
#include <string.h>



FILE *logfile;
pthread_spinlock_t spinlock;
char banner[4096];

#define TRACE_FILE_LEN 18
#define TRACE_FILE "/JcFS.log"

int64_t print_timer(void)
{
    struct timespec tms;

    if (clock_gettime(CLOCK_REALTIME, &tms)) {
        printf("ERROR\n");
        return 0;
    }
    int64_t micros = tms.tv_sec * 1000000;

    micros += tms.tv_nsec/1000;
    if (tms.tv_nsec % 1000 >= 500)
        ++micros;
    return micros;
}

/* called with file lock */
int print_banner(void)
{
    int len;
    int64_t time;
    int pid;
    unsigned long tid;

    banner[0] = '\0';
    time = print_timer();
    pid = getpid();
    tid = syscall(SYS_gettid);
    if (time == 0)
        return -1;
    len = sprintf(banner, "Time : %"PRId64" Pid : %d Tid : %lu ",
                            time, pid, tid);
    (void) len;
    fputs(banner, logfile);
    return 0;
}

int log_open(char *statsDir_relative)
{
	char *statsDir = realpath(statsDir_relative, NULL);
    char *trace_path = NULL;

    if (statsDir) {
        trace_path = (char *)malloc(strlen(statsDir) +
                TRACE_FILE_LEN + 1);
        memset(trace_path, 0, strlen(statsDir) + TRACE_FILE_LEN + 1);
        strncpy(trace_path, statsDir, strlen(statsDir));
        strncat(trace_path, TRACE_FILE, TRACE_FILE_LEN);
    } else {
        trace_path = (char *)malloc(TRACE_FILE_LEN);
        memset(trace_path, 0, TRACE_FILE_LEN);
        strncpy(trace_path, TRACE_FILE + 1, TRACE_FILE_LEN-1);
    }
    printf("Trace file location : %s\n", trace_path);
    logfile = fopen(trace_path, "a");
    if (logfile == NULL) {
        perror("logfile");
        free(trace_path);
        return -1;
    }
    free(trace_path);
    setvbuf(logfile, NULL, _IOLBF, 0);
    return 0;
}

void log_close(void)
{

    if (logfile)
        fclose(logfile);
}

void StackFS_trace(const char *format, ...)
{
    va_list ap;
    int ret = 0;

    /*lock*/
    //printf("[debug] locking\n");
    //pthread_spin_lock(&spinlock);
    if (logfile) {
        /*Banner : time + pid + tid*/
        ret = print_banner();
        if (ret)
            goto trace_out;
        /*Done with banner*/
        va_start(ap, format);
        vfprintf(logfile, format, ap);
        /*Done with trace*/
        fprintf(logfile, "\n");
    }
trace_out:
    /*unlock*/
    //pthread_spin_unlock(&spinlock);
    return;
}
