#include "nfs4/tc_impl_nfs4.h"
#include "tc_nfs.h"

#include "../tc_cache/TC_MetaDataCache.h"
using namespace std;

TC_MetaDataCache<string, dirEntry *> *mdCache = NULL;

bool on_remove_metadata(dirEntry *de)
{
	if (de) {
		cout << "on_remove_metadata: " << de->path << "\n";
		delete de->attrs;
		delete de;
	} else {
		cout << "on_remove_metadata: dirEntry is NULL\n";
	}
	return true;
}

void init_page_cache()
{
	mdCache =
	    new TC_MetaDataCache<string, dirEntry *>(2, 60, on_remove_metadata);
}

tc_file *nfs_openv(const char **paths, int count, int *flags, mode_t *modes)
{
	return nfs4_openv(paths, count, flags, modes);
}

tc_res nfs_closev(tc_file *tcfs, int count)
{
	return nfs4_closev(tcfs, count);
}

off_t nfs_fseek(tc_file *tcf, off_t offset, int whence)
{
	return nfs4_fseek(tcf, offset, whence);
}

tc_res nfs_readv(struct tc_iovec *iovs, int count, bool istxn)
{
	return nfs4_readv(iovs, count, istxn);
}

tc_res nfs_writev(struct tc_iovec *writes, int write_count, bool is_transaction)
{
	return nfs4_writev(writes, write_count, is_transaction);
}

tc_res nfs_lgetattrsv(struct tc_attrs *attrs, int count, bool is_transaction)
{
	return nfs4_lgetattrsv(attrs, count, is_transaction);
}

tc_res nfs_lsetattrsv(struct tc_attrs *attrs, int count, bool is_transaction)
{
	return nfs4_lsetattrsv(attrs, count, is_transaction);
}

tc_res nfs_listdirv(const char **dirs, int count, struct tc_attrs_masks masks,
		    int max_entries, bool recursive, tc_listdirv_cb cb,
		    void *cbarg, bool is_transaction)
{
	return nfs4_listdirv(dirs, count, masks, max_entries, recursive, cb,
			     cbarg, is_transaction);
}

tc_res nfs_renamev(tc_file_pair *pairs, int count, bool is_transaction)
{
	return nfs4_renamev(pairs, count, is_transaction);
}

tc_res nfs_removev(tc_file *tc_files, int count, bool is_transaction)
{
	return nfs4_removev(tc_files, count, is_transaction);
}

tc_res nfs_mkdirv(struct tc_attrs *dirs, int count, bool is_transaction)
{
	return nfs4_mkdirv(dirs, count, is_transaction);
}

tc_res nfs_lcopyv(struct tc_extent_pair *pairs, int count, bool is_transaction)
{
	return nfs4_lcopyv(pairs, count, is_transaction);
}

tc_res nfs_hardlinkv(const char **oldpaths, const char **newpaths, int count,
		     bool istxn)
{
	return nfs4_hardlinkv(oldpaths, newpaths, count, istxn);
}

tc_res nfs_symlinkv(const char **oldpaths, const char **newpaths, int count,
		    bool istxn)
{
	return nfs4_symlinkv(oldpaths, newpaths, count, istxn);
}

tc_res nfs_readlinkv(const char **paths, char **bufs, size_t *bufsizes,
		     int count, bool istxn)
{
	return nfs4_readlinkv(paths, bufs, bufsizes, count, istxn);
}

int nfs_chdir(const char *path)
{
	return nfs4_chdir(path);
}

char *nfs_getcwd()
{
	return nfs4_getcwd();
}

