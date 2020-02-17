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

#include <unistd.h>
#include "tc_impl_nfs4.h"
#include "nfs4_util.h"
#include "tc_helper.h"
#include "log.h"
#include "fsal_types.h"
#include "../MainNFSD/nfs_init.h"
#include "path_utils.h"
#include "iovec_utils.h"

/*
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
void *nfs4_init(const char *config_path, const char *log_path,
		uint16_t export_id)
{
	char *exec_name = "nfs-ganesha";
	char *host_name = "localhost";
	struct fsal_module *new_module = NULL;
	sigset_t signals_to_block;
	struct config_error_type err_type;
	struct gsh_export *exp = NULL;
	int rc;
	fsal_status_t st;
	config_file_t config_struct;
	struct cache_context *ctx;
	nfs_start_info_t my_nfs_start_info = { .dump_default_config = false,
					       .lw_mark_trigger = false };

	nfs_prereq_init(exec_name, host_name, -1, log_path);

	/* Set up for the signal handler.
         * Blocks the signals the signal handler will handle.
         */
        sigemptyset(&signals_to_block);
        sigaddset(&signals_to_block, SIGHUP);
        sigaddset(&signals_to_block, SIGPIPE);
	rc = pthread_sigmask(SIG_BLOCK, &signals_to_block, NULL);
	if (rc != 0) {
		fprintf(
		    stderr,
		    "Could not start nfs daemon, pthread_sigmask failed: %s",
		    strerror(errno));
		return NULL;
	}

	/* Parse the configuration file so we all know what is going on. */

	rc = access(config_path, R_OK);
	if (rc != 0) {
		fprintf(stderr, "Could not access config file %s: %s; "
				"current working directory is: %s.\n",
			config_path, strerror(errno),
			getcwd(alloca(PATH_MAX), PATH_MAX));
		return NULL;
	}

	config_struct = config_ParseFile(config_path, &err_type);

	if (!config_error_no_error(&err_type)) {
		char *errstr = err_type_str(&err_type);

		if (!config_error_is_harmless(&err_type))
			LogFatal(
			    COMPONENT_INIT,
			    "Fatal error while parsing %s because of %s errors",
			    config_path, errstr != NULL ? errstr : "unknown");
		/* NOT REACHED */
		LogCrit(COMPONENT_INIT, "Minor parse errors found %s in %s",
			errstr != NULL ? errstr : "unknown", config_path);
		if (errstr != NULL)
			gsh_free(errstr);
	}

	if (read_log_config(config_struct) < 0)
		LogFatal(COMPONENT_INIT,
			 "Error while parsing log configuration");
	/* We need all the fsal modules loaded so we can have
	 * the list available at exports parsing time.
	 */
	start_fsals();

	/* parse configuration file */

	if (nfs_set_param_from_conf(config_struct, &my_nfs_start_info)) {
                LogFatal(COMPONENT_INIT,
                         "Error setting parameters from configuration file.");
        }

	/* initialize core subsystems and data structures */
        if (init_server_pkgs() != 0)
                LogFatal(COMPONENT_INIT,
                         "Failed to initialize server packages");

	new_module = lookup_fsal("TCNFS");
	if (new_module == NULL) {
		LogDebug(COMPONENT_FSAL, "TCNFS Module Not found\n");
		return NULL;
	}

	st = new_module->ops->init_config(new_module, config_struct);
	if (FSAL_IS_ERROR(st)) {
		LogFatal(COMPONENT_INIT, "failed to init TCNFS module");
	}

        /* Load export entries from parsed file
         * returns the number of export entries.
         */
        rc = ReadExports(config_struct);
        if (rc < 0)
                LogFatal(COMPONENT_INIT,
                          "Error while parsing export entries");
        else if (rc == 0)
                LogWarn(COMPONENT_INIT,
                        "No export entries found in configuration file !!!");

        /* freeing syntax tree : */
        config_Free(config_struct);

	exp = get_gsh_export(export_id);
	if (exp == NULL) {
		LogDebug(COMPONENT_FSAL, "Export Not found\n");
		return NULL;
	}

	LogDebug(COMPONENT_FSAL,
		 "Export %d at pseudo (%s) with path (%s) and tag (%s)",
		 exp->export_id, exp->pseudopath, exp->fullpath,
		 exp->FS_tag);

	// op_ctx is a symbol (pointer) from the shared library
	op_ctx = calloc(1, sizeof(*op_ctx));
	if (op_ctx == NULL) {
		LogDebug(COMPONENT_FSAL, "No memory for op_ctx\n");
		return NULL;
	}

	op_ctx->creds = NULL;
	op_ctx->export = exp;
	op_ctx->fsal_export = exp->fsal_export;

	LogCrit(COMPONENT_INIT, "Metadata Cache size: %lu & expiration: %lu",
				exp->cache_size, exp->cache_expiration);
	LogCrit(COMPONENT_INIT, "Data Cache size: %lu & expiration: %lu",
				exp->data_cache_size, exp->data_cache_expiration);

	rc = nfs4_chdir(exp->fullpath);
	assert(rc == 0);

	vinit_fds();

	ctx = calloc(1, sizeof(struct cache_context));
	if (ctx == NULL) {
		LogDebug(COMPONENT_FSAL, "No memory for ctx\n");
		return NULL;
	}

	ctx->context = (void*)new_module;
	ctx->cache_size = exp->cache_size;
	ctx->data_cache_size = exp->data_cache_size;
	ctx->cache_expiration = exp->cache_expiration;
	ctx->data_cache_expiration = exp->data_cache_expiration;
	return (void*)ctx;
}

/*
 * Free the reference to module and op_ctx
 * Should be called if nfs4_init() was called previously
 */
void nfs4_deinit(void *arg)
{
	struct fsal_module *module = NULL;
	struct gsh_export *export = op_ctx->export;

	/* Close all open fds, client might have forgot to close them */
	nfs4_close_all();

	export->fsal_export->obj_ops->tc_destroysession();

	if (op_ctx != NULL) {
		free(op_ctx);
		op_ctx = NULL;
	}

	if (arg != NULL) {
		module = (struct fsal_module*)((struct cache_context*)arg)->context;
		if (module != NULL) {
			LogDebug(COMPONENT_FSAL, "Dereferencing tc_client module\n");
			// In tc_init(), two references of the module are taken, one by
			// load_fsal() called via commit_fsal() during config loading,
			// and lookup_fsal() explicitly in tc_init().
			fsal_put(module);  /* for lookup_fsal() */
			fsal_put(module);  /* for load_fsal() */
		}
		free(arg);
		arg = NULL;
	}
}

/*
 * iovs - Array of reads for one or more files
 *       Contains file-path, read length, offset, etc.
 * read_count - Length of the above array
 *              (Or number of reads)
 */
vres nfs4_do_readv(struct viovec *iovs, int read_count, bool istxn,
		   struct vattrs *old_attrs, struct vattrs *attrs)
{
	struct gsh_export *export = op_ctx->export;
	vres tcres = { .index = 0, .err_no = (int)ENOENT };
	int finished;

	if (export == NULL) {
		return tcres;
	}

	if (export->fsal_export->obj_ops->vec_read == NULL) {
		tcres.err_no = (int)ENOTSUP;
		return tcres;
	}

	NFS4_DEBUG("nfs4_do_readv() called");

	for (finished = 0; finished < read_count; finished += tcres.index) {
		tcres = export->fsal_export->obj_ops->vec_read(
		    iovs + finished, read_count - finished, attrs + finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

/**
 * Use relative paths to shorten path lookups.
 *
 * Returns the saved vfiles if the iovs has been changed, or NULL if we
 * cannot compress the paths.
 */
/*
static vfile *nfs4_compress_paths(struct viovec *iovs, int count)
{
	vfile *saved_tcfs = NULL;
	char *buf;
	int i;
	bool compressed = false;

	if (iovs == NULL || count <= 1) {
		return NULL;
	}

	saved_tcfs = calloc(count, sizeof(*saved_tcfs));
	if (!saved_tcfs) {
		return NULL;
	}

	saved_tcfs[0] = iovs[0].file;
	for (i = 1; i < count; ++i) {
		saved_tcfs[i] = iovs[i].file;
		if (iovs[i].file.type != VFILE_PATH ||
		    saved_tcfs[i - 1].type != VFILE_PATH) {
			continue;
		}
		buf = malloc(PATH_MAX);
		if (!buf) {
			nfs4_decompress_paths(iovs, i, saved_tcfs);
			return NULL;
		}
		if (tc_path_rebase(saved_tcfs[i - 1].path, iovs[i].file.path,
				   buf, PATH_MAX) < 0) {
			free(buf);
			nfs4_decompress_paths(iovs, i, saved_tcfs);
			return NULL;
		}
		if (tc_path_tokenize(buf, NULL) >=
		    tc_path_tokenize(iovs[i].file.path, NULL)) {
			free(buf);
			continue;
		}
		iovs[i].file.type = VFILE_CURRENT;
		iovs[i].file.path = buf;
		compressed = true;
	}

	if (!compressed) {
		free(saved_tcfs);
		saved_tcfs = NULL;
	}

	return saved_tcfs;
}

static void nfs4_decompress_paths(struct viovec *iovs, int count,
				  vfile *saved_tcfs)
{
	int i;

	if (saved_tcfs == NULL) {
		return;
	}

	for (i = 0; i < count; ++i) {
		if (!tc_cmp_file(&iovs[i].file, saved_tcfs + i)) {
			free((char *)iovs[i].file.path);
			iovs[i].file = saved_tcfs[i];
		}
	}
	free(saved_tcfs);
}
*/

static int nfs4_fill_fd_data(vfile *tcf)
{
	struct nfs4_fd_data *fd_data;
	struct tc_kfd *tcfd = NULL;

	assert(tcf->type == VFILE_DESCRIPTOR);
	tcfd = tc_get_fd_struct(tcf->fd, false);
	if (!tcfd) {
		return -EINVAL;
	}
	fd_data = malloc(sizeof(*fd_data));
	if (!fd_data) {
		tc_put_fd_struct(&tcfd);
		return -ENOMEM;
	}
	/* TODO: check race condition */
	fd_data->stateid = &tcfd->stateid;
	fd_data->fh4 = &tcfd->fh;
	fd_data->fd_cursor = tcfd->offset;
	tc_put_fd_struct(&tcfd);
	tcf->fd_data = fd_data;

	return 0;
}

static void nfs4_clear_fd_data(vfile *tcf)
{
	if (tcf->fd_data) {
		free((struct nfs4_fd_data *)(tcf->fd_data));
		tcf->fd_data = NULL;
	}
}

static void nfs4_clear_fd_iovecs(struct viovec *iovs, int count)
{
	struct tc_kfd *tcfd = NULL;
	int i;

	for (i = 0; i < count; ++i) {
		if (iovs[i].file.type == VFILE_DESCRIPTOR) {
			if (iovs[i].offset == TC_OFFSET_CUR) {
				tcfd = tc_get_fd_struct(iovs[i].file.fd, true);
				assert(tcfd);
				tcfd->offset += iovs[i].length;
				tc_put_fd_struct(&tcfd);
			}
			nfs4_clear_fd_data(&iovs[i].file);
		}
	}
}

static int nfs4_fill_fd_iovecs(struct viovec *iovs, int count)
{
	int i;
	int r;

	for (i = 0; i < count; ++i) {
		if (iovs[i].file.type == VFILE_DESCRIPTOR &&
		    (r = nfs4_fill_fd_data(&iovs[i].file)) != 0) {
			nfs4_clear_fd_iovecs(iovs, --i);
			return r;
		}
	}

	return 0;
}

vres nfs4_do_iovec(struct viovec *iovs, int count, bool istxn,
		   vres (*fn)(struct viovec *iovs, int count, bool istxn,
			      struct vattrs *old_attrs,
			      struct vattrs *new_attrs),
		   struct vattrs *old_attrs, struct vattrs *new_attrs)
{
	static const int CPD_LIMIT = (1 << 20);
	int i, j, k;
	int nparts;
	struct viov_array iova = VIOV_ARRAY_INITIALIZER(iovs, count);
	struct viov_array *parts;
	vres tcres;

	for (i = 0; i < count; ++i) {
		iovs[i].is_eof = false;
		iovs[i].is_failure = false;
	}

	/* deal with VFILE_DESCRIPTOR files */
	tcres.err_no = nfs4_fill_fd_iovecs(iovs, count);
	if (tcres.err_no != 0) {
		tcres.index = 0;
		return tcres;
	}

	parts = tc_split_iov_array(&iova, CPD_LIMIT, &nparts);

	j = 0;
	for (i = 0; i < nparts; ++i) {
		tcres = fn(parts[i].iovs, parts[i].size, istxn, old_attrs + j,
			   new_attrs + j);
		if (!vokay(tcres)) {
			/* TODO: FIX tcres */
			goto exit;
		}
		for (k = 0; k < parts[i].size; ++k) {
			if (parts[i].iovs[k].__is_last_of_multiparts) {
				++j;
			}
		}
	}

exit:
	vrestore_iov_array(&iova, &parts, nparts);
	nfs4_clear_fd_iovecs(iovs, count);
	return tcres;
}

vres nfs4_readv(struct viovec *iovs, int count, bool istxn,
		struct vattrs *attrs)
{
	return nfs4_do_iovec(iovs, count, istxn, nfs4_do_readv, NULL, attrs);
}

/*
 * arg - Array of writes for one or more files
 *       Contains file-path, write length, offset, etc.
 * read_count - Length of the above array
 *              (Or number of reads)
 */
vres nfs4_do_writev(struct viovec *iovs, int write_count, bool istxn,
		    struct vattrs *old_attrs, struct vattrs *new_attrs)
{
	struct gsh_export *export = op_ctx->export;
	vres tcres = { .index = 0, .err_no = (int)ENOENT };
	int finished;

	if (export == NULL) {
		return tcres;
	}

	if (export->fsal_export->obj_ops->vec_write == NULL) {
		tcres.err_no = (int)ENOTSUP;
		return tcres;
	}

	NFS4_DEBUG("nfs4_do_writev() called");

	for (finished = 0; finished < write_count; finished += tcres.index) {
		tcres = export->fsal_export->obj_ops->vec_write(
		    iovs + finished, write_count - finished,
		    old_attrs + finished, new_attrs + finished, istxn);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

vres nfs4_writev(struct viovec *iovs, int count, bool istxn,
		 struct vattrs *old_attrs, struct vattrs *new_attrs)
{
	return nfs4_do_iovec(iovs, count, istxn, nfs4_do_writev, old_attrs,
			     new_attrs);
}

vfile *nfs4_openv(const char **paths, int count, int *flags, mode_t *modes,
		    struct vattrs *attrs)
{
	int i;
	int finished;
	vres tcres;
	vfile *tcfs;
	struct gsh_export *export = op_ctx->export;
	stateid4 *sids;
	struct tc_kfd *tcfd;
	nfs_fh4 fh4;

	if (export->fsal_export->obj_ops->vec_open == NULL) {
		return NULL;
	}

	sids = calloc(count, sizeof(*sids));
	for (i = 0; i < count; ++i) {
		if (flags[i] & O_CREAT) {
			vset_up_creation(attrs + i, paths[i],
					   modes ? modes[i] : 0);
		} else {
			(attrs)[i].file = vfile_from_path(paths[i]);
		}
	}

	tcfs = calloc(count, sizeof(*tcfs));
	for (finished = 0; finished < count; ) {
		tcres = export->fsal_export->obj_ops->vec_open(
		    attrs + finished, count - finished, flags + finished,
		    sids + finished);
		if (!vokay(tcres)) {
			nfs4_closev(tcfs, finished);
			tcfs = NULL;
			goto exit;
		}

		for (i = finished; i < finished + tcres.index; ++i) {
			fh4.nfs_fh4_len = attrs[i].file.handle->handle_bytes;
			fh4.nfs_fh4_val =
			    (char *)attrs[i].file.handle->f_handle;
			tcfd =
			    tc_get_fd_struct(tc_alloc_fd(sids + i, &fh4), true);
			tcfd->filesize = attrs[i].size;
			tcfs[i] = vfile_from_fd(tcfd->fd);
			tc_put_fd_struct(&tcfd);
		}
		finished += tcres.index;
	}

exit:
	for (i = 0; i < count; ++i) {
		if (attrs[i].file.type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)attrs[i].file.handle);
		}
	}

	free(sids);
	return tcfs;
}

static int nfs4_close_impl(struct tc_kfd *tcfd, void *args)
{
	struct gsh_export *export = op_ctx->export;
	vres tcres;

	tcres = export->fsal_export->obj_ops->vec_close(
	    &tcfd->fh, 1, &tcfd->stateid, &tcfd->seqid);
	if (!vokay(tcres)) {
		return (int)EAGAIN;
	}

	tc_free_fd(tcfd->fd);
	return 0;
}

vres nfs4_closev(vfile *files, int count)
{
	struct gsh_export *export = op_ctx->export;
	vres tcres = { .index = count, .err_no = 0 };
	nfs_fh4 *fh4s;
	stateid4 *sids;
	seqid4 *seqs;
	int i;
	int n;
	struct tc_kfd *tcfd;
	int finished;

	fh4s = calloc(count, sizeof(*fh4s));
	sids = calloc(count, sizeof(*sids));
	seqs = calloc(count, sizeof(*seqs));

	n = 0;
	for (i = 0; i < count; ++i) {
		if (files[i].type != VFILE_NULL) {
			assert(files[i].type == VFILE_DESCRIPTOR);
			tcfd = tc_get_fd_struct(files[i].fd, false);
			fh4s[n] = tcfd->fh;
			sids[n] = tcfd->stateid;
			seqs[n++] = tcfd->seqid;
			tc_put_fd_struct(&tcfd);
		}
	}

	for (finished = 0; finished < n; ) {
		tcres = export->fsal_export->obj_ops->vec_close(
		    fh4s + finished, n - finished, sids + finished,
		    seqs + finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
		for (i = finished; i < tcres.index + finished; ++i) {
			tc_free_fd(files[i].fd);
			files[i].type = VFILE_NULL;
		}
		finished += tcres.index;
	}

	for (i = 0; i < count; ++i) {
		if (files[i].type != VFILE_NULL) {
			tc_free_fd(files[i].fd);
		}
	}
	free(seqs);
	free(sids);
	free(fh4s);
	return tcres;
}

off_t nfs4_fseek(vfile *tcf, off_t offset, int whence)
{
	struct tc_kfd *tcfd;

	assert(tcf->type == VFILE_DESCRIPTOR);
	tcfd = tc_get_fd_struct(tcf->fd, true);
	assert(tcfd);
	if (whence == SEEK_SET) {
		tcfd->offset = offset;
	} else if (whence == SEEK_CUR) {
		tcfd->offset += offset;
	} else if (whence == SEEK_END) {
		tcfd->offset = tcfd->filesize + offset;
	} else {
		assert(false);
	}
	offset = tcfd->offset;
	tc_put_fd_struct(&tcfd);

	return offset;
}

void nfs4_close_all()
{
	tc_for_each_fd(nfs4_close_impl, NULL);
}

static void nfs4_restore_vfiles(struct vattrs *attrs, int count,
				  vfile *saved_tcfs);

/**
 * Translate FD to file handle and save the original vfile.
 */
static vfile *nfs4_process_vfiles(struct vattrs *attrs, int count)
{
	int i;
	vfile *saved_tcfs;
	struct tc_kfd *tcfd;
	struct file_handle *h;
	vfile *tcf;

	saved_tcfs = calloc(count, sizeof(vfile));
	if (!saved_tcfs) {
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		tcf = &attrs[i].file;
		saved_tcfs[i] = *tcf;
		if (tcf->type == VFILE_DESCRIPTOR) {
			tcfd = tc_get_fd_struct(tcf->fd, false);
			if (!tcfd) {
				nfs4_restore_vfiles(attrs, --i, saved_tcfs);
				return NULL;
			}
			h = new_file_handle(tcfd->fh.nfs_fh4_len,
					    tcfd->fh.nfs_fh4_val);
			if (!h) {
				tc_put_fd_struct(&tcfd);
				nfs4_restore_vfiles(attrs, --i, saved_tcfs);
				return NULL;
			}
			tc_put_fd_struct(&tcfd);
			tcf->type = VFILE_HANDLE;
			tcf->handle = h;
		}
	}

	return saved_tcfs;
}

static void nfs4_restore_vfiles(struct vattrs *attrs, int count,
				  vfile *saved_tcfs)
{
	int i;

	for (i = 0; i < count; ++i) {
		if (saved_tcfs[i].type != attrs[i].file.type &&
		    attrs[i].file.type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)attrs[i].file.handle);
			// Restore only if we have type FD
			attrs[i].file = saved_tcfs[i];
		}
	}
	free(saved_tcfs);
}

vres nfs4_lgetattrsv(struct vattrs *attrs, int count, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres = { .index = count, .err_no = 0 };
	vfile *saved_tcfs;
	int finished;

	saved_tcfs = nfs4_process_vfiles(attrs, count);
	if (!saved_tcfs) {
		return vfailure(0, ENOMEM);
	}

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_lgetattrs(
		    attrs + finished, count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	nfs4_restore_vfiles(attrs, count, saved_tcfs);
	return tcres;
}

vres nfs4_lsetattrsv(struct vattrs *attrs, int count, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres = { .index = count, .err_no = 0 };
	vfile *saved_tcfs;
	int finished;

	saved_tcfs = nfs4_process_vfiles(attrs, count);
	if (!saved_tcfs) {
		return vfailure(0, ENOMEM);
	}

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_lsetattrs(
		    attrs + finished, count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	nfs4_restore_vfiles(attrs, count, saved_tcfs);
	return tcres;
}

vres nfs4_mkdirv(struct vattrs *dirs, int count, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres;
	vfile *saved_tcfs;
	int finished;

	saved_tcfs = nfs4_process_vfiles(dirs, count);
	if (!saved_tcfs) {
		return vfailure(0, ENOMEM);
	}

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_mkdir(dirs + finished,
							     count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	nfs4_restore_vfiles(dirs, count, saved_tcfs);

	return tcres;
}

vres nfs4_listdirv(const char **dirs, int count, struct vattrs_masks masks,
		     int max_entries, bool recursive, vec_listdir_cb cb,
		     void *cbarg, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres res;

	res = exp->fsal_export->obj_ops->vec_listdir(
	    dirs, count, masks, max_entries, recursive, cb, cbarg);

	return res;
}

vres nfs4_renamev(vfile_pair *pairs, int count, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres;
	int finished;

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_rename(pairs + finished,
							      count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

vres nfs4_removev(vfile *files, int count, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres = { .err_no = 0 };
	int finished;

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_remove(files + finished,
							      count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

vres nfs4_lcopyv(struct vextent_pair *pairs, int count, bool is_transaction)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres = { .err_no = 0 };
	int finished;

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_lcopy(pairs + finished,
							    count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

vres nfs4_hardlinkv(const char **oldpaths, const char **newpaths, int count,
		      bool istxn)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres;
	int finished;

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_hardlink(
		    oldpaths + finished, newpaths + finished, count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

vres nfs4_symlinkv(const char **oldpaths, const char **newpaths, int count,
		     bool istxn)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres;
	int finished;

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_symlink(
		    oldpaths + finished, newpaths + finished, count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

vres nfs4_readlinkv(const char **paths, char **bufs, size_t *bufsizes,
		      int count, bool istxn)
{
	struct gsh_export *exp = op_ctx->export;
	vres tcres;
	int finished;

	for (finished = 0; finished < count; finished += tcres.index) {
		tcres = exp->fsal_export->obj_ops->vec_readlink(
		    paths + finished, bufs + finished, bufsizes + finished,
		    count - finished);
		if (!vokay(tcres)) {
			tcres.index += finished;
			break;
		}
	}

	return tcres;
}

int nfs4_chdir(const char *path)
{
	struct gsh_export *exp = op_ctx->export;

	assert(exp->fullpath);
	if (strncmp(path, exp->fullpath, strlen(exp->fullpath)) != 0) {
		NFS4_ERR("cannot set TC working directory to %s because it is "
			 "outside of NFS export %s", path, exp->fullpath);
		return -EINVAL;
	}

	return exp->fsal_export->obj_ops->sca_chdir(path);
}

char *nfs4_getcwd()
{
	return op_ctx->fsal_export->obj_ops->sca_getcwd();
}
