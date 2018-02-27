#!/bin/bash

mkdir ~/mntpoint_jcfs
fusermount3 -u ~/mntpoint_jcfs
make clean
make 
./jcFs ~/mntpoint_jcfs
