#define TIMES 200
#define MAXSIZE 32  // MB
#define MINSIZE 4096 // KB
#define MAX_THREAD_NUM 32
#define THREAD_NUM 2

struct Arg {
    int fd;
    char *buf;
    int size;
    int offset;
};

struct IO_msg { // The queue node
    struct Arg args;
    struct IO_msg *next;
};

struct Arg_th {
    int index;
};
