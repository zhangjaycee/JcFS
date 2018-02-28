#include <stdio.h>
#include "log.h"


int main(void)
{
    printf("log_test start...\n");
    printf("spinlock init...\n");
    pthread_spin_init(&spinlock, 0);
    log_open("test.log");
    printf("log_test writing...\n");
    StackFS_trace("hello, I'm testing the log system...");
    log_close();
    printf("log_test end\n");
    return 0;

}
