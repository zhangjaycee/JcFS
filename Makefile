INC_PATH = ./include
SRC_PATH = ./high-level
SRC_PATH_LL = ./low-level
SRC_PATH_LZ4 = ./lz4
#SRC = $(wildcard ${SRC_PATH}/*.c) 
SRC = high-level/passthrough.c high-level/log.c
SRC_PTHREAD = high-level/passthrough_pthread.c high-level/log.c
SRC_LL = $(wildcard ${SRC_PATH_LL}/*.c)  $(wildcard ${SRC_PATH_LZ4}/*.c)
BIN = .
TARGET = $(BIN)/jcFs
TARGET_PTHREAD = $(BIN)/jcFs_pthread
TARGET_LL = $(BIN)/jcFs_ll

CC = gcc
CFLAGS = -I${INC_PATH} `pkg-config fuse3 --cflags --libs` -Wall -lpthread
#CFLAGS = -I${INC_PATH} -I/usr/local/include/fuse3 -L/usr/local/lib/x86_64-linux-gnu -lpthread -Wall

all: jcFs jcFs_pthread jcFs_ll
	
jcFs:
	#export PKG_CONFIG_PATH="/usr/local/lib64/pkgconfig"
	$(CC) $(SRC) $(CFLAGS) -o $(TARGET)
	#gcc -Wall -g  passthrough_ll.c lz4.c `pkg-config fuse3 --cflags --libs` -std=c99 -o jcFs_ll 
	#gcc -Wall -g  passthrough_fh.c lz4.c `pkg-config fuse3 --cflags --libs` -std=c99 -o jcFs

jcFs_pthread:
	#PKG_CONFIG_PATH="/usr/local/lib64/pkgconfig"
	$(CC) $(SRC_PTHREAD) $(CFLAGS) -o $(TARGET_PTHREAD)

jcFs_ll:
	#PKG_CONFIG_PATH="/usr/local/lib64/pkgconfig"
	$(CC) $(SRC_LL) $(CFLAGS) -o $(TARGET_LL)

clean:
	rm -rf $(TARGET)
	rm -rf $(TARGET_PTHREAD)
	rm -rf $(TARGET_LL)

