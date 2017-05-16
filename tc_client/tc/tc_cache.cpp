#include "nfs4/tc_impl_nfs4.h"
#include "tc_nfs.h"
#include "tc_helper.h"

#include "../tc_cache/TC_MetaDataCache.h"
#include "../tc_cache/TC_DataCache.h"

using namespace std;

TC_MetaDataCache<string, DirEntry > *mdCache = NULL;
TC_DataCache *dataCache = NULL;
unordered_map<int, string> *fd_to_path_map = NULL;;
int g_miss_count = 0;

void reset_miss_count() {
	g_miss_count = 0;
}

int get_miss_count() {
	return g_miss_count;
}

void init_page_cache(uint64_t size, uint64_t time)
{
	mdCache = new TC_MetaDataCache<string, DirEntry>(size, time);
}

void init_data_cache(uint64_t size, uint64_t time)
{
	dataCache = new TC_DataCache(size, time);
	fd_to_path_map = new unordered_map<int, string>();
}

void deinit_page_cache()
{
	mdCache->clear();
}

void deinit_data_cache()
{
	dataCache->clear();
}

void nfs_restore_FhToFilename(struct tc_attrs *attrs, int count,
			      tc_file *saved_tcfs);

struct file_handle *copyFH(const struct file_handle *old)
{
	size_t oldLen = old->handle_bytes;
	struct file_handle *fh = NULL;

	fh = (struct file_handle *)malloc(sizeof(*fh) + oldLen);
	if (fh) {
		fh->handle_bytes = oldLen;
		fh->handle_type = old->handle_type;
		memcpy(fh->f_handle, old->f_handle, oldLen);
	}

	return fh;
}

/*
 * Update fileHandle for single tc_file object
 */
void update_file(tc_file *tcf)
{
	struct file_handle *curHandle;
	struct file_handle *h;

	SharedPtr<DirEntry> ptrElem = mdCache->get(tcf->path);
	if (ptrElem.isNull()) {
		return;
	}

	pthread_rwlock_rdlock(&((ptrElem)->attrLock));
	curHandle = (struct file_handle *)(ptrElem)->fh;
	if(!curHandle) {
		pthread_rwlock_unlock(&((ptrElem)->attrLock));
		return;
	}

	h = copyFH(curHandle);
	pthread_rwlock_unlock(&((ptrElem)->attrLock));

	tcf->type = TC_FILE_HANDLE;
	tcf->handle = h;
}

/**
 * Translate filename to file handle and save the original tc_file.
 * For tc_attrs
 */
tc_file *nfs_updateAttr_FilenameToFh(struct tc_attrs *attrs, int count)
{
	int i;
	tc_file *saved_tcfs;
	tc_file *tcf;

	saved_tcfs = (tc_file *)malloc(sizeof(tc_file) * count);
	if (!saved_tcfs) {
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		tcf = &attrs[i].file;
		saved_tcfs[i] = *tcf;
		if (tcf->type == TC_FILE_PATH) {
			update_file(tcf);
		}
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For tc_attrs
 */
void nfs_restoreAttr_FhToFilename(struct tc_attrs *attrs, int count,
				  tc_file *saved_tcfs)
{
	int i;

	for (i = 0; i < count; ++i) {

		if (saved_tcfs[i].type == TC_FILE_PATH &&
		    attrs[i].file.type == TC_FILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)attrs[i].file.handle);
		}
		attrs[i].file = saved_tcfs[i];
	}

	free(saved_tcfs);
}

/**
 * Translate filename to file handle and save the original tc_file.
 * For tc_iovec
 */
tc_file *nfs_updateIovec_FilenameToFh(struct tc_iovec *iovs, int count)
{
	int i;
	tc_file *saved_tcfs;
	tc_file *tcf;

	saved_tcfs = (tc_file *)malloc(sizeof(tc_file) * count);
	if (!saved_tcfs) {
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		tcf = &iovs[i].file;
		saved_tcfs[i] = *tcf;
		if (tcf->type == TC_FILE_PATH) {
			update_file(tcf);
		}
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For tc_iovec
 */
void nfs_restoreIovec_FhToFilename(struct tc_iovec *iovs, int count,
				  tc_file *saved_tcfs)
{
	int i;

	for (i = 0; i < count; ++i) {
		if (saved_tcfs[i].type == TC_FILE_PATH &&
		    iovs[i].file.type == TC_FILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)iovs[i].file.handle);
		}
		iovs[i].file = saved_tcfs[i];
	}

	free(saved_tcfs);
}

/**
 * Validate memtadata cache by comparing the timestamp of cached entry with the
 * latest timestamp from the server side.
 * Return whether the metadata cache entry is valid.
 */
static bool validate_metacache(DirEntry *dentry, const struct tc_attrs *attrs)
{
	return timespec_diff(&dentry->attrs.st_ctim, &attrs->ctime) == 0;
}

tc_file *nfs_openv(const char **paths, int count, int *flags, mode_t *modes)
{
	struct tc_attrs *attrs;
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *file;

	file = nfs4_openv(paths, count, flags, modes, &attrs);

	for (int i = 0; i < tcres.index; i++) {
		SharedPtr<DirEntry> ptrElem = mdCache->get(paths[i]);
		if (!ptrElem.isNull()) {
			pthread_rwlock_wrlock(&((ptrElem)->attrLock));
			if (!validate_metacache(ptrElem.get(), &attrs[i])) {
				//Data cache contains stale data
				dataCache->remove(paths[i]);
			}
			//Update metadata cache
			tc_attrs2stat(&attrs[i], &(ptrElem)->attrs);
			ptrElem->timestamp = time(NULL);
			pthread_rwlock_unlock(&((ptrElem)->attrLock));
		}
		else {
			DirEntry de(paths[i]);
			tc_attrs2stat(&attrs[i], &de.attrs);
			de.fh = NULL;
			mdCache->add(de.path, de);
		}
		(*fd_to_path_map)[file[i].fd] = paths[i];
	}

	return file;
}

tc_res nfs_closev(tc_file *tcfs, int count)
{
	tc_res tcres = { .index = count, .err_no = 0 };

	tcres = nfs4_closev(tcfs, count);
	for (int i = 0; i < tcres.index; i++) {
		fd_to_path_map->erase(tcfs[i].fd);
	}

	return tcres;
}

off_t nfs_fseek(tc_file *tcf, off_t offset, int whence)
{
	return nfs4_fseek(tcf, offset, whence);
}

void fill_newIovec(struct tc_iovec *fiovec, struct tc_iovec *siovec)
{
	memcpy((void *) fiovec, (void *) siovec, sizeof(struct tc_iovec));
}

struct tc_iovec *check_dataCache(struct tc_iovec *siovec, int count,
				 int *miss_count, bool *hitArray)
{
	struct tc_iovec *final_iovec = NULL;
	struct tc_iovec *cur_siovec = NULL;
	struct tc_iovec *cur_fiovec = NULL;
	size_t *hits = NULL;
	int hit_count = 0;
	int i = 0;

	struct tc_attrs *attrs = (tc_attrs *)malloc(count*sizeof(tc_attrs));
	if (attrs == NULL)
		return NULL;
	final_iovec =
		(struct tc_iovec *)malloc(sizeof(struct tc_iovec) * count);
	if (final_iovec == NULL) {
		free(attrs);
		return NULL;
	}
	hits = (size_t *) malloc(sizeof(size_t)*count);
	if (hits == NULL) {
		free(attrs);
		free(final_iovec);
		return NULL;
	}

	for(i = 0; i < count; i++) {
		cur_siovec = siovec + i;
		if (cur_siovec->file.type != TC_FILE_PATH &&
			cur_siovec->file.type != TC_FILE_DESCRIPTOR) {
			hits[i] = 0;
			continue;
		}
		if (cur_siovec->file.type == TC_FILE_DESCRIPTOR) {
			unordered_map<int, string>::iterator it =
				fd_to_path_map->find(cur_siovec->file.fd);
			if (it != fd_to_path_map->end()) {
				cur_siovec->file.path = it->second.c_str();
			}
			else
				continue;
		}
		int hit =
		    dataCache->get(cur_siovec->file.path, cur_siovec->offset,
				   cur_siovec->length, cur_siovec->data);
		if (hit == 0) {
			hits[i] = 0;
			continue;
		}
		SharedPtr<DirEntry> ptrElem = mdCache->get(cur_siovec->file.path);
		if (!ptrElem.isNull()) {
			printf("Data Cache hit. MDCache hit.\n");
			//Add to getattrs
			hits[i] = hit;
			attrs[hit_count].file = cur_siovec->file;
			attrs[hit_count].masks = TC_ATTRS_MASK_ALL; 
			hit_count++;
		}
		else {
			printf("Data Cache hit. MDCache miss.\n");
			hits[i] = 0;
			//Remove from data cache?
		}
	}
	//Do getattrs
	if (hit_count != 0) {
		tc_res tcres = { .index = hit_count, .err_no = 0 };
		tcres = nfs4_lgetattrsv(attrs, hit_count, false);
		int l = 0;
		if (tc_okay(tcres)) {
			//Compare attrs in loop. If ctime is different, set hits[i] to 0.
			for (int k = 0; k < count; k++) {
				if (hits[k] == 0)
					continue;
				cur_siovec = siovec + k;
				SharedPtr<DirEntry> ptrElem = mdCache->get(cur_siovec->file.path);
				if (ptrElem.isNull() || ptrElem->attrs.st_ctim.tv_sec !=
					attrs[l].ctime.tv_sec || ptrElem->attrs.st_ctim.tv_nsec !=
					attrs[l].ctime.tv_nsec) {
					hits[k] = 0;
				}
				else if (ptrElem->attrs.st_size <= cur_siovec->offset + cur_siovec->length){
					cur_siovec->is_eof = true;
				}
				l++;
			}
		}
		else {
			goto exit;
		}
	}
	i = 0;
	while (i < count) {
		cur_siovec = siovec + i;
		if (hits[i] == 0) {
			/* fill final_array[miss_count] */
			cur_fiovec = final_iovec + *miss_count;
			fill_newIovec(cur_fiovec, cur_siovec);
			if (i > 0 && hitArray[i-1] == true &&
				cur_fiovec->file.type == TC_FILE_CURRENT) {
				char *new_path = (char *) malloc(strlen(siovec[i-1].file.path) +
						strlen(siovec[i].file.path) + 2);
				sprintf(new_path, "%s/%s", siovec[i-1].file.path, siovec[i].file.path);
				cur_fiovec->file.path = new_path;
				cur_fiovec->file.type = TC_FILE_PATH;
			}
			(*miss_count)++;
			i++;
			continue;
		}
		if (hits[i] == cur_siovec->length) {
                        /* Cache hit */
			hitArray[i] = true;
		}
		else if (hits[i] > 0) {
			/*Handle partial hit*/
			cur_fiovec = final_iovec + *miss_count;
			fill_newIovec(cur_fiovec, cur_siovec);
			cur_fiovec->offset = hits[i];
			cur_fiovec->length = cur_fiovec->length - hits[i];
			cur_fiovec->data = cur_fiovec->data + hits[i];
			(*miss_count)++;
		}
		else {
			cur_fiovec = final_iovec + *miss_count;
			fill_newIovec(cur_fiovec, cur_siovec);
			(*miss_count)++;
		}
		i++;
	}
exit:
	free(attrs);
	free(hits);

	return final_iovec;
}

void update_dataCache(struct tc_iovec *siovec,
			struct tc_iovec *final_iovec, int count,
			bool *hitArray)
{
        int i = 0;
        int j = 0;
        struct tc_iovec *cur_siovec = NULL;
        struct tc_iovec *cur_fiovec = NULL;
	int t_len = 0;
	int t_offset = 0;
	unordered_map<int, string>::iterator it;

        while (i < count) {
                cur_siovec = siovec + i;
		if (hitArray[i] == false && 
			cur_siovec->file.type == TC_FILE_DESCRIPTOR) {
			cur_fiovec = final_iovec + j;
			it = fd_to_path_map->find(cur_siovec->file.fd);
			if (it != fd_to_path_map->end()) {
				cur_fiovec->file.path = it->second.c_str();
			}
		}
                if (hitArray[i] == false && (cur_siovec->file.type == TC_FILE_PATH ||
			it != fd_to_path_map->end())) {
                        cur_fiovec = final_iovec + j;

                        dataCache->put(cur_fiovec->file.path, cur_fiovec->offset,
					 cur_fiovec->length, cur_fiovec->data);
			cur_siovec->length = cur_fiovec->length + cur_fiovec->offset - 
						cur_siovec->offset;
			cur_siovec->is_failure = cur_fiovec->is_failure;
			cur_siovec->is_eof = cur_fiovec->is_eof;
			cur_siovec->is_write_stable = cur_fiovec->is_write_stable;
                        //memcpy(cur_siovec, cur_fiovec, sizeof(struct tc_iovec));
			
                        j++;
                }
                else if (hitArray[i] == false) {
                        cur_fiovec = final_iovec + j;
			memcpy(cur_siovec, cur_fiovec, sizeof(struct tc_iovec));
                        j++;
                }

                i++;
        }
}

tc_res nfs_readv(struct tc_iovec *iovs, int count, bool istxn)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *saved_tcfs = NULL;
	bool *hitArray = NULL;
	int miss_count = 0;
	tc_iovec *final_iovec = NULL;
	struct tc_attrs *attrs = NULL;

	saved_tcfs = nfs_updateIovec_FilenameToFh(iovs, count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}
	hitArray = new bool[count]();
	if (hitArray == NULL)
		return tc_failure(0, ENOMEM);

	std::fill_n(hitArray, count, false);

	final_iovec = check_dataCache(iovs, count, &miss_count, hitArray);
	if (final_iovec == NULL)
		goto mem_failure2;

	g_miss_count += miss_count;
	if (miss_count == 0) {
		/* Full cache hit */
		goto exit;
	}
	attrs = (struct tc_attrs *) malloc(miss_count * sizeof(struct tc_attrs));
	tcres = nfs4_readv(final_iovec, miss_count, istxn, attrs);
	if (tc_okay(tcres)) {
		for (int i = 0; i < miss_count; i++) {
			if (final_iovec[i].file.type == TC_FILE_PATH || 
				final_iovec[i].file.type == TC_FILE_DESCRIPTOR) {
				if (final_iovec[i].file.type == TC_FILE_DESCRIPTOR) {
					unordered_map<int, string>::iterator it =
						fd_to_path_map->find(final_iovec[i].file.fd);
					if (it != fd_to_path_map->end())
						final_iovec[i].file.path = it->second.c_str();
					else
						continue;
				}
				DirEntry de(final_iovec[i].file.path);
				tc_attrs2stat(&attrs[i], &de.attrs);
				de.fh = NULL;
				mdCache->add(de.path, de);
			}
		}
		update_dataCache(iovs, final_iovec, count, hitArray);
	}

	nfs_restoreIovec_FhToFilename(iovs, count, saved_tcfs);

exit:
	delete hitArray;
	free(final_iovec);
	free(attrs);
	return tcres;

mem_failure2:
	delete hitArray;
	return tc_failure(0, ENOMEM);;
}

tc_res check_and_remove(struct tc_iovec *writes, int write_count)
{
	int hit;
	int hit_count = 0;
	int *hits = (int *) malloc(write_count * sizeof(int));
	struct tc_attrs *attrs =
	    (tc_attrs *)malloc(write_count * sizeof(struct tc_attrs));
	tc_res tcres = { .index = hit_count, .err_no = 0 };
	if (attrs == NULL) {
		free(hits);
		return tcres;
	}
	SharedPtr<DirEntry> *ptrElem = new SharedPtr<DirEntry>[write_count]();
	if (ptrElem == NULL) {
		return tcres;
	}

	for (int i = 0; i < write_count; i++) {
		//Read from md cache
		if (writes[i].file.type != TC_FILE_DESCRIPTOR &&
		    writes[i].file.type != TC_FILE_PATH) {
			hits[i] = 0;
			continue;
		}
		if (writes[i].file.type == TC_FILE_DESCRIPTOR) {
			unordered_map<int, string>::iterator it =
				fd_to_path_map->find(writes[i].file.fd);
			if (it != fd_to_path_map->end()) {
				writes[i].file.path = it->second.c_str();
			}
			else{
				hits[i] = 0;
				continue;
			}
		}
		ptrElem[i] = mdCache->get(writes[i].file.path);
		if (!ptrElem[i].isNull() &&
		    dataCache->isCached(writes[i].file.path)) {
			//If present in both add to getattrs
			hits[i] = 1;
			attrs[hit_count].file = writes[i].file;
			attrs[hit_count].masks = TC_ATTRS_MASK_ALL;
			hit_count++;
		} else {
			hits[i] = 0;
		}
	}
	if (hit_count != 0) {
		tcres = { .index = hit_count, .err_no = 0 };
		tcres = nfs4_lgetattrsv(attrs, hit_count, false);
		int l = 0;
		if (!tc_okay(tcres)) {
			free(hits);
			free(attrs);
			delete[] ptrElem;
			return tcres;
		}
		for (int k = 0; k < write_count; k++) {
			if (hits[k] == 0)
				continue;
			if (!validate_metacache(ptrElem[l].get(), &attrs[l])) {
				dataCache->remove(writes[k].file.path);
			}
		}
	}
	free(hits);
	free(attrs);
	delete[] ptrElem;

	return tcres;
}

tc_res nfs_writev(struct tc_iovec *writes, int write_count, bool is_transaction)
{
	tc_res tcres = { .index = write_count, .err_no = 0 };
	tc_file *saved_tcfs = NULL;
	struct tc_attrs *attrs = NULL;

	saved_tcfs = nfs_updateIovec_FilenameToFh(writes, write_count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}
	attrs = (struct tc_attrs *) malloc(write_count* 
					sizeof(struct tc_attrs));
	tcres = check_and_remove(writes, write_count);
	if (!tc_okay(tcres))
	{
		free(attrs);
		return tcres;
	}
	tcres = nfs4_writev(writes, write_count, is_transaction, attrs);

	if (tc_okay(tcres)) {
		for (int i = 0; i < write_count; i++) {
			if (writes[i].file.type == TC_FILE_DESCRIPTOR) {
				unordered_map<int, string>::iterator it =
                                  fd_to_path_map->find(writes[i].file.fd);
				if (it != fd_to_path_map->end()) {
					writes[i].file.path = it->second.c_str();
				}
				else
					continue;
			}
			if (writes[i].file.type == TC_FILE_PATH ||
				writes[i].file.type == TC_FILE_DESCRIPTOR) {
				DirEntry de(writes[i].file.path);
				tc_attrs2stat(&attrs[i], &de.attrs);
				de.fh = NULL;
				mdCache->add(de.path, de);
			}
			if (writes[i].file.type == TC_FILE_PATH ||
				writes[i].file.type == TC_FILE_DESCRIPTOR) {
				if (writes[i].offset != TC_OFFSET_END &&
					writes[i].offset != TC_OFFSET_CUR) {
					dataCache->put(writes[i].file.path,
							writes[i].offset,
							writes[i].length,
							writes[i].data);
				}
				else if (writes[i].offset == TC_OFFSET_END){
					dataCache->put(writes[i].file.path,
							attrs[i].size - writes[i].length,
							writes[i].length,
							writes[i].data);
				}
				else if (writes[i].offset == TC_OFFSET_CUR) {
					//Can this be handled?
				}
			}
		}
	}

	nfs_restoreIovec_FhToFilename(writes, write_count, saved_tcfs);
	free(attrs);

	return tcres;
}

void fill_newAttr(struct tc_attrs *fAttrs, struct tc_attrs *sAttrs)
{
	memcpy((void *)&fAttrs->file, (void *)&sAttrs->file, sizeof(tc_file));
}

struct tc_attrs *getattr_check_pagecache(struct tc_attrs *sAttrs, int count,
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
		if (cur_sAttr->file.type != TC_FILE_PATH) {
			/* fill final_array[miss_count] */
			cur_fAttr = final_attrs + *miss_count;
			fill_newAttr(cur_fAttr, cur_sAttr);
			cur_fAttr->masks = TC_MASK_INIT_ALL;
			if (hitArray[i - 1] == true &&
			    cur_fAttr->file.type == TC_FILE_CURRENT) {
				char *new_path = (char *)malloc(
				    strlen(sAttrs[i - 1].file.path) +
				    strlen(sAttrs[i].file.path) + 2);
				sprintf(new_path, "%s/%s",
					sAttrs[i - 1].file.path,
					sAttrs[i].file.path);
				cur_fAttr->file.path = new_path;
				cur_fAttr->file.type = TC_FILE_PATH;
			}
			(*miss_count)++;
			i++;
			continue;
		}
		SharedPtr<DirEntry> ptrElem =
		    mdCache->get(cur_sAttr->file.path);
		if (!ptrElem.isNull() &&
		    (time(NULL) - ptrElem->timestamp <= MD_REFRESH_TIME)) {
			/* Cache hit */
			pthread_rwlock_rdlock(&((ptrElem)->attrLock));

			tc_stat2attrs(&(ptrElem)->attrs, cur_sAttr);

			pthread_rwlock_unlock(&((ptrElem)->attrLock));

			hitArray[i] = true;
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

void getattr_update_pagecache(struct tc_attrs *sAttrs,
			      struct tc_attrs *final_attrs, int count,
			      bool *hitArray)
{
	int i = 0;
	int j = 0;
	struct tc_attrs *cur_sAttr = NULL;
	struct tc_attrs *cur_fAttr = NULL;

	while (i < count) {
		cur_sAttr = sAttrs + i;
		if (hitArray[i] == false && cur_sAttr->file.type == TC_FILE_PATH) {
			cur_fAttr = final_attrs + j;

			DirEntry de1(cur_sAttr->file.path);
			tc_attrs2stat(cur_fAttr, &de1.attrs);
			de1.fh = NULL;

			pthread_rwlock_wrlock(&(de1.attrLock));

			mdCache->add(de1.path, de1);

			// Do we need to perform this with outside the
			// write lock?
			tc_stat2attrs(&de1.attrs, cur_sAttr);

			pthread_rwlock_unlock(&(de1.attrLock));

			j++;
		}
		else if (hitArray[i] == false) {
			cur_fAttr = final_attrs + j;
			tc_attrs2attrs(cur_sAttr, cur_fAttr); 
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
	int j = 0;

	hitArray = new bool[count]();
	if (hitArray == NULL)
		return tc_failure(0, ENOMEM);

	std::fill_n(hitArray, count, false);

	final_attrs = getattr_check_pagecache(attrs, count, &miss_count, hitArray);
	if (final_attrs == NULL)
		goto mem_failure2;

	g_miss_count += miss_count;
	if (miss_count == 0) {
	/* Full cache hit */
		goto exit;
	}

	tcres = nfs4_lgetattrsv(final_attrs, miss_count, is_transaction);

	if (tc_okay(tcres)) {
		getattr_update_pagecache(attrs, final_attrs, count, hitArray);
	}

exit:
	for (int i = 0; i < count; i++) {
		if (hitArray[i] == false) {
			if (attrs[i].file.type == TC_FILE_CURRENT &&
				final_attrs[j].file.type == TC_FILE_PATH)
				free((void *) final_attrs[j].file.path);
			j++;
		}
	}
	delete hitArray;
	free(final_attrs);

	return tcres;

mem_failure2:
	delete hitArray;
	return tc_failure(0, ENOMEM);
}

static void setattr_update_pagecache(struct tc_attrs *sAttrs, int count)
{
	int i = 0;
	struct tc_attrs *cur_sAttr = NULL;

	while (i < count) {
		cur_sAttr = sAttrs + i;
		if (cur_sAttr->file.type != TC_FILE_PATH) {
			i++;
			continue;
		}
		SharedPtr<DirEntry> ptrElem =
		    mdCache->get(cur_sAttr->file.path);
		if (!ptrElem.isNull()) {
			/* Update cache */
			pthread_rwlock_wrlock(&((ptrElem)->attrLock));

			tc_attrs2stat(cur_sAttr, &(ptrElem)->attrs);
			ptrElem->timestamp = time(NULL);

			pthread_rwlock_unlock(&((ptrElem)->attrLock));
		}

		i++;
	}
}

tc_res nfs_lsetattrsv(struct tc_attrs *attrs, int count, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *saved_tcfs;

	saved_tcfs = nfs_updateAttr_FilenameToFh(attrs, count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}

	tcres = nfs4_lsetattrsv(attrs, count, is_transaction);
	nfs_restoreAttr_FhToFilename(attrs, count, saved_tcfs);
	if (tc_okay(tcres)) {
		setattr_update_pagecache(attrs, count);
	}
	
	return tcres;
}

struct listDirPxy
{
	tc_listdirv_cb cb;
	void *cbarg;
	struct tc_attrs_masks masks;
};

static bool poco_direntry(const struct tc_attrs *dentry, const char *dir,
			  void *cbarg)
{
	struct listDirPxy *temp = (struct listDirPxy *)cbarg;
	struct tc_attrs finalDentry;

	SharedPtr<DirEntry> parentElem = mdCache->get(dir);
	if (!parentElem.isNull()) {
		SharedPtr<DirEntry> ptrElem = mdCache->get(dentry->file.path);
		if (ptrElem.isNull()) {
			DirEntry de1(dentry->file.path);
			de1.fh = NULL;
			tc_attrs2stat(dentry, &de1.attrs);

			pthread_rwlock_wrlock(&(de1.attrLock));
			
			mdCache->add(de1.path, de1);
			de1.parent = parentElem;
			pthread_rwlock_unlock(&(de1.attrLock));
			ptrElem = mdCache->get(dentry->file.path);
		} else {
			pthread_rwlock_wrlock(&((ptrElem)->attrLock));
			tc_attrs2stat(dentry, &(ptrElem)->attrs);
			(ptrElem)->parent = parentElem;
			pthread_rwlock_unlock(&((ptrElem)->attrLock));
		}

		pthread_rwlock_wrlock(&((parentElem)->attrLock));
		(parentElem)->children[dentry->file.path] = ptrElem;
		pthread_rwlock_unlock(&((parentElem)->attrLock));
	}

	finalDentry.masks = temp->masks;
	tc_attrs2attrs(&finalDentry, dentry);
	(temp->cb)(&finalDentry, dir, temp->cbarg);

	return true;
}

static const char **listdir_check_pagecache(bool *hitArray, const char **dirs,
					    int count, int *miss_count)
{
	const char **finalDirs;
	int i = 0;
	int j = 0;
	const char *curDir = NULL;
	SharedPtr<DirEntry> curElem;

	finalDirs = (const char**)malloc(count * sizeof(char*));
	if (!finalDirs) {
		return NULL;
	}

	while (i < count) {
		curDir = dirs[i];
		curElem = mdCache->get(curDir);
		if (curElem.isNull() || !(curElem)->has_listdir ||
		    (time(NULL) - curElem->timestamp > MD_REFRESH_TIME)) {
			(*miss_count)++;
			finalDirs[j++] = curDir;
		} else {
			hitArray[i] = true;
		}

		i++;
	}

	return finalDirs;
}

void invoke_callback(const char *curDir, SharedPtr<DirEntry> &curElem,
		     tc_listdirv_cb cb, void *cbarg,
		     struct tc_attrs_masks masks)
{
	SharedPtr<DirEntry> fileEntry = NULL;
	struct tc_attrs finalDentry;

	for (const auto &mypair : (curElem)->children) {
		fileEntry = mypair.second;
		finalDentry.masks = masks;
		finalDentry.file = tc_file_from_path((fileEntry)->path.c_str());
		tc_stat2attrs(&(fileEntry)->attrs, &finalDentry);

		cb(&finalDentry, curDir, cbarg);
	}
}

void reply_from_pagecache(const char **dirs, int count, bool *hitArray,
			  tc_listdirv_cb cb, void *cbarg,
			  struct tc_attrs_masks masks)
{
	int i = 0;
	SharedPtr<DirEntry> curElem;
	const char *curDir = NULL;

	while (i < count) {
		curDir = dirs[i];
		curElem = mdCache->get(curDir);
		if (!curElem.isNull()) {
			(curElem)->has_listdir = true;
			if (hitArray[i] == true) {
				invoke_callback(dirs[i], curElem, cb, cbarg, masks);
			}
		}

		i++;
	}
}

tc_res nfs_listdirv(const char **dirs, int count, struct tc_attrs_masks masks,
		    int max_entries, bool recursive, tc_listdirv_cb cb,
		    void *cbarg, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	struct listDirPxy *temp = NULL;
	const char **finalDirs;
	int miss_count;
	bool *hitArray = NULL;
	temp = new listDirPxy;
	if (temp == NULL)
		goto failure;

	hitArray = new bool[count]();
	if (hitArray == NULL)
		goto bool_failure;

	std::fill_n(hitArray, count, false);

	finalDirs = listdir_check_pagecache(hitArray, dirs, count, &miss_count);

	if (miss_count > 0) {
		temp->cb = cb;
		temp->cbarg = cbarg;
		temp->masks = masks;
		masks = TC_MASK_INIT_ALL;
		tcres = nfs4_listdirv(finalDirs, miss_count, masks, max_entries,
				      recursive, poco_direntry, temp,
				      is_transaction);
	}

	reply_from_pagecache(dirs, count, hitArray, cb, cbarg, masks);

dir_failure:
	delete hitArray;
bool_failure:
	delete temp;
failure:
	return tcres;
}

/**
 * Translate filename to file handle and save the original tc_file.
 * For tc_file_pair
 */
tc_file *nfs_updatePair_FilenameToFh(tc_file_pair *pairs, int count)
{
	int i, j;
	tc_file *saved_tcfs;
	tc_file *tcf;

	saved_tcfs = (tc_file *)malloc(sizeof(tc_file) * count * 2);
	if (!saved_tcfs) {
		return NULL;
	}

	for (i = 0, j=0; i < count; ++i) {
		tcf = &pairs[i].src_file;
		saved_tcfs[j] = *tcf;
		if (tcf->type == TC_FILE_PATH) {
			update_file(tcf);
		}

		j++;

		tcf = &pairs[i].dst_file;
		saved_tcfs[j] = *tcf;
		if (tcf->type == TC_FILE_PATH) {
			update_file(tcf);
		}

		j++;
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For tc_file_pair
 */
void nfs_restorePair_FhToFilename(tc_file_pair *pairs, int count,
				  tc_file *saved_tcfs)
{
	int i, j;

	for (i = 0, j = 0; i < count; ++i) {
		if (saved_tcfs[j].type == TC_FILE_PATH &&
		    pairs[i].src_file.type == TC_FILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)pairs[i].src_file.handle);
		}
		pairs[i].src_file = saved_tcfs[j];
		j++;

		if (saved_tcfs[j].type == TC_FILE_PATH &&
		    pairs[i].dst_file.type == TC_FILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)pairs[i].dst_file.handle);
		}
		pairs[i].dst_file = saved_tcfs[j];
		j++;
	}

	free(saved_tcfs);
}

tc_res nfs_renamev(tc_file_pair *pairs, int count, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *saved_tcfs;

	saved_tcfs = nfs_updatePair_FilenameToFh(pairs, count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}

	tcres = nfs4_renamev(pairs, count, is_transaction);

	nfs_restorePair_FhToFilename(pairs, count, saved_tcfs);

	return tcres;
}

/**
 * Translate filename to file handle and save the original tc_file.
 * For tc_file
 */
tc_file *nfs_updateFile_FilenameToFh(tc_file *tc_files, int count)
{
	int i;
	tc_file *saved_tcfs;
	tc_file *tcf;

	saved_tcfs = (tc_file *)malloc(sizeof(tc_file) * count * 2);
	if (!saved_tcfs) {
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		tcf = &tc_files[i];
		saved_tcfs[i] = *tcf;
		if (tcf->type == TC_FILE_PATH) {
			update_file(tcf);
		}
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For tc_file
 */
void nfs_restoreFile_FhToFilename(tc_file *tc_files, int count,
				  tc_file *saved_tcfs)
{
	int i;

	for (i = 0; i < count; ++i) {
		if (saved_tcfs[i].type == TC_FILE_PATH &&
		    tc_files[i].type == TC_FILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)tc_files[i].handle);
		}
		tc_files[i] = saved_tcfs[i];
	}

	free(saved_tcfs);
}

tc_res nfs_removev(tc_file *tc_files, int count, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *saved_tcfs;

	saved_tcfs = nfs_updateFile_FilenameToFh(tc_files, count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}

	tcres = nfs4_removev(tc_files, count, is_transaction);

	nfs_restoreFile_FhToFilename(tc_files, count, saved_tcfs);

	if (tc_okay(tcres)) {
		for (int i = 0; i < count; i++) {
			if (tc_files[i].type == TC_FILE_PATH) {
				dataCache->remove(tc_files[i].path);
				mdCache->remove(tc_files[i].path);
			}
		}
	}
	return tcres;
}

tc_res nfs_mkdirv(struct tc_attrs *dirs, int count, bool is_transaction)
{
	tc_res tcres = { .index = count, .err_no = 0 };
	tc_file *saved_tcfs;

	saved_tcfs = nfs_updateAttr_FilenameToFh(dirs, count);
	if (!saved_tcfs) {
		return tc_failure(0, ENOMEM);
	}

	tcres = nfs4_mkdirv(dirs, count, is_transaction);

	nfs_restoreAttr_FhToFilename(dirs, count, saved_tcfs);

	return tcres;
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

