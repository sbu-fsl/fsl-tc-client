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
 */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <linux/limits.h>
#include <assert.h>
#include <stdio.h>
#include "tc_api.h"
#include "posix/tc_impl_posix.h"
#include "nfs4/tc_impl_nfs4.h"
#include "path_utils.h"
#include "common_types.h"
#include "sys/stat.h"
#include "tc_helper.h"
#include "tc_nfs.h"

static vres TC_OKAY = { .index = -1, .err_no = 0, };

static bool TC_IMPL_IS_NFS4 = false;

static pthread_t tc_counter_thread;
static const char *tc_counter_path = "/tmp/tc-counters.txt";
static int tc_counter_running = 1;

const struct vattrs_masks VATTRS_MASK_ALL = VMASK_INIT_ALL;
const struct vattrs_masks VATTRS_MASK_NONE = VMASK_INIT_NONE;

bool tc_counter_printer(struct tc_func_counter *tfc, void *arg)
{
	buf_t *pbuf = (buf_t *)arg;
	buf_appendf(pbuf, "%s %u %u %llu %llu ",
		    tfc->name,
		    __sync_fetch_and_or(&tfc->calls, 0),
		    __sync_fetch_and_or(&tfc->failures, 0),
		    __sync_fetch_and_or(&tfc->micro_ops, 0),
		    __sync_fetch_and_or(&tfc->time_ns, 0));
	return true;
}

static void *output_tc_counters(void *arg)
{
	char buf[4096];
	buf_t bf = BUF_INITIALIZER(buf, 4096);

	FILE *pfile = fopen(tc_counter_path, "w");
	while (__sync_fetch_and_or(&tc_counter_running, 0)) {
		buf_reset(&bf);
		tc_iterate_counters(tc_counter_printer, &bf);
		buf_append_char(&bf, '\n');
		fwrite(bf.data, 1, bf.size, pfile);
		fflush(pfile);
		sleep(TC_COUNTER_OUTPUT_INTERVAL);
	}
	fclose(pfile);
	return NULL;
}

/* Not thread-safe */
void *vinit(const char *config_path, const char *log_path, uint16_t export_id)
{
	void *context;
	int retval;

	TC_IMPL_IS_NFS4 = (config_path !=  NULL);
	if (TC_IMPL_IS_NFS4) {
		context = nfs4_init(config_path, log_path, export_id);
	} else {
		context = posix_init(config_path, log_path);
	}

	if (!context) {
		return NULL;
	}

	tc_counter_running = 1;
	retval =
	    pthread_create(&tc_counter_thread, NULL, &output_tc_counters, NULL);
	if (retval != 0) {
		fprintf(stderr, "failed to create tc_counter thread: %s\n",
			strerror(retval));
		vdeinit(context);
		return NULL;
	}

	init_page_cache(((struct cache_context*)context)->cache_size,
			((struct cache_context*)context)->cache_expiration);
	/*init_data_cache(((struct cache_context*)context)->data_cache_size,
			((struct cache_context*)context)->data_cache_expiration);*/
	init_data_cache(100,60000);

	return context;
}

void vdeinit(void *module)
{
	buf_t *pbuf = new_auto_buf(4096);
	FILE *pfile;

	__sync_fetch_and_sub(&tc_counter_running, 1);
	tc_iterate_counters(tc_counter_printer, pbuf);
	buf_append_char(pbuf, '\n');

	pfile = fopen(tc_counter_path, "a+");
	assert(pfile);
	fwrite(pbuf->data, 1, pbuf->size, pfile);
	fclose(pfile);

	deinit_page_cache();
	deinit_data_cache();

	if (TC_IMPL_IS_NFS4) {
		nfs4_deinit(module);
	}
}

vfile *vec_open(const char **paths, int count, int *flags, mode_t *modes)
{
	vfile *tcfs;
	TC_DECLARE_COUNTER(open);

	TC_START_COUNTER(open);
	if (TC_IMPL_IS_NFS4) {
		tcfs = nfs_openv(paths, count, flags, modes);
	} else {
		tcfs = posix_openv(paths, count, flags, modes);
	}
	TC_STOP_COUNTER(open, count, tcfs != NULL);

	return tcfs;
}

vfile *vec_open_simple(const char **paths, int count, int flags, mode_t mode)
{
	vfile *tcfs;
	int i;
	int *flag_array = (int *) calloc(count, sizeof(int));
	mode_t *mode_array = (mode_t *) calloc(count, sizeof(mode_t));

	for (i = 0; i < count; ++i) {
		flag_array[i] = flags;
		mode_array[i] = mode;
	}
	tcfs = vec_open(paths, count, flag_array, mode_array);

	free(mode_array);
	free(flag_array);
	return tcfs;
}

vres vec_close(vfile *tcfs, int count)
{
	vres tcres;
	TC_DECLARE_COUNTER(close);

	TC_START_COUNTER(close);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_closev(tcfs, count);
	} else {
		tcres = posix_closev(tcfs, count);
	}
	TC_STOP_COUNTER(close, count, vokay(tcres));

	return tcres;
}

off_t sca_fseek(vfile *tcf, off_t offset, int whence)
{
	off_t res;
	TC_DECLARE_COUNTER(seek);

	TC_START_COUNTER(seek);
	if (TC_IMPL_IS_NFS4) {
		res = nfs_fseek(tcf, offset, whence);
	} else {
		res = posix_fseek(tcf, offset, whence);
	}
	TC_STOP_COUNTER(seek, 1, res != -1);

	return res;
}

vfile* sca_open_by_path(int dirfd, const char *pathname, int flags, mode_t mode)
{
	return vec_open(&pathname, 1, &flags, &mode);
}

int sca_close(vfile *tcf)
{
	return vec_close(tcf, 1).err_no;
}

vres vec_read(struct viovec *reads, int count, bool is_transaction)
{
	int i;
	vres tcres;
	TC_DECLARE_COUNTER(read);

	TC_START_COUNTER(read);
	for (i = 0; i < count; ++i) {
		if (reads[i].is_creation) {
			TC_STOP_COUNTER(read, count, false);
			return vfailure(i, EINVAL);
		}
	}
	/**
	 * TODO: check if the functions should use posix or TC depending on the
	 * back-end file system.
	 */
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_readv(reads, count, is_transaction);
	} else {
		tcres = posix_readv(reads, count, is_transaction);
	}
	TC_STOP_COUNTER(read, count, vokay(tcres));

	return tcres;
}

vres vec_write(struct viovec *writes, int count, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(write);

	TC_START_COUNTER(write);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_writev(writes, count, is_transaction);
	} else {
		tcres = posix_writev(writes, count, is_transaction);
	}
	TC_STOP_COUNTER(write, count, vokay(tcres));

	return tcres;
}


struct syminfo {
	const char *src_path; // path of file to be checked for symlink; can be
			      // NULL if file does not have path (eg, is fd)
	char *target_path; // path symlink points to; NULL if src_path is found
			   // to not be a symlink
};

bool resolve_symlinks(struct syminfo *syms, int count, vres *err) {
	int i;
	struct vattrs attrs[count];
	int attrs_original_indices[count];
	int attrs_count = 0;
	const char **paths;
	int *paths_original_indices;
	int path_count = 0;
	char **bufs;
	size_t *bufsizes;
	bool res = true;

	*err = TC_OKAY;

	for (i = 0; i < count; i++) {
		struct syminfo sym = syms[i];
		if (sym.src_path) {
			attrs[attrs_count].file =
			    vfile_from_path(sym.src_path);
			attrs[attrs_count].masks = VATTRS_MASK_NONE;
			attrs[attrs_count].masks.has_mode = true;
			attrs_original_indices[attrs_count] = i;
			attrs_count++;
		} else {
		    sym.target_path = NULL;
		}
	}

	if (attrs_count == 0) {
		return false;
	}

	*err = vec_lgetattrs(attrs, attrs_count, false);
	if (!vokay(*err)) {
		err->index = attrs_original_indices[err->index];
		return false;
	}

	paths = (const char **) calloc(attrs_count, sizeof(char *));
	bufs = (char **) calloc(attrs_count, sizeof(char *));
	bufsizes = (size_t *) calloc(attrs_count, sizeof(size_t));
	paths_original_indices = (int *) calloc(attrs_count, sizeof(int));

	for (i = 0; i < attrs_count; i++) {
		if (S_ISLNK(attrs[i].mode))	{
			paths[path_count] = attrs[i].file.path;

			bufs[path_count] = (char*) malloc(sizeof(char) * PATH_MAX);
			bufsizes[path_count] = PATH_MAX;

			paths_original_indices[path_count] = i;

			path_count++;
		} else {
			syms[attrs_original_indices[i]].target_path = NULL;
		}
	}

	if (path_count == 0) {
		res = false;
		goto exit;
	}

	*err = vec_readlink(paths, bufs, bufsizes, path_count, false);
	//TODO: what if vec_readlink() returns symlinks (symlink to symlink)?
	if (!vokay(*err)) {
		err->index =
		    paths_original_indices[attrs_original_indices[err->index]];
		res = false;
		goto exit;
	}

	for (i = 0; i < path_count; i++) {
		struct syminfo *sym =
		    &syms[attrs_original_indices[paths_original_indices[i]]];
		if (sym->src_path[0] == '/' && bufs[i][0] != '/') {
			char *path = (char*) malloc(sizeof(char) * PATH_MAX);
			slice_t dirname = tc_path_dirname(strdup(sym->src_path));
			((char*) dirname.data)[dirname.size] = '\0';
			tc_path_join(dirname.data, bufs[i], path, PATH_MAX);
			free(bufs[i]);
			free((char*)dirname.data);
			sym->target_path = path;
		} else {
			sym->target_path = bufs[i];
		}
	}

exit:
	free(paths_original_indices);
	free(bufsizes);
	free(bufs);
	free(paths);
	return res;
}

void free_syminfo(struct syminfo *syminfo, int count) {
	int i;
	for (i = 0; i < count; i++) {
		if (syminfo[i].target_path != NULL) {
			free(syminfo[i].target_path);
		}
	}
}

vres vec_getattrs(struct vattrs *attrs, int count, bool is_transaction)
{
	vres res;
	const char *paths[count];
	vfile original_files[count];
	struct vattrs_masks old_masks[count];
	mode_t old_modes[count];
	int mode_original_indices[count];
	int original_indices[count];

	char *bufs[count];
	size_t bufsizes[count];

	int old_mode_count = 0;
	int link_count = 0;
	int i;

	for (i = 0; i < count; i++) {
		// if caller doesn't want to get a mode, save old mode before
		// passing attrs into lgetattrsv to determine if files are
		// symlinks
		if (!attrs[i].masks.has_mode) {
			old_modes[old_mode_count] = attrs[i].mode;
			old_masks[old_mode_count] = attrs[i].masks;
			attrs[i].masks.has_mode = true;
			mode_original_indices[old_mode_count] = i;
			old_mode_count++;
		}
	}

	res = vec_lgetattrs(attrs, count, false);

	for (i = 0; i < count; i++) {
		if (S_ISLNK(attrs[i].mode)) {
			vfile file = attrs[i].file;
			assert(file.type == VFILE_PATH);

			paths[link_count] = file.path;

			bufs[link_count] = (char *) malloc(sizeof(char) * PATH_MAX);
			bufsizes[link_count] = PATH_MAX;
			original_indices[link_count] = i;

			link_count++;
		}
	}

	for (i = 0; i < old_mode_count; i++) {
		attrs[mode_original_indices[i]].mode = old_modes[i];
		attrs[mode_original_indices[i]].masks = old_masks[i];
	}

	// if nothing was a symlink, then our prior call to vec_lgetattrs()
	// sufficed, so we're done
	if (!vokay(res) || link_count == 0) {
		goto exit;
	}


	res = vec_readlink(paths, bufs, bufsizes, link_count, false);
	// TODO: what if vec_readlink() returns another symlink (symlink to
	// symlink)?

	if (!vokay(res)) {
		res.index = original_indices[res.index];
		goto exit;
	}

	for (i = 0; i < link_count; i++) {
		original_files[i] = attrs[original_indices[i]].file;
		attrs[original_indices[i]].file = vfile_from_path(bufs[i]);
	}

	res = vec_lgetattrs(attrs, link_count, is_transaction);

	if (!vokay(res)) {
		res.index = original_indices[res.index];
	}

	for (i = 0; i < link_count; i++) {
		attrs[original_indices[i]].file = original_files[i];
	}

exit:
	for (i = 0; i < link_count; ++i) {
		free(bufs[i]);
	}
	return res;
}

vres vec_lgetattrs(struct vattrs *attrs, int count, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(lgetattrs);

	TC_START_COUNTER(lgetattrs);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_lgetattrsv(attrs, count, is_transaction);
	} else {
		tcres = posix_lgetattrsv(attrs, count, is_transaction);
	}
	TC_STOP_COUNTER(lgetattrs, count, vokay(tcres));

	return tcres;
}

static int sca_stat_impl(vfile tcf, struct stat *buf, bool readlink)
{
	const char *path;
	char *linkbuf;
	char *link_target;
	int ret;
	vres tcres;
	struct vattrs tca = {
		.file = tcf,
		.masks = VATTRS_MASK_ALL,
	};

	tcres = vec_lgetattrs(&tca, 1, false);
	if (!vokay(tcres)) {
		return tcres.err_no;
	}

	if (!readlink || !S_ISLNK(tca.mode)) {
		vattrs2stat(&tca, buf);
		return 0;
	}

	assert(tcf.type == VFILE_PATH);

	linkbuf = (char*) alloca(PATH_MAX);
	link_target = (char*) alloca(PATH_MAX);

	while (S_ISLNK(tca.mode)) {
		path = tca.file.path;
		ret = sca_readlink(path, linkbuf, PATH_MAX);
		if (ret != 0) {
			return ret;
		}

		tc_path_joinall(link_target, PATH_MAX, path, "..", linkbuf);

		tca.file.path = link_target;
		tcres = vec_lgetattrs(&tca, 1, false);
		if (!vokay(tcres)) {
			return tcres.err_no;
		}
	}

	vattrs2stat(&tca, buf);
	return 0;
}

int sca_stat(const char *path, struct stat *buf)
{
	return sca_stat_impl(vfile_from_path(path), buf, true);
}

int sca_fstat(vfile *tcf, struct stat *buf)
{
	return sca_stat_impl(*tcf, buf, false);
}

int sca_lstat(const char *path, struct stat *buf)
{
	return sca_stat_impl(vfile_from_path(path), buf, false);
}

vres vec_setattrs(struct vattrs *attrs, int count, bool is_transaction)
{
	struct syminfo syminfo[count];
	vres res;
	int i;
	bool had_link;

	for(i = 0; i < count; i++) {
		if (attrs[i].file.type == VFILE_PATH) {
			syminfo[i].src_path = attrs[i].file.path;
		} else {
			syminfo[i].src_path = NULL;
		}
	}

	had_link = resolve_symlinks(syminfo, count, &res);
	if (!vokay(res)) {
		return res;
	}

	if (!had_link) {
		return vec_lsetattrs(attrs, count, is_transaction);
	}

	for (i = 0; i < count; i++) {
		if (syminfo[i].target_path)	{
			attrs[i].file.path = syminfo[i].target_path;
		}
	}

	res = vec_lsetattrs(attrs, count, is_transaction);

	for (i = 0; i < count; i++) {
		if (syminfo[i].target_path)	{
			attrs[i].file.path = syminfo[i].src_path;
		}
	}

	free_syminfo(syminfo, count);

	return res;
}

vres vec_lsetattrs(struct vattrs *attrs, int count, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(lsetattrs);

	TC_START_COUNTER(lsetattrs);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_lsetattrsv(attrs, count, is_transaction);
	} else {
		tcres = posix_lsetattrsv(attrs, count, is_transaction);
	}
	TC_STOP_COUNTER(lsetattrs, count, vokay(tcres));

	return tcres;
}

struct _vattrs_array {
	struct vattrs *attrs;
	size_t size;
	size_t capacity;
};

static bool fill_dir_entries(const struct vattrs *entry, const char *dir,
			     void *cbarg)
{
	void *buf;
	struct _vattrs_array *parray = (struct _vattrs_array *)cbarg;

	if (parray->size >= parray->capacity) {
		buf = realloc(parray->attrs,
			      sizeof(struct vattrs) * parray->capacity * 2);
		if (!buf) {
			return false;
		}
		parray->attrs = (struct vattrs *)buf;
		parray->capacity *= 2;
	}
	parray->attrs[parray->size] = *entry;
	parray->attrs[parray->size].file.path = strdup(entry->file.path);
	parray->size += 1;

	return true;
}

vres sca_listdir(const char *dir, struct vattrs_masks masks, int max_count,
		  bool recursive, struct vattrs **contents, int *count)
{
	vres tcres;
	struct _vattrs_array atarray;

	atarray.size = 0;
	if (max_count == 0) {
		atarray.capacity = 8;
	} else {
		assert(max_count > 0);
		atarray.capacity = max_count;
	}
	atarray.attrs = (struct vattrs *) calloc(atarray.capacity, sizeof(struct vattrs));
	if (!atarray.attrs) {
		return vfailure(0, ENOMEM);
	}

	tcres = vec_listdir(&dir, 1, masks, max_count, recursive,
			    fill_dir_entries, &atarray, false);
	if (!vokay(tcres)) {
		vfree_attrs(atarray.attrs, atarray.size, true);
	}

	*count = atarray.size;
	if (*count == 0) {
		free(atarray.attrs);
		*contents = NULL;
	} else {
		*contents = atarray.attrs;
	}

	return tcres;
}

vres vec_listdir(const char **dirs, int count, struct vattrs_masks masks,
		   int max_entries, bool recursive, vec_listdir_cb cb,
		   void *cbarg, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(listdir);

	if (count == 0) return TC_OKAY;

	TC_START_COUNTER(listdir);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_listdirv(dirs, count, masks, max_entries, recursive,
				     cb, cbarg, is_transaction);
	} else {
		tcres = posix_listdirv(dirs, count, masks, max_entries,
				      recursive, cb, cbarg, is_transaction);
	}
	TC_STOP_COUNTER(listdir, count, vokay(tcres));

	return tcres;
}

vres vec_rename(vfile_pair *pairs, int count, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(rename);

	TC_START_COUNTER(rename);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_renamev(pairs, count, is_transaction);
	} else {
		tcres = posix_renamev(pairs, count, is_transaction);
	}
	TC_STOP_COUNTER(rename, count, vokay(tcres));

	return tcres;
}

vres vec_remove(vfile *files, int count, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(remove);

	TC_START_COUNTER(remove);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_removev(files, count, is_transaction);
	} else {
		tcres = posix_removev(files, count, is_transaction);
	}
	TC_STOP_COUNTER(remove, count, vokay(tcres));

	return tcres;
}

int sca_unlink(const char *path)
{
	vfile tcf = vfile_from_path(path);
	return vec_remove(&tcf, 1, false).err_no;
}

vres vec_unlink(const char **paths, int count)
{
	int i = 0, r = 0;
	vfile *files;
	vres tcres;

	files = (vfile *)calloc(count, sizeof(vfile));
	for (i = 0; i < count; ++i) {
		files[i] = vfile_from_path(paths[i]);
	}

	tcres = vec_remove(files, count, false);

	free(files);
	return tcres;
}

vres vec_mkdir(struct vattrs *dirs, int count, bool is_transaction)
{
	int i;
	vres tcres;
	TC_DECLARE_COUNTER(mkdir);

	TC_START_COUNTER(mkdir);
	for (i = 0; i < count; ++i) {
		assert(dirs[i].masks.has_mode);
	}
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_mkdirv(dirs, count, is_transaction);
	} else {
		tcres = posix_mkdirv(dirs, count, is_transaction);
	}
	TC_STOP_COUNTER(mkdir, count, vokay(tcres));

	return tcres;
}

static vres posix_ensure_dir(slice_t *comps, int n, mode_t mode)
{
	vres tcres = TC_OKAY;
	struct vattrs attrs;
	buf_t *path;
	int i;

	path = new_auto_buf(PATH_MAX + 1);
	for (i = 0; i < n; ++i) {
		tc_path_append(path, comps[i]);
		attrs.file = vfile_from_path(asstr(path));
		tcres = posix_lgetattrsv(&attrs, 1, false);
		if (vokay(tcres)) {
			continue;
		}
		if (tcres.err_no != ENOENT) {
			/*POSIX_ERR("failed to stat %s", path->data);*/
			return tcres;
		}
		vset_up_creation(&attrs, path->data, mode);
		tcres = posix_mkdirv(&attrs, 1, false);
		if (!vokay(tcres)) {
			/*POSIX_ERR("failed to create %s", path->data);*/
			return tcres;
		}
	}

	return tcres;
}

static vres nfs4_ensure_dir(slice_t *comps, int n, mode_t mode)
{
	vres tcres = TC_OKAY;
	struct vattrs *dirs;
	buf_t *path;
	int absent;
	int i;

	dirs = (struct vattrs *) calloc(n, sizeof(*dirs));
	dirs[0].file = vfile_from_path(new_auto_str(comps[0]));
	dirs[0].masks = VATTRS_MASK_NONE;
	dirs[0].masks.has_mode = true;
	for (i = 1; i < n; ++i) {
		dirs[i].file = vfile_from_cfh(new_auto_str(comps[i]));
		dirs[i].masks = dirs[0].masks;
	}

	tcres = vec_getattrs(dirs, n, false);
	if (vokay(tcres) || tcres.err_no != ENOENT) {
		goto exit;
	}

	path = new_auto_buf(PATH_MAX + 1);
	absent = 0;
	for (i = 0; i < n; ++i) {
		if (i < tcres.index) {
			tc_path_append(path, comps[i]);
			continue;
		} else if (i == tcres.index) {
			tc_path_append(path, comps[i]);
			vset_up_creation(&dirs[absent], asstr(path), mode);
		} else {
			vset_up_creation(&dirs[absent],
					   new_auto_str(comps[i]), mode);
			dirs[absent].file.type = VFILE_CURRENT;
		}
		++absent;
	}

	if (absent == 0)
		goto exit;

	tcres = vec_mkdir(dirs, absent, false);

exit:
	free(dirs);
	return tcres;
}

vres sca_ensure_dir(const char *dir, mode_t mode, slice_t *leaf)
{
	vres tcres = TC_OKAY;
	slice_t *comps;
	int n;

	n = tc_path_tokenize(dir, &comps);
	if (n < 0) {
		return vfailure(0, -n);
	}

	if (leaf && n > 0) {
		*leaf = comps[--n];
	}

	if (n == 0) {
		goto exit;
	}

	if (TC_IMPL_IS_NFS4) {
		tcres = nfs4_ensure_dir(comps, n, mode);
	} else {
		tcres = posix_ensure_dir(comps, n, mode);
	}

exit:
	free(comps);
	return tcres;
}

vres vec_lcopy(struct vextent_pair *pairs, int count, bool is_transaction)
{
	vres tcres;
	TC_DECLARE_COUNTER(lcopy);

	TC_START_COUNTER(lcopy);

	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_lcopyv(pairs, count, is_transaction);
	} else {
		tcres = posix_lcopyv(pairs, count, is_transaction);
	}
	TC_STOP_COUNTER(lcopy, count, vokay(tcres));

	return tcres;
}

static vres
tc_pair(struct vextent_pair *pairs, int count, bool is_transaction,
	vres (*fn)(struct vextent_pair *pairs, int count, bool txn))
{
	struct syminfo syminfo[count];
	vres res;
	int i;
	bool had_link;

	for(i = 0; i < count; i++) {
		syminfo[i].src_path = pairs[i].src_path;
	}

	had_link = resolve_symlinks(syminfo, count, &res);
	if (!vokay(res)) {
		return res;
	}

	if (!had_link) {
		return fn(pairs, count, is_transaction);
	}

	for (i = 0; i < count; i++) {
		if (syminfo[i].target_path) {
			pairs[i].src_path = syminfo[i].target_path;
		}
	}

	/* TODO: What if a target is an existing symlink. */
	res = fn(pairs, count, is_transaction);

	for (i = 0; i < count; i++) {
		if (syminfo[i].target_path) {
			pairs[i].src_path = syminfo[i].src_path;
		}
	}

	free_syminfo(syminfo, count);

	return res;
}

vres vec_copy(struct vextent_pair *pairs, int count, bool is_transaction)
{
	return tc_pair(pairs, count, is_transaction, vec_lcopy);
}

/**
 * FIXME: allow moving files larger than RAM.
 */
vres vec_ldup(struct vextent_pair *pairs, int count, bool is_transaction)
{
	vres tcres;
	struct viovec *iovs;
	int i;

	iovs = (struct viovec*) malloc(count * sizeof(*iovs));
	assert(iovs);
	for (i = 0; i < count; ++i) {
		iovs[i].file = vfile_from_path(pairs[i].src_path);
		iovs[i].offset = pairs[i].src_offset;
		iovs[i].length = pairs[i].length;
		iovs[i].data = (char*) malloc(pairs[i].length);
		iovs[i].is_creation = false;
		iovs[i].is_failure = false;
		iovs[i].is_eof = false;
	}

	tcres = vec_read(iovs, count, is_transaction);
	if (!vokay(tcres)) {
		fprintf(stderr,
			"vec_ldup failed when reading %s (%d-th file): %s",
			pairs[i].src_path, i, strerror(tcres.err_no));
		goto exit;
	}

	for (i = 0; i < count; ++i) {
		iovs[i].file = vfile_from_path(pairs[i].dst_path);
		iovs[i].is_creation = true;
		iovs[i].is_write_stable = true;
		iovs[i].is_failure = false;
	}
	tcres = vec_write(iovs, count, is_transaction);
	if (!vokay(tcres)) {
		fprintf(stderr,
			"vec_ldup failed when writing %s (%d-th file): %s",
			pairs[i].dst_path, i, strerror(tcres.err_no));
		goto exit;
	}

exit:
	for (i = 0; i < count; ++i) {
		free(iovs[i].data);
	}
	free(iovs);

	return tcres;
}

vres vec_dup(struct vextent_pair *pairs, int count, bool is_transaction)
{
	return tc_pair(pairs, count, is_transaction, vec_ldup);
}

vres vec_hardlink(const char **oldpaths, const char **newpaths, int count,
		    bool istxn)
{
	vres tcres = TC_OKAY;
	TC_DECLARE_COUNTER(hardlink);

	TC_START_COUNTER(hardlink);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_hardlinkv(oldpaths, newpaths, count, istxn);
	} else {
		tcres = posix_hardlinkv(oldpaths, newpaths, count, istxn);
	}
	TC_STOP_COUNTER(hardlink, count, vokay(tcres));

	return tcres;
}

vres vec_symlink(const char **oldpaths, const char **newpaths, int count,
		   bool istxn)
{
	vres tcres = TC_OKAY;
	TC_DECLARE_COUNTER(symlink);

	TC_START_COUNTER(symlink);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_symlinkv(oldpaths, newpaths, count, istxn);
	} else {
		tcres = posix_symlinkv(oldpaths, newpaths, count, istxn);
	}
	TC_STOP_COUNTER(symlink, count, vokay(tcres));

	return tcres;
}

vres vec_readlink(const char **paths, char **bufs, size_t *bufsizes,
		    int count, bool istxn)
{
	vres tcres;
	TC_DECLARE_COUNTER(readlink);

	TC_START_COUNTER(readlink);
	if (TC_IMPL_IS_NFS4) {
		tcres = nfs_readlinkv(paths, bufs, bufsizes, count, istxn);
	} else {
		tcres = posix_readlinkv(paths, bufs, bufsizes, count, istxn);
	}
	TC_STOP_COUNTER(readlink, count, vokay(tcres));

	return tcres;
}

vres tc_write_adb(struct tc_adb *patterns, int count, bool is_transaction)
{
	return TC_OKAY;
}


int sca_chdir(const char *path)
{
	if (TC_IMPL_IS_NFS4) {
		return nfs_chdir(path);
	} else {
		return posix_chdir(path);
	}
}

char *sca_getcwd()
{
	if (TC_IMPL_IS_NFS4) {
		return nfs_getcwd();
	} else {
		return posix_getcwd();
	}
}
