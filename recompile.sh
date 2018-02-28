#!/bin/bash

mkdir mnt
fusermount3 -u mnt
make clean
make 
./jcFs mnt
