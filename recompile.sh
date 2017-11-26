#!/bin/bash

fusermount3 -u /root/mnt
make clean
make 
./jcFs /root/mnt
