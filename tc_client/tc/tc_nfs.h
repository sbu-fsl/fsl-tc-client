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

tc_file *nfs_openv(const char **paths, int count, int *flags, mode_t *modes);

tc_res nfs_closev(tc_file *tcfs, int count);

off_t nfs_fseek(tc_file *tcf, off_t offset, int whence);

tc_res nfs_writev(struct tc_iovec *writes, int write_count,
		   bool is_transaction);

tc_res nfs_lsetattrsv(struct tc_attrs *attrs, int count, bool is_transaction);

tc_res nfs_renamev(tc_file_pair *pairs, int count, bool is_transaction);

tc_res nfs_removev(tc_file *tc_files, int count, bool is_transaction);

tc_res nfs_mkdirv(struct tc_attrs *dirs, int count, bool is_transaction);

tc_res nfs_listdirv(const char **dirs, int count, struct tc_attrs_masks masks,
                     int max_entries, bool recursive, tc_listdirv_cb cb,
                     void *cbarg, bool is_transaction);

tc_res nfs_lcopyv(struct tc_extent_pair *pairs, int count, bool is_transaction);

tc_res nfs_hardlinkv(const char **oldpaths, const char **newpaths, int count,
                      bool istxn);

tc_res nfs_symlinkv(const char **oldpaths, const char **newpaths, int count,
                     bool istxn);

tc_res nfs_readlinkv(const char **paths, char **bufs, size_t *bufsizes,
                      int count, bool istxn);

tc_res nfs_readv(struct tc_iovec *iovs, int count, bool istxn);

tc_res nfs_lgetattrsv(struct tc_attrs *attrs, int count, bool is_transaction);

int nfs_chdir(const char *path);

char *nfs_getcwd();

