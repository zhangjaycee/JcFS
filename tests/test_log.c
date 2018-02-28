#include <stdio.h>
#include "log.h"


int main(void)
{
    printf("log_test start...\n");
    printf("spinlock init...\n");
    pthread_spin_init(&spinlock, 0);
    log_open(".");
    printf("log_test writing...\n");
    JcFS_log("hello, I'm testing the log system...");
    log_close();
    printf("log_test end\n");
    return 0;

}
