/**
 * Copyright (C) Stony Brook University 2016
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Client API of NFS Transactional Compounds (TC).
 *
 * Functions with "tc_" are general API, whereas functions with "tx_" are API
 * with transaction support.
 */
#ifndef __TC_API_H__
#define __TC_API_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include "common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize tc_client
 * log_path - Location of the log file
 * config_path - Location of the config file
 * export_id - Export id of the export configured in the conf file
 *
 * This returns fsal_module pointer to tc_client module
 * If tc_client module does not exist, it will return NULL
 *
 * Caller of this function should call vdeinit() after use
 */
void *vinit(const char *config_path, const char *log_path,
	      uint16_t export_id);

/*
 * Free the reference to module and op_ctx
 * Should be called if vinit() was called previously
 *
 * This will always succeed
 */
void vdeinit(void *module);

enum VFILETYPE {
	VFILE_NULL = 0,
	VFILE_DESCRIPTOR,
	VFILE_PATH,
	VFILE_HANDLE,
	VFILE_CURRENT,
	VFILE_SAVED,
};

#define VFD_NULL -1
#define VFD_CWD -2
#define VFD_ABS -3

/* See http://lxr.free-electrons.com/source/include/linux/exportfs.h */
#define FILEID_NFS_FH_TYPE 0x1001

/**
 * "type" is one of the six file types; "fd" and "path_or_handle" depend on
 * the file type:
 *	0. A "type" value of VFILE_NULL means the vfile is invalid or emtpy.
 *
 *	1. When "type" is VFILE_DESCRIPTOR, "fd" identifies the file we are
 *	operating on.
 *
 *	2. When "type" is VFILE_PATH, "fd" is the base file descriptor, and
 *	"path_or_handle" is the file path.  The file is identified by resolving
 *	the path relative to "fd".  In this case, "fd" has two special values:
 *	(a) TC_FDCWD which means the current working directory, and
 *	(b) TC_FDABS which means the "path_or_handle" is an absolute path.
 *
 *	3. When "type" is VFILE_HANDLE, "fd" is "mount_fd", and
 *	"path_or_handle" points to "struct file_handle".  We expand the "type"
 *	of "struct file_handle" to include FILEID_NFS_FH_TYPE.
 *
 *	4. When "type" is VFILE_CURRENT, the "current filehandle" on the NFS
 *	server side is used.  "fd" and "path" are ignored.
 *
 *	5. When "type" is VFILE_SAVED, the "saved filehandle" on the NFS
 *	server side is used.  "fd" and "path" are ignored.
 *
 * See http://man7.org/linux/man-pages/man2/open_by_handle_at.2.html
 */
typedef struct _vfile
{
	int type;

	int fd;

	union
	{
		void *fd_data;
		const char *path;
		const struct file_handle *handle;
	}; /* path_or_handle */
} vfile;

static inline vfile vfile_from_path(const char *pathname) {
	vfile tf;

	assert(pathname);
	tf.type = VFILE_PATH;
	tf.fd = pathname[0] == '/' ? VFD_ABS : VFD_CWD;
	tf.path = pathname;

	return tf;
}

static inline vfile vfile_from_fd(int fd) {
	vfile tf;

	tf.type = VFILE_DESCRIPTOR;
	tf.fd = fd;
	tf.fd_data = NULL;

	return tf;
}

static inline vfile vfile_current(void)
{
	vfile tf;

	tf.type = VFILE_CURRENT;
	tf.fd = -1;     /* poison */
	tf.path = NULL; /* poison */

	return tf;
}

/**
 * Create a TC file relative to current FH.
 */
static inline vfile vfile_from_cfh(const char *relpath) {
	vfile tf;

	if (relpath && relpath[0] == '/') {
		return vfile_from_path(relpath);
	}

	tf.type = VFILE_CURRENT;
	tf.fd = -1;	/* poison */
	tf.path = relpath;

	return tf;
}

/**
 * Open a vfile using path.  Similar to "openat(2)".
 *
 * NOTE: It is not necessary that a vfile have to be open before reading
 * from/writing to it.  We recommend using vec_read() and vec_write() to
 * implicitly open a file when necessary.
 */
vfile* sca_open_by_path(int dirfd, const char *pathname, int flags,
			mode_t mode);

static inline vfile* sca_open(const char *pathname, int flags, mode_t mode)
{
	return sca_open_by_path(AT_FDCWD, pathname, flags, mode);
}

/**
 * Open a vfile using file handle.  Similar to "open_by_handle_at(2)".
 */
vfile sca_open_by_handle(int mount_fd, struct file_handle *fh, int flags);

/**
 * Close a vfile if necessary.
 */
int sca_close(vfile *tcf);

/**
 * Change current work directory to "path".
 *
 * Return 0 on success and a negative error number in case of failure.
 */
int sca_chdir(const char *path);

/**
 * Returns current working directory.
 *
 * The caller owns the returned buffer and is responsible for freeing it.
 */
char *sca_getcwd(void);

/**
 * A special offset that is the same as the file size.
 */
#define TC_OFFSET_END (SIZE_MAX)
/**
 * A special offset indicates the current offset of the file descriptor.
 */
#define TC_OFFSET_CUR (SIZE_MAX-1)

/**
 * Represents an I/O vector of a file.
 *
 * The fields have different meaning depending the operation is read or write.
 * Most often, clients allocate an array of this struct.
 */
struct viovec
{
	vfile file;
	size_t offset; /* IN: read/write offset */

	/**
	 * IN:  # of bytes of requested read/write
	 * OUT: # of bytes successfully read/written
	 */
	size_t length;

	/**
	 * This data buffer should always be allocated by caller for either
	 * read or write, and the length of the buffer should be indicated by
	 * the "length" field above.
	 *
	 * IN:  data requested to be written
	 * OUT: data successfully read
	 */
	char *data;

	unsigned int is_creation : 1; /* IN: create file if not exist? */
	unsigned int is_direct_io : 1;/* IN: is direct I/O or not */
	unsigned int is_failure : 1;  /* OUT: is this I/O a failure? */
	unsigned int is_eof : 1;      /* OUT: does this I/O reach EOF? */
	unsigned int is_write_stable : 1;   /* IN/OUT: stable write? */
};

struct viov_array
{
	int size;
	struct viovec *iovs;
};

#define VIOV_ARRAY_INITIALIZER(iov, s)                                       \
	{                                                                      \
		.size = (s), .iovs = (iov),                                    \
	}

static inline struct viov_array viovs2array(struct viovec *iovs, int s)
{
	struct viov_array iova = VIOV_ARRAY_INITIALIZER(iovs, s);
	return iova;
}

static inline struct viovec *viov2file(struct viovec *iov,
					   const vfile *tcf,
					   size_t off,
					   size_t len,
					   char *buf)
{
	iov->file = *tcf;
	iov->offset = off;
	iov->length = len;
	iov->data = buf;
	iov->is_creation = false;
	iov->is_direct_io = false;
	iov->is_failure = false;
	iov->is_eof = false;
	iov->is_write_stable = false;
	return iov;
}

static inline struct viovec *viov2current(struct viovec *iov, size_t off,
					      size_t len, char *buf)
{
	vfile tcf = vfile_current();
	return viov2file(iov, &tcf, off, len, buf);
}

static inline struct viovec *viov2path(struct viovec *iov, const char *path,
				       size_t off, size_t len, char *buf)
{
	iov->file = vfile_from_path(path);
	iov->offset = off;
	iov->length = len;
	iov->data = buf;
	iov->is_creation = false;
	iov->is_direct_io = false;
	iov->is_failure = false;
	iov->is_eof = false;
	iov->is_write_stable = false;
	return iov;
}

static inline struct viovec *viov2fd(struct viovec *iov, int fd, size_t off,
				     size_t len, char *buf)
{
	iov->file = vfile_from_fd(fd);
	iov->offset = off;
	iov->length = len;
	iov->data = buf;
	iov->is_creation = false;
	iov->is_direct_io = false;
	iov->is_failure = false;
	iov->is_eof = false;
	iov->is_write_stable = false;
	return iov;
}

static inline struct viovec *
viov4creation(struct viovec *iov, const char *path, size_t len, char *buf)
{
	iov->file = vfile_from_path(path);
	iov->offset = 0;
	iov->length = len;
	iov->data = buf;
	iov->is_creation = true;
	iov->is_direct_io = false;
	iov->is_failure = false;
	iov->is_eof = false;
	iov->is_write_stable = false;
	return iov;
}

/**
 * Result of a TC operation.
 *
 * When transaction is not enabled, compound processing stops upon the first
 * failure.
 */
typedef struct _vres
{
	int index;  /* index of the first failed operation */
	int err_no; /* error number of the failed operation */
} vres;

#define vokay(tcres) ((tcres).err_no == 0)

static inline vres vfailure(int i, int err) {
	vres res;
	res.index = i;
	res.err_no = err;
	return res;
}

vfile *vec_open(const char **paths, int count, int *flags, mode_t *modes);

vfile *vec_open_simple(const char **paths, int count, int flags, mode_t mode);

vres vec_close(vfile *files, int count);

/**
 * Reposition read/write file offset.
 * REQUIRE: tcf->type == VFILE_DESCRIPTOR
 */
off_t sca_fseek(vfile *tcf, off_t offset, int whence);

/**
 * Read from one or more files.
 *
 * @reads: the viovec array of read operations.  "path" of the first array
 * element must not be NULL; a NULL "path" of any other array element means
 * using the same "path" of the preceding array element.
 * @count: the count of reads in the preceding array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres vec_read(struct viovec *reads, int count, bool is_transaction);

static inline bool tx_vec_read(struct viovec *reads, int count)
{
	return vokay(vec_read(reads, count, true));
}

/**
 * Write to one or more files.
 *
 * @writes: the viovec array of write operations.  "path" of the first array
 * element must not be NULL; a NULL "path" of any other array element means
 * using the same "path"
 * @count: the count of writes in the preceding array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres vec_write(struct viovec *writes, int count, bool is_transaction);

static inline bool tx_vec_write(struct viovec *writes, int count)
{
	return vokay(vec_write(writes, count, true));
}

/**
 * The bitmap indicating the presence of file attributes.
 */
struct vattrs_masks
{
	unsigned int has_mode : 1;  /* protection flags */
	unsigned int has_size : 1;  /* file size, in bytes */
	unsigned int has_nlink : 1; /* number of hard links */
	unsigned int has_fileid : 1;
	unsigned int has_blocks : 1;	/* number of 512B blocks */
	unsigned int has_uid : 1;   /* user ID of owner */
	unsigned int has_gid : 1;   /* group ID of owner */
	unsigned int has_rdev : 1;  /* device ID of block or char special
				   files */
	unsigned int has_atime : 1; /* time of last access */
	unsigned int has_mtime : 1; /* time of last modification */
	unsigned int has_ctime : 1; /* time of last status change */
};

/**
 * File attributes.  See stat(2).
 */
struct vattrs
{
	vfile file;
	struct vattrs_masks masks;
	mode_t mode;   /* protection */
	size_t size;   /* file size, in bytes */
	nlink_t nlink; /* number of hard links */
	uint64_t fileid;
	blkcnt_t blocks;    /* number of 512B blocks */
	uid_t uid;
	gid_t gid;
	dev_t rdev;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
};

static inline void vattrs_set_mode(struct vattrs *attrs, mode_t mode)
{
	attrs->mode = mode;
	attrs->masks.has_mode = true;
}

static inline void vattrs_set_size(struct vattrs *attrs, size_t size)
{
	attrs->size = size;
	attrs->masks.has_size = true;
}

static inline void vattrs_set_fileid(struct vattrs *attrs, uint64_t fileid)
{
	attrs->fileid = fileid;
	attrs->masks.has_fileid = true;
}

static inline void vattrs_set_uid(struct vattrs *attrs, size_t uid)
{
	attrs->uid = uid;
	attrs->masks.has_uid = true;
}

static inline void vattrs_set_gid(struct vattrs *attrs, size_t gid)
{
	attrs->gid = gid;
	attrs->masks.has_gid = true;
}

static inline void vattrs_set_nlink(struct vattrs *attrs, size_t nlink)
{
	attrs->nlink = nlink;
	attrs->masks.has_nlink = true;
}

static inline void vattrs_set_atime(struct vattrs *attrs,
				      struct timespec atime)
{
	attrs->atime = atime;
	attrs->masks.has_atime = true;
}

static inline void vattrs_set_mtime(struct vattrs *attrs,
				      struct timespec mtime)
{
	attrs->mtime = mtime;
	attrs->masks.has_mtime = true;
}

static inline void vattrs_set_ctime(struct vattrs *attrs,
				      struct timespec ctime)
{
	attrs->ctime = ctime;
	attrs->masks.has_ctime = true;
}

static inline void vattrs_set_rdev(struct vattrs *attrs, dev_t rdev)
{
	attrs->rdev = rdev;
	attrs->masks.has_rdev = true;
}

static inline void vset_up_creation(struct vattrs *newobj, const char *name,
				      mode_t mode)
{
	newobj->file = vfile_from_path(name);
	memset(&newobj->masks, 0, sizeof(struct vattrs_masks));
	newobj->masks.has_mode = true;
	newobj->mode = mode;
	newobj->masks.has_uid = true;
	newobj->uid = geteuid();
	newobj->masks.has_gid = true;
	newobj->gid = getegid();
}

static inline void vstat2attrs(const struct stat *st, struct vattrs *attrs)
{
	if (attrs->masks.has_mode)
		attrs->mode = st->st_mode;
	if (attrs->masks.has_size)
		attrs->size = st->st_size;
	if (attrs->masks.has_nlink)
		attrs->nlink = st->st_nlink;
	if (attrs->masks.has_fileid)
		attrs->fileid = (uint64_t)st->st_ino;
	if (attrs->masks.has_uid)
		attrs->uid = st->st_uid;
	if (attrs->masks.has_gid)
		attrs->gid = st->st_gid;
	if (attrs->masks.has_rdev)
		attrs->rdev = st->st_rdev;
	if (attrs->masks.has_blocks)
		attrs->blocks = st->st_blocks;
	if (attrs->masks.has_atime) {
		attrs->atime.tv_sec = st->st_atim.tv_sec;
		attrs->atime.tv_nsec = st->st_atim.tv_nsec;
	}
	if (attrs->masks.has_mtime) {
		attrs->mtime.tv_sec = st->st_mtim.tv_sec;
		attrs->mtime.tv_nsec = st->st_mtim.tv_nsec;
	}
	if (attrs->masks.has_ctime) {
		attrs->ctime.tv_sec = st->st_ctim.tv_sec;
		attrs->ctime.tv_nsec = st->st_ctim.tv_nsec;;
	}
}

static inline void tc_attrs2attrs(struct vattrs *dstAttrs, const struct vattrs *srcAttrs)
{
	memcpy(&dstAttrs->file, &srcAttrs->file, sizeof(vfile));

	if (dstAttrs->masks.has_mode)
		dstAttrs->mode = srcAttrs->mode;
	if (dstAttrs->masks.has_size)
		dstAttrs->size = srcAttrs->size;
	if (dstAttrs->masks.has_nlink)
		dstAttrs->nlink = srcAttrs->nlink;
	if (dstAttrs->masks.has_fileid)
		dstAttrs->fileid = srcAttrs->fileid;
	if (dstAttrs->masks.has_uid)
		dstAttrs->uid = srcAttrs->uid;
	if (dstAttrs->masks.has_gid)
		dstAttrs->gid = srcAttrs->gid;
	if (dstAttrs->masks.has_rdev)
		dstAttrs->rdev = srcAttrs->rdev;
	if (dstAttrs->masks.has_blocks)
		dstAttrs->blocks = srcAttrs->blocks;
	if (dstAttrs->masks.has_atime) {
		dstAttrs->atime.tv_sec = srcAttrs->atime.tv_sec;
		dstAttrs->atime.tv_nsec = srcAttrs->atime.tv_nsec;
	}
	if (dstAttrs->masks.has_mtime) {
		dstAttrs->mtime.tv_sec = srcAttrs->mtime.tv_sec;
		dstAttrs->mtime.tv_nsec = srcAttrs->mtime.tv_nsec;
	}
	if (dstAttrs->masks.has_ctime) {
		dstAttrs->ctime.tv_sec = srcAttrs->ctime.tv_sec;
		dstAttrs->ctime.tv_nsec = srcAttrs->ctime.tv_nsec;
	}
}


static inline void vattrs2stat(const struct vattrs *attrs, struct stat *st)
{
	if (attrs->masks.has_mode)
		st->st_mode = attrs->mode;
	if (attrs->masks.has_size)
		st->st_size = attrs->size;
	if (attrs->masks.has_nlink)
		st->st_nlink = attrs->nlink;
	if (attrs->masks.has_fileid)
		st->st_ino = (ino_t)attrs->fileid;
	if (attrs->masks.has_blocks)
		st->st_blocks = attrs->blocks;
	if (attrs->masks.has_uid)
		st->st_uid = attrs->uid;
	if (attrs->masks.has_gid)
		st->st_gid = attrs->gid;
	if (attrs->masks.has_rdev)
		st->st_rdev = attrs->rdev;
	if (attrs->masks.has_atime) {
		st->st_atim.tv_sec = attrs->atime.tv_sec;
		st->st_atim.tv_nsec = attrs->atime.tv_nsec;
	}
	if (attrs->masks.has_mtime) {
		st->st_mtim.tv_sec = attrs->mtime.tv_sec;
		st->st_mtim.tv_nsec = attrs->mtime.tv_nsec;
	}
	if (attrs->masks.has_ctime) {
		st->st_ctim.tv_sec = attrs->ctime.tv_sec;
		st->st_ctim.tv_nsec = attrs->ctime.tv_nsec;
	}
}

extern const struct vattrs_masks VATTRS_MASK_ALL;
extern const struct vattrs_masks VATTRS_MASK_NONE;

#define VMASK_INIT_ALL                                                       \
	{                                                                      \
		.has_mode = true, .has_size = true, .has_nlink = true,         \
		.has_fileid = true, .has_blocks = true,                        \
		.has_uid = true, .has_gid = true, .has_rdev = true,            \
		.has_atime = true, .has_mtime = true, .has_ctime = true        \
	}

#define VMASK_INIT_NONE                                                      \
	{                                                                      \
		.has_mode = false, .has_size = false, .has_nlink = false,      \
		.has_fileid = false, .has_blocks = false,                      \
                .has_uid = false, .has_gid = false, .has_rdev = false,         \
                .has_atime = false, .has_mtime = false, .has_ctime = false     \
	}

/**
 * Get attributes of file objects.
 *
 * @attrs: array of attributes to get
 * @count: the count of vattrs in the preceding array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres vec_getattrs(struct vattrs *attrs, int count, bool is_transaction);
vres vec_lgetattrs(struct vattrs *attrs, int count, bool is_transaction);

static inline bool tx_vec_getattrs(struct vattrs *attrs, int count)
{
	return vokay(vec_getattrs(attrs, count, true));
}

static inline bool tx_vec_lgetattrs(struct vattrs *attrs, int count)
{
	return vokay(vec_lgetattrs(attrs, count, true));
}

int sca_stat(const char *path, struct stat *buf);
int sca_lstat(const char *path, struct stat *buf);
int sca_fstat(vfile *tcf, struct stat *buf);

static inline bool sca_exists(const char *path)
{
	struct stat buf;
	return sca_lstat(path, &buf) == 0;
}

/**
 * Set attributes of file objects.
 *
 * @attrs: array of attributes to set
 * @count: the count of vattrs in the preceding array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres vec_setattrs(struct vattrs *attrs, int count, bool is_transaction);

static inline bool tx_vec_setattrs(struct vattrs *attrs, int count)
{
	return vokay(vec_setattrs(attrs, count, true));
}

vres vec_lsetattrs(struct vattrs *attrs, int count, bool is_transaction);

static inline bool tx_vec_lsetattrs(struct vattrs *attrs, int count)
{
	return vokay(vec_lsetattrs(attrs, count, true));
}

/**
 * List the content of a directory.
 *
 * @dir [IN]: the path of the directory to list
 * @masks [IN]: masks of attributes to get for listed objects
 * @max_count [IN]: the maximum number of count to list; 0 means unlimited
 * @contents [OUT]: the pointer to the array of files/directories in the
 * directory.  The array and the paths in the array will be allocated
 * internally by this function; the caller is responsible for releasing the
 * memory, probably by using vfree_attrs().
 */
vres sca_listdir(const char *dir, struct vattrs_masks masks, int max_count,
		  bool recursive, struct vattrs **contents, int *count);

/**
 * Callback of vec_listdir().
 *
 * @entry [IN]: the current directory entry listed
 * @dir [IN]: the parent directory of @entry as provided in the first argument
 * of vec_listdir().
 * @cbarg [IN/OUT]: any extra user arguments or context of the callback.
 *
 * Return whether vec_listdir() should continue the processing or stop.
 */
typedef bool (*vec_listdir_cb)(const struct vattrs *entry, const char *dir,
			       void *cbarg);
/**
 * List the content of the specified directories.
 *
 * @dirs: the array of directories to list
 * @count: the length of "dirs"
 * @masks: the attributes to retrieve for each listed entry
 * @recursive [IN]: list directory entries recursively
 * @max_entries [IN]: the max number of entry to list; 0 means infinite
 * @cb [IN}: the callback function to be applied to each listed entry
 * @cbarg [IN/OUT]: private arguments for the callback
 */
vres vec_listdir(const char **dirs, int count, struct vattrs_masks masks,
		   int max_entries, bool recursive, vec_listdir_cb cb,
		   void *cbarg, bool is_transaction);

/**
 * Free an array of "vattrs".
 *
 * @attrs [IN]: the array to be freed
 * @count [IN]: the length of the array
 * @free_path [IN]: whether to free the paths in "vattrs" as well.
 */
static inline void vfree_attrs(struct vattrs *attrs, int count,
				 bool free_path)
{
	int i;
	if (free_path) {
		for (i = 0; i < count; ++i) {
			if (attrs[i].file.type == VFILE_PATH)
				free((char *)attrs[i].file.path);
			else if (attrs[i].file.type == VFILE_HANDLE)
				free((char *)attrs[i].file.handle);
		}
	}
	free(attrs);
}

typedef struct vfile_pair
{
	vfile src_file;
	vfile dst_file;
} vfile_pair;

/**
 * Rename the file from "src_path" to "dst_path" for each of "pairs".
 *
 * @pairs: the array of file pairs to be renamed
 * @count: the count of the preceding "vfile_pair" array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres vec_rename(struct vfile_pair *pairs, int count, bool is_transaction);

static inline bool tx_vec_rename(vfile_pair *pairs, int count)
{
	return vokay(vec_rename(pairs, count, true));
}

vres vec_remove(vfile *files, int count, bool is_transaction);

static inline bool tx_vec_remove(vfile *files, int count)
{
	return vokay(vec_remove(files, count, true));
}

int sca_unlink(const char *pathname);
vres vec_unlink(const char **pathnames, int count);

/**
 * Create one or more directories.
 *
 * @dirs [IN/OUT]: the directories and their attributes (mode, uid, gid) to be
 * created.  Other attributes (timestamps etc.) of the newly created
 * directories will be returned on success.
 * @count [IN]: the count of the preceding "dirs" array
 * @is_transaction [IN]: whether to execute the compound as a transaction
 */
vres vec_mkdir(struct vattrs *dirs, int count, bool is_transaction);

static inline bool tx_vec_mkdir(struct vattrs *dirs, int count,
			     bool is_transaction)
{
	return vokay(vec_mkdir(dirs, count, is_transaction));
}

struct vextent_pair
{
	const char *src_path;
	const char *dst_path;
	size_t src_offset;
	size_t dst_offset;
	/**
	 * When length is UINT64_MAX, it means the effective length is current
	 * file size of the source file substracted by src_offset.
	 */
	size_t length;
};

static inline void vfill_extent_pair(struct vextent_pair *tcep,
				       const char *spath, size_t soff,
				       const char *dpath, size_t doff,
				       size_t len)
{
	tcep->src_path = spath;
	tcep->dst_path = dpath;
	tcep->src_offset = soff;
	tcep->dst_offset = doff;
	tcep->length = len;
}

/**
 * Copy the file from "src_path" to "dst_path" for each of "pairs".
 *
 * @pairs: the array of file extent pairs to copy
 * @count: the count of the preceding "vextent_pair" array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres vec_copy(struct vextent_pair *pairs, int count, bool is_transaction);
vres vec_lcopy(struct vextent_pair *pairs, int count, bool is_transaction);

static inline bool tx_copyv(struct vextent_pair *pairs, int count)
{
	return vokay(vec_copy(pairs, count, true));
}

/**
 * Copy the data from "src_path" to "dst_path" by reading from "src_path" and
 * then writing to "dst_path".
 */
vres vec_dup(struct vextent_pair *pairs, int count, bool is_transaction);
vres vec_ldup(struct vextent_pair *pairs, int count, bool is_transaction);

vres vec_hardlink(const char **oldpaths, const char **newpaths, int count,
		    bool istxn);

static inline bool tx_vec_hardlink(const char **oldpaths, const char **newpaths,
				int count, bool istxn)
{
	return vokay(vec_hardlink(oldpaths, newpaths, count, true));
}

/**
 * Create a list of symlinks.  Useful for operations such as "cp -sR".
 *
 * @oldpaths: an array of source paths
 * @newpaths: an array of dest paths
 */
vres vec_symlink(const char **oldpaths, const char **newpaths, int count,
		   bool istxn);

static inline bool tx_vec_symlink(const char **oldpaths, const char **newpaths,
			       int count)
{
	return vokay(vec_symlink(oldpaths, newpaths, count, true));
}

vres vec_readlink(const char **paths, char **bufs, size_t *bufsizes,
		    int count, bool istxn);

static inline bool tx_vec_readlink(const char **paths, char **bufs,
				size_t *bufsizes, int count)
{
	return vokay(vec_readlink(paths, bufs, bufsizes, count, true));
}

static inline int sca_symlink(const char *oldpath, const char *newpath)
{
	return vec_symlink(&oldpath, &newpath, 1, false).err_no;
}

static inline int sca_readlink(const char *path, char *buf, size_t bufsize)
{
	return vec_readlink(&path, &buf, &bufsize, 1, false).err_no;
}

/**
 * Application data blocks (ADB).
 *
 * See https://tools.ietf.org/html/draft-ietf-nfsv4-minorversion2-39#page-60
 */
struct tc_adb
{
	const char *path;

	/**
	 * The offset within the file the ADB blocks should start.
	 */
	size_t adb_offset;

	/**
	 * size (in bytes) of an ADB block
	 */
	size_t adb_block_size;

	/**
	 * IN: requested number of ADB blocks to write
	 * OUT: number of ADB blocks successfully written.
	 */
	size_t adb_block_count;

	/**
	 * Relative offset within an ADB block to write then Application Data
	 * Block Number (ADBN).
	 *
	 * A value of UINT64_MAX means no ADBN to write.
	 */
	size_t adb_reloff_blocknum;

	/**
	 * The Application Data Block Number (ADBN) of the first ADB.
	 */
	size_t adb_block_num;

	/**
	 * Relative offset of the pattern within an ADB block.
	 *
	 * A value of UINT64_MAX means no pattern to write.
	 */
	size_t adb_reloff_pattern;

	/**
	 * Size and value of the ADB pattern.
	 */
	size_t adb_pattern_size;
	void *adb_pattern_data;
};

/**
 * Write Application Data Blocks (ADB) to one or more files.
 *
 * @patterns: the array of ADB patterns to write
 * @count: the count of the preceding pattern array
 * @is_transaction: whether to execute the compound as a transaction
 */
vres tc_write_adb(struct tc_adb *patterns, int count, bool is_transaction);

static inline bool tx_write_adb(struct tc_adb *patterns, int count)
{
	return vokay(tc_write_adb(patterns, count, true));
}

/**
 * Create the specified directory and all its ancestor directories.
 * When "leaf" is NULL, "dir" is considered the full path of the target
 * directory; when "leaf" is not NULL, the parent of "dir" is the target
 * directory, and leaf will be set to the name of the leaf node.
 */
vres sca_ensure_dir(const char *dir, mode_t mode, slice_t *leaf);

/**
 * Copy a directory to a new destination
 */
vres sca_cp_recursive(const char *src_dir, const char *dst, bool symlink,
		       bool use_server_side_copy);

/**
 * Remove a list of file-system objects (files or directories).
 */
vres vec_unlink_recursive(const char **objs, int count);

#ifdef __cplusplus
}
#endif

#endif // __TC_API_H__
