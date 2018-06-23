/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

/** @file
 *
 * This file system mirrors the existing file system hierarchy of the
 * system, starting at the root file system. This is implemented by
 * just "passing through" all requests to the corresponding user-space
 * libc functions. Its performance is terrible.
 *
 * Compile with
 *
 *     gcc -Wall passthrough.c `pkg-config fuse3 --cflags --libs` -o passthrough
 *
 * ## Source code ##
 * \include passthrough.c
 */

//#define JC_LOG

#define FUSE_USE_VERSION 31

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <pthread.h>
#include "passthrough_pthread.h"

#include "log.h"



/********* queue model

		|  enqueue
		V
     |      |
   9 | |||| |  <- head[0:4]
   8 | |||| |
   7 | |||| |
   6 | |||| |
   5 | |||| |
   4 | |||| |  <- tail[0] tail[2]
   3 |  | | |  <- tail[1]
   2 |    | |  <- tail[3] 
   1 |      |
   0 |      |     (already finished requests)
     |      |
	 +      +

queue_head_number and queue_head will only be modified by xmp_read
queue_tail_number and queue_tail will only be modified by pool_func
since number of pool_funcs is THREAD_NUM, wo dont need locks in pool_func
***********/


// variables for pthread
int th_n = THREAD_NUM;
struct IO_msg *queue_head[THREAD_NUM];
struct IO_msg *queue_tail[THREAD_NUM];
int queue_head_number[THREAD_NUM];
int queue_tail_number[THREAD_NUM];
pthread_cond_t queue_ready[THREAD_NUM];
pthread_mutex_t queue_lock[THREAD_NUM];

void *pool_func(void *void_arg)
{
    struct Arg_th *arg_th = (struct Arg_th *)void_arg;
    int i = arg_th->index;
	struct IO_msg *msg;
    int n;
    struct Arg *arg;
    
    while(1) {
        if (queue_head[i]->num > queue_tail[i]->num) {
            msg = queue_tail[i]->prev;
            arg = &(msg->args);
#ifdef MMAP
            memcpy(buf, file_buf + arg->offset, arg->size);
            n = arg->size;
#else
            n = pread(arg->fd, arg->buf, arg->size, arg->offset);
#endif
#ifdef JC_LOG
            if (n != arg->size)
                JcFS_log("[DEBUG] read failed n = %d!\n", n);
#endif
            queue_tail[i] = queue_tail[i]->prev;
            free(queue_tail[i]->next);
        } else {
            usleep(1);
        }
    }
}



static void *xmp_init(struct fuse_conn_info *conn,
		      struct fuse_config *cfg)
{
	(void) conn;
	cfg->use_ino = 1;

	/* Pick up changes from lower filesystem right away. This is
	   also necessary for better hardlink support. When the kernel
	   calls the unlink() handler, it does not know the inode of
	   the to-be-removed entry and can therefore not invalidate
	   the cache of the associated inode - resulting in an
	   incorrect st_nlink value being reported for any remaining
	   hardlinks to this inode. */
	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

#ifdef JC_LOG
    JcFS_log("XMP  initing ...");
#endif

    //pthread create
    pthread_t tid[th_n];

    struct Arg_th **arg_th = (struct Arg_th **)malloc(sizeof(struct Arg_th *) * th_n);
	int i;
    for (i = 0; i < th_n; i++) {
        queue_head[i] = malloc(sizeof(struct IO_msg));
        queue_head[i]->next = queue_head[i]->prev = NULL;
        queue_head[i]->num = 0;
        queue_tail[i] = queue_head[i];
        arg_th[i] = (struct Arg_th *)malloc(sizeof(struct Arg_th));
        arg_th[i]->index = i;
        pthread_mutex_init(&queue_lock[i], NULL);
        pthread_create(&tid[i], NULL, pool_func, arg_th[i]);
    }
	//pthread destory
	/*
    for (i = 0; i < th_n; i++) {
        pthread_cancel(tid[i]);
    }
	*/
#ifdef JC_LOG
    JcFS_log("XMP  inited ...");
#endif

	return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi,
		       enum fuse_readdir_flags flags)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;
	(void) flags;

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{
	int res;

	if (flags)
		return -EINVAL;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size,
			struct fuse_file_info *fi)
{
	int res;

	if (fi != NULL)
		res = ftruncate(fi->fh, size);
	else
		res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode,
		      struct fuse_file_info *fi)
{
	int res;

	res = open(path, fi->flags, mode);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int res;

	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	fi->fh = res;
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
#ifdef JC_LOG
    JcFS_log("reading from [%s] ...", path);
#endif
	int fd;
	int res;

	if(fi == NULL) {
		fd = open(path, O_RDONLY);
#ifdef JC_LOG
		JcFS_log("!!! The file [%s] is not opened, reopening it ...", path);
#endif
	} else {
		fd = fi->fh;
	}
	
	if (fd == -1)
		return -errno;

	
	// 1. directly passthrough:
	//res = pread(fd, buf, size, offset);
	
	// 2. pthread pread:
	// init the messages
	struct IO_msg **new_msg = (struct IO_msg **)malloc(sizeof(struct IO_msg *) * th_n);
	//struct IO_msg *tmp_p;
    int my_head_number[th_n];
	int i;
	for (i = 0; i < th_n; i++) {
		new_msg[i] = (struct IO_msg *)malloc(sizeof(struct IO_msg));
		new_msg[i]->args.fd = fd;
		new_msg[i]->args.buf = buf + size/th_n * i;
		new_msg[i]->args.size = size/th_n;
		new_msg[i]->args.offset = offset + size/th_n * i;
        // enqueue
		pthread_mutex_lock(&queue_lock[i]);
		new_msg[i]->num = queue_head[i]->num + 1;
        my_head_number[i] = queue_head[i]->num + 1;
		new_msg[i]->next = queue_head[i];
        queue_head[i]->prev = new_msg[i];
        queue_head[i] = new_msg[i];
		pthread_mutex_unlock(&queue_lock[i]);
	}
	// waiting until all threads completed
	while (1) {
        int ok_flag = 1;
		for (i = 0; i < th_n; i++) {
            if (queue_tail[i]->num < my_head_number[i]) {
                usleep(1);
                ok_flag = 0;
                break;
            }
		}
		if (ok_flag) {
#ifdef JC_LOG
			JcFS_log("[threads sync] succeed! %d", my_head_number[i]);
#endif
			res = size;
			break;
        }
	}

	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
#ifdef JC_LOG
    JcFS_log("writing to [%s] ...", path);
#endif
	int fd;
	int res;

	(void) fi;
	if(fi == NULL)
		fd = open(path, O_WRONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	if(fi == NULL)
		fd = open(path, O_WRONLY);
	else
		fd = fi->fh;
	
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	if(fi == NULL)
		close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.init           = xmp_init,
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= xmp_utimens,
#endif
	.open		= xmp_open,
	.create 	= xmp_create,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
};

int main(int argc, char *argv[])
{
//#ifdef JC_LOG
    //init logfile
    pthread_spin_init(&spinlock, 0);
    log_open(".");
    JcFS_log("====================================");
    JcFS_log("hello, I'm JcFs and I am initing ...");
//#endif
	umask(0);
	int ret = fuse_main(argc, argv, &xmp_oper, NULL);

//#ifdef JC_LOG
    JcFS_log("hello, I'm JcFs and I am closing ...");
    JcFS_log("====================================");
    log_close();
//#endif
	return ret;
}
