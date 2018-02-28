# JcFS

I'm trying to write my first toy File System, that's exicting but not easy for me ... JcFS has no specific features yet (233333..)

high-level JcFS is under developing now... I'm planning to add a small log/trace system, and make it able to compress or decompress when access some specific directories.

### JcFS will be built on top of:

* FUSE and libfuse -- Filesystem in Userspace (https://github.com/libfuse/libfuse)

* LZ4 -- A very fast compression algorithm (https://github.com/lz4/lz4)

### When implement some details(e.g. log system), I referenced to these projects:

* sbu-fsl/fuse-stackfs (https://github.com/sbu-fsl/fuse-stackfs)
