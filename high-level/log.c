#include <stdio.h>
#include <stdlib.h>

FILE *logfile;

int log_open(char *statsDir_relative)
{
	char *statsDir = realpath(statsDir_relative, NULL)
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
    logfile = fopen(trace_path, "w");
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
    pthread_spin_lock(&spinlock);
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
    pthread_spin_unlock(&spinlock);
}
