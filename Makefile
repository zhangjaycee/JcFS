all: jcFs

jcFs:
	PKG_CONFIG_PATH="/usr/local/lib64/pkgconfig"
	gcc -Wall passthrough_ll.c lz4.c `pkg-config fuse3 --cflags --libs` -std=c99 -o jcFs 

clean:
	rm -rf jcFs
