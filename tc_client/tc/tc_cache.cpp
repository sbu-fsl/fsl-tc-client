#include "nfs4/tc_impl_nfs4.h"
#include "tc_nfs.h"

#include "../tc_cache/TC_MetaDataCache.h"
using namespace std;

TC_MetaDataCache<string, SharedPtr<DirEntry> > *mdCache = NULL;
int g_miss_count = 0;

void reset_miss_count() {
	g_miss_count = 0;
}

int get_miss_count() {
	return g_miss_count;
}

void init_page_cache(uint64_t size, uint64_t time)
{
	mdCache = new TC_MetaDataCache<string, SharedPtr<DirEntry> >(
	    size, time);
}

void deinit_page_cache()
{
	mdCache->clear();
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

void fill_newAttr(struct tc_attrs *fAttrs, struct tc_attrs *sAttrs)
{
	memcpy((void *)&fAttrs->file, (void *)&sAttrs->file, sizeof(tc_file));
}

struct tc_attrs *getattr_check_pageCache(struct tc_attrs *sAttrs, int count,
				     int *miss_count, bool *hitArray)
{
	struct tc_attrs *final_attrs = NULL;
	struct tc_attrs *cur_sAttr = NULL;
	struct tc_attrs *cur_fAttr = NULL;
	
	int i = 0;

	final_attrs =
	    (struct tc_attrs *)malloc(sizeof(struct tc_attrs) * count);
	if (final_attrs == NULL)
		return NULL;

	while (i < count) {
		cur_sAttr = sAttrs + i;
		SharedPtr<DirEntry> *ptrElem =
		    mdCache->get(cur_sAttr->file.path);
		if (ptrElem) {
			/* Cache hit */
			pthread_rwlock_rdlock(&((*ptrElem)->attrLock));

			tc_stat2attrs((*ptrElem)->attrs, cur_sAttr);

			pthread_rwlock_unlock(&((*ptrElem)->attrLock));

			hitArray[count] = true;
		} else {
			/* fill final_array[miss_count] */
			cur_fAttr = final_attrs + *miss_count;
			fill_newAttr(cur_fAttr, cur_sAttr);
			cur_fAttr->masks = TC_MASK_INIT_ALL;
			(*miss_count)++;
		}

		i++;
	}

	return final_attrs;
}

void getattr_update_pageCache(struct tc_attrs *sAttrs, struct tc_attrs *final_attrs,
			  int count, bool *hitArray)
{
	int i = 0;
	int j = 0;
	struct tc_attrs *cur_sAttr = NULL;
	struct tc_attrs *cur_fAttr = NULL;

	while (i < count) {
		cur_sAttr = sAttrs + i;
		if (hitArray[i] == false) {
			cur_fAttr = final_attrs + j;

			SharedPtr<DirEntry> de1(
			    new DirEntry(cur_sAttr->file.path));
			de1->attrs = new struct stat();
			tc_attrs2stat(cur_fAttr, de1->attrs);

			pthread_rwlock_wrlock(&(de1->attrLock));

			mdCache->add(de1->path, de1);

			// Do we need to perform this with outside the
			// write lock?
			tc_stat2attrs(de1->attrs, cur_sAttr);

			pthread_rwlock_unlock(&(de1->attrLock));

			j++;
		}

		i++;
	}
}

tc_res nfs_lgetattrsv(struct tc_attrs *attrs, int count, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *saved_tcfs = NULL;
	struct tc_attrs *final_attrs = NULL;
	int miss_count = 0;
	bool *hitArray = NULL;

	saved_tcfs = nfs4_process_tc_files(attrs, count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}

	hitArray = new bool[count]();
	if (hitArray == NULL)
		goto mem_failure;

	std::fill_n(hitArray, count, false);

	final_attrs = getattr_check_pageCache(attrs, count, &miss_count, hitArray);
	if (final_attrs == NULL)
		goto mem_failure2;

	g_miss_count += miss_count;
	if (miss_count == 0) {
		/* Full cache hit */
		goto exit;
	}

	tcres = nfs4_lgetattrsv(final_attrs, miss_count, is_transaction);

	if (tc_okay(tcres)) {
		getattr_update_pageCache(attrs, final_attrs, count, hitArray);
	}

exit:
	delete hitArray;
	free(final_attrs);

	nfs4_restore_tc_files(attrs, count, saved_tcfs);

	return tcres;

mem_failure2:
	delete hitArray;
mem_failure:
	nfs4_restore_tc_files(attrs, count, saved_tcfs);
	return tc_failure(0, ENOMEM);
}

void setattr_update_pageCache(struct tc_attrs *sAttrs, int count)
{
	int i = 0;
	struct tc_attrs *cur_sAttr = NULL;

	while (i < count) {
		cur_sAttr = sAttrs + i;

		SharedPtr<DirEntry> *ptrElem =
		    mdCache->get(cur_sAttr->file.path);
		if (ptrElem) {
			/* Update cache */
			pthread_rwlock_wrlock(&((*ptrElem)->attrLock));

			tc_attrs2stat(cur_sAttr, (*ptrElem)->attrs);

			pthread_rwlock_unlock(&((*ptrElem)->attrLock));
		}

		i++;
	}
}

tc_res nfs_lsetattrsv(struct tc_attrs *attrs, int count, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };

	tcres = nfs4_lsetattrsv(attrs, count, is_transaction);

	if (tc_okay(tcres)) {
		setattr_update_pageCache(attrs, count);
	}

	return tcres;
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

