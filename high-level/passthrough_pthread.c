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
//#define JC_ALERT

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

#ifdef JC_ALERT
const char *sensitive_words[] = {"zjc", "ZJC", "jaycee", "Jaycee", "ZhangJaycee"};
int sw_nr;
#endif


// variables for pthread
//unsigned long long timer, timer_thread, throughput, throughput_thread;
//char *pthread_buf;
struct IO_msg *msg_queue[MAX_THREAD_NUM];
pthread_cond_t queue_ready[MAX_THREAD_NUM];
pthread_mutex_t queue_lock[MAX_THREAD_NUM];
pthread_mutex_t con_lock;
int th_n = THREAD_NUM;
int global_counter = 0;
int concurrency = 0;
int th_counter[MAX_THREAD_NUM];

void *pool_func(void *void_arg)
{
    struct Arg_th *arg_th = (struct Arg_th *)void_arg;
    int i = arg_th->index;
    th_counter[i] = 0;
	struct IO_msg *msg;
    int n;
    struct Arg *arg;
    while(1) {
		/*
		JcFS_log("%d thread loop waiting...", i);
        while (msg_queue[i] == NULL) {
			JcFS_log("pointer addr of msg_queue[%d]: %p ", i, msg_queue[i]);
			sleep(1);
		}
		JcFS_log("%d thread ready  ...", i);
		*/
        pthread_mutex_lock(&queue_lock[i]);
        while (msg_queue[i] == NULL) {
#ifdef JC_LOG
			JcFS_log("%d thread waiting...", i);
			JcFS_log("pointer addr of msg_queue[%d]: %p ", i, msg_queue[i]);
#endif
            pthread_cond_wait(&queue_ready[i], &queue_lock[i]);
#ifdef JC_LOG
			JcFS_log("%d thread ready  ...", i);
#endif
        }
		msg = msg_queue[i];
        msg_queue[i] = msg_queue[i]->next;
        pthread_mutex_unlock(&queue_lock[i]);
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
        //pthread_mutex_lock(&queue_lock[i]);
        th_counter[i]++;
        //pthread_mutex_unlock(&queue_lock[i]);
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
        msg_queue[i] = NULL;
        arg_th[i] = (struct Arg_th *)malloc(sizeof(struct Arg_th));
        arg_th[i]->index = i;
        pthread_cond_init(&queue_ready[i], NULL);
        pthread_mutex_init(&queue_lock[i], NULL);
		sleep(1);
        pthread_create(&tid[i], NULL, pool_func, arg_th[i]);
    }

	pthread_mutex_init(&con_lock, NULL);


	//pthread destory
	/*
    for (i = 0; i < th_n; i++) {
        pthread_cancel(tid[i]);
    }
    sleep(2);
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
	
/*
	pthread_mutex_lock(&con_lock);
	concurrency++;
    JcFS_log("concurrency: %d", concurrency);
	pthread_mutex_unlock(&con_lock);
*/
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
	global_counter++; // need to check int bound!!
	struct IO_msg **new_msg = (struct IO_msg **)malloc(sizeof(struct IO_msg *) * th_n);
	int j, k;
	for (j = 0; j < th_n; j++) {
		new_msg[j] = (struct IO_msg *)malloc(sizeof(struct IO_msg));
		new_msg[j]->args.fd = fd;
		new_msg[j]->args.buf = buf + size/th_n * j;
		new_msg[j]->args.size = size/th_n;
		new_msg[j]->args.offset = offset + size/th_n * j;
#ifdef JC_LOG
		if (new_msg[j] == NULL)
			JcFS_log("[read prepare] new_msg[%d] == NULL", j);
#endif
	}
	// add to queue and notify threads
	for (k = 0; k < th_n; k++) {
		pthread_mutex_lock(&queue_lock[k]);
		new_msg[k]->next = msg_queue[k];
		msg_queue[k] = new_msg[k];
#ifdef JC_LOG
		if (msg_queue[k] == NULL)
			JcFS_log("[read prepare] msg_queue[%d] == NULL", k);
		else
			JcFS_log("[read prepare] pointer addr of msg_queue[%d]: %p ", k, msg_queue[k]);
#endif
		pthread_mutex_unlock(&queue_lock[k]);
		while (pthread_cond_signal(&queue_ready[k])) {
			continue;
		}
	}	
	// waiting until all threads completed
	while (1) {
		int equal_flag = 1;
		for (k = 0; k < th_n; k++) pthread_mutex_lock(&queue_lock[k]);
		for (k = 0; k < th_n; k++) {
			if (th_counter[k] != global_counter)
				equal_flag = 0;
		}
		for (k = 0; k < th_n; k++) pthread_mutex_unlock(&queue_lock[k]);
		if (equal_flag) {
//#ifdef JC_LOG
			JcFS_log("[threads sync] succeed! %d", global_counter);
//#endif
			res = size;
			break;
		} else {
			continue;
		}
	}

	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
#ifdef JC_ALERT
    int i;
    for (i = 0; i < sw_nr; i++) {
        if (strstr(buf, sensitive_words[i])) {
            JcFS_log("[ALERT] Someone trying to READ sensitive word: %s", sensitive_words[i]);
        }
    }
#endif
/*
	pthread_mutex_lock(&con_lock);
	concurrency--;
	pthread_mutex_unlock(&con_lock);
*/
	return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
#ifdef JC_LOG
    JcFS_log("writing to [%s] ...", path);
#endif
#ifdef JC_ALERT
    int i;
    for (i = 0; i < sw_nr; i++) {
        if (strstr(buf, sensitive_words[i])) {
            JcFS_log("[ALERT] Someone trying to WRITE sensitive word: %s", sensitive_words[i]);
        }
    }
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
#ifdef JC_ALERT
    sw_nr = sizeof(sensitive_words) / sizeof(char *);
    JcFS_log("[debug alert system] sw_nr = %d, sensitive words list:", sw_nr);
    int i;
    for (i = 0; i < sw_nr; i++) {
        JcFS_log("%s", sensitive_words[i]);
    }
#endif
	umask(0);
	int ret = fuse_main(argc, argv, &xmp_oper, NULL);

//#ifdef JC_LOG
    JcFS_log("hello, I'm JcFs and I am closing ...");
    JcFS_log("====================================");
    log_close();
//#endif
	return ret;
}

