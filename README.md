# JcFS

I'm trying to write my first toy File System, that's exicting but not easy for me ... JcFS has no specific features yet (233333..)

high-level JcFS is under developing now... I'm planning to add a small log/trace system, and make it able to compress or decompress when access some specific directories.

### JcFS will be built on top of:

* FUSE and libfuse -- Filesystem in Userspace (https://github.com/libfuse/libfuse)

* LZ4 -- A very fast compression algorithm (https://github.com/lz4/lz4)


### Current avaliable features:

* Support log some important operations (e.g. init FS, close FS) to the logfile(`JcFS.log`).

* Support sensitive words monitoring. When read or write some specified words, an alert will be write to the logfile.

* JcFS-pthread (`high-level/passthough_pthread.c`) will split a read request into multiple parts, and each part will be processed by an individual pre-created thread. But now, JcFS-pthread is not thread-safe, high concurrency read may cause program failure.


### When implement some details(e.g. log system), I referenced to these projects:

* sbu-fsl/fuse-stackfs (https://github.com/sbu-fsl/fuse-stackfs)
