/*
 * APIs to call nfs implementations
 * tc_* functions should call nfs_* functions
 * Depending on the implementation (might be nfsv3, nfsv4, nfsv4.1, etc)
 * nfs_* will call the right APIs
 *
 * If you are implementing a newer version of nfs client, make sure
 * you are calling from nfs_* functions
 *
 * Since calls to any nfs implementation goes via nfs_*,
 * this is the right place to use page cache. So irrespective of the nfs
 * implementation, the page cache will be used/updated in nfs_* functions.
 */

/*
 * Initialize POCO page cache
 */
void init_page_cache(uint64_t size, uint64_t time);

/*
 * De-Initialize POCO page cache
 */
void deinit_page_cache();

void init_data_cache(uint64_t size, uint64_t time);

void deinit_data_cache();

vfile *nfs_openv(const char **paths, int count, int *flags, mode_t *modes);

vres nfs_closev(vfile *tcfs, int count);

off_t nfs_fseek(vfile *tcf, off_t offset, int whence);

vres nfs_writev(struct viovec *writes, int write_count,
		   bool is_transaction);

vres nfs_lsetattrsv(struct vattrs *attrs, int count, bool is_transaction);

vres nfs_renamev(vfile_pair *pairs, int count, bool is_transaction);

vres nfs_removev(vfile *vfiles, int count, bool is_transaction);

vres nfs_mkdirv(struct vattrs *dirs, int count, bool is_transaction);

vres nfs_listdirv(const char **dirs, int count, struct vattrs_masks masks,
                     int max_entries, bool recursive, vec_listdir_cb cb,
                     void *cbarg, bool is_transaction);

vres nfs_lcopyv(struct vextent_pair *pairs, int count, bool is_transaction);

vres nfs_hardlinkv(const char **oldpaths, const char **newpaths, int count,
                      bool istxn);

vres nfs_symlinkv(const char **oldpaths, const char **newpaths, int count,
                     bool istxn);

vres nfs_readlinkv(const char **paths, char **bufs, size_t *bufsizes,
                      int count, bool istxn);

vres nfs_readv(struct viovec *iovs, int count, bool istxn);

vres nfs_lgetattrsv(struct vattrs *attrs, int count, bool is_transaction);

int nfs_chdir(const char *path);

char *nfs_getcwd();

