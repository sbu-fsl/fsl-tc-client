#include <mutex>
#include <vector>

#include "nfs4/tc_impl_nfs4.h"
#include "tc_nfs.h"
#include "tc_helper.h"
#include "path_utils.h"


#include "../tc_cache/TC_MetaDataCache.h"
#include "../tc_cache/TC_DataCache.h"

using namespace std;

TC_MetaDataCache<string, DirEntry > *mdCache = NULL;
TC_DataCache *dataCache = NULL;

// TODO: merge fd_to_path_map into tc_kfd
unordered_map<int, string> *fd_to_path_map = NULL;;
std::mutex *fd_to_path_mutex = nullptr;

// TODO: make this thread-safe
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
	fd_to_path_mutex = new std::mutex();
}

void deinit_page_cache()
{
	mdCache->clear();
}

void deinit_data_cache()
{
	dataCache->clear();
	delete fd_to_path_map;
	delete fd_to_path_mutex;
}

static void add_fd_to_path(int fd, const char *path)
{
	std::lock_guard<std::mutex> lock(*fd_to_path_mutex);
	(*fd_to_path_map)[fd] = path;
}

static const char* get_path_from_fd(int fd)
{
	std::lock_guard<std::mutex> lock(*fd_to_path_mutex);
	auto it = fd_to_path_map->find(fd);
	return it == fd_to_path_map->end() ? nullptr : it->second.data();
}

static void clear_fd_to_path(int fd)
{
	std::lock_guard<std::mutex> lock(*fd_to_path_mutex);
	fd_to_path_map->erase(fd);
}

/*
 * Update fileHandle for single vfile object
 */
void metacache_path_to_handle(vfile *tcf)
{
	struct file_handle *h;

	SharedPtr<DirEntry> ptrElem = mdCache->get(tcf->path);
	if (!ptrElem.isNull() && (h = ptrElem->copyFileHandle()) != nullptr) {
		tcf->type = VFILE_HANDLE;
		tcf->handle = h;
	}
}

/**
 * Translate filename to file handle and save the original vfile.
 * For vattrs
 */
vector<vfile> nfs_updateAttr_FilenameToFh(struct vattrs *attrs, int count)
{
	vector<vfile> saved_tcfs(count);

	for (int i = 0; i < count; ++i) {
		vfile *tcf = &attrs[i].file;
		saved_tcfs[i] = *tcf;
		if (tcf->type == VFILE_PATH) {
			metacache_path_to_handle(tcf);
		}
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For vattrs
 */
void nfs_restoreAttr_FhToFilename(struct vattrs *attrs, int count,
				  const vector<vfile> &saved_tcfs)
{
	for (int i = 0; i < count; ++i) {
		if (saved_tcfs[i].type == VFILE_PATH &&
		    attrs[i].file.type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)attrs[i].file.handle);
		}
		attrs[i].file = saved_tcfs[i];
	}
}

/**
 * Translate filename to file handle and save the original vfile.
 * For viovec
 */
vector<vfile> nfs_updateIovec_FilenameToFh(struct viovec *iovs, int count)
{
	vector<vfile> saved_tcfs(count);
	vfile *tcf;

	for (int i = 0; i < count; ++i) {
		tcf = &iovs[i].file;
		saved_tcfs[i] = *tcf;
		if (tcf->type == VFILE_PATH) {
			metacache_path_to_handle(tcf);
		}
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For viovec
 */
void nfs_restoreIovec_FhToFilename(struct viovec *iovs, int count,
				   const vector<vfile> &saved_tcfs)
{
	for (int i = 0; i < count; ++i) {
		if (saved_tcfs[i].type == VFILE_PATH &&
		    iovs[i].file.type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)iovs[i].file.handle);
		}
		iovs[i].file = saved_tcfs[i];
	}
}

vfile *nfs_openv(const char **paths, int count, int *flags, mode_t *modes)
{
	vector<struct vattrs> attrs(count);
	vres tcres = { .index = count, .err_no = 0 };
	vfile *file;

	file = nfs4_openv(paths, count, flags, modes, attrs.data());

	for (int i = 0; file && i < tcres.index; i++) {
		SharedPtr<DirEntry> ptrElem = mdCache->get(paths[i]);
		if (!ptrElem.isNull()) {
			if (!ptrElem->refreshAttrs(&attrs[i], true)) {
				//Data cache contains stale data
				dataCache->remove(paths[i]);
			}
		}
		else {
			DirEntry de(paths[i], &attrs[i]);
			mdCache->add(paths[i], de);
		}
		add_fd_to_path(file[i].fd, paths[i]);
	}
	return file;
}

vres nfs_closev(vfile *tcfs, int count)
{
	vres tcres = { .index = count, .err_no = 0 };

	tcres = nfs4_closev(tcfs, count);
	for (int i = 0; i < tcres.index; i++) {
		clear_fd_to_path(tcfs[i].fd);
	}

	return tcres;
}

off_t nfs_fseek(vfile *tcf, off_t offset, int whence)
{
	return nfs4_fseek(tcf, offset, whence);
}

void fill_newIovec(struct viovec *fiovec, struct viovec *siovec)
{
	memcpy((void *) fiovec, (void *) siovec, sizeof(struct viovec));
}

// Return whether we can cache the given file.
static inline bool cacheable(const vfile *vf)
{
	return vf->type == VFILE_PATH || vf->type == VFILE_DESCRIPTOR;
}

// Get the path of the given vfile.
//
// REQUIRES: |vf| must be cacheable
static const char *get_path(const vfile *vf)
{
	const char *p = nullptr;

	if (vf->type == VFILE_PATH) {
		p = vf->path;
	} else if (vf->type == VFILE_DESCRIPTOR) {
		p = get_path_from_fd(vf->fd);
	} else {
		assert(false);  // not VFILE_PATH or VFILE_DESCRIPTOR
	}

	assert(p != nullptr);
	return p;
}

// hitArray[i] tells where the i-th I/O is a full cache hit.
struct viovec *check_dataCache(struct viovec *siovec, int count,
			       int *miss_count, std::vector<bool> &hitArray)
{
	struct viovec *final_iovec = NULL;
	struct viovec *cur_siovec = NULL;
	struct viovec *cur_fiovec = NULL;
	vector<size_t> hits(count, 0);
	int revalidate_count = 0;
	int i = 0;
	bool revalidate = false;
	vector<bool> reval(count, false);
	vector<struct vattrs> attrs(count);

	final_iovec = (struct viovec *)malloc(sizeof(struct viovec) * count);
	if (final_iovec == NULL) {
		return NULL;
	}

	for(i = 0; i < count; i++) {
		cur_siovec = siovec + i;
		if (cur_siovec->file.type != VFILE_PATH &&
			cur_siovec->file.type != VFILE_DESCRIPTOR) {
			hits[i] = 0;
			reval[i] = false;
			continue;
		}
		const char *p = get_path(&cur_siovec->file);
		int hit =
		    dataCache->get(p, cur_siovec->offset, cur_siovec->length,
				   cur_siovec->data, &revalidate);
		if (hit == 0) {
			hits[i] = 0;
			reval[i] = false;
			continue;
		}
		SharedPtr<DirEntry> ptrElem = mdCache->get(p);
		if (revalidate == false && !ptrElem.isNull() &&
		    (time(NULL) - ptrElem->getTimestamp() <= MD_REFRESH_TIME)) {
			hits[i] = hit;
			reval[i] = false;
			if (ptrElem->getFileSize() <=
			    cur_siovec->offset + cur_siovec->length) {
				cur_siovec->is_eof = true;
			}
		}
		else if (!ptrElem.isNull()) {
			//Add to getattrs
			hits[i] = hit;
			reval[i] = true;
			attrs[revalidate_count].file = cur_siovec->file;
			attrs[revalidate_count].masks = VATTRS_MASK_ALL;
			revalidate_count++;
		}
		else {
			hits[i] = 0;
			reval[i] = false;
			//Remove from data cache?
		}
	}

	//Do getattrs
	if (revalidate_count != 0) {
		vres tcres = { .index = revalidate_count, .err_no = 0 };
		tcres = nfs4_lgetattrsv(attrs.data(), revalidate_count, false);
		int l = 0;
		if (vokay(tcres)) {
			// Compare attrs in loop. If ctime is different, set
			// hits[i] to 0.
			for (int k = 0; k < count; k++) {
				if (!reval[k])
					continue;
				cur_siovec = siovec + k;
				const char *p = get_path(&cur_siovec->file);
				SharedPtr<DirEntry> ptrElem = mdCache->get(p);
				if (ptrElem.isNull() ||
				    !ptrElem->validate(&attrs[l].ctime)) {
					hits[k] = 0;
				} else if (ptrElem->getFileSize() <=
					   cur_siovec->offset +
					       cur_siovec->length) {
					cur_siovec->is_eof = true;
				}
				l++;
			}
		}
		else {
			goto exit;
		}
	}

	*miss_count = 0;
	for (i = 0; i < count; ++i) {
		cur_siovec = siovec + i;
		if (hits[i] == 0) {
			/* fill final_array[miss_count] */
			cur_fiovec = final_iovec + *miss_count;
			fill_newIovec(cur_fiovec, cur_siovec);
			if (i > 0 && hitArray[i-1] == true &&
				cur_fiovec->file.type == VFILE_CURRENT) {
			// is this logic correct? , when is new_path freed
			// FIXME: multiple VFILE_CURRENT before it
				char *new_path = (char *)malloc(
				    strlen(siovec[i - 1].file.path) +
				    strlen(siovec[i].file.path) + 2);
				sprintf(new_path, "%s/%s",
					siovec[i - 1].file.path,
					siovec[i].file.path);
				cur_fiovec->file.path = new_path;
				cur_fiovec->file.type = VFILE_PATH;
			}
			(*miss_count)++;
		} else if (hits[i] >= cur_siovec->length) {
			/* Cache hit */
			hitArray[i] = true;
		} else {  //  (hits[i] > 0) {
			/*Handle partial hit*/
			// requires miss_count to be initialized by user
			cur_fiovec = final_iovec + *miss_count;
			fill_newIovec(cur_fiovec, cur_siovec);
			cur_fiovec->offset += hits[i];
			cur_fiovec->length = cur_fiovec->length - hits[i];
			cur_fiovec->data = cur_fiovec->data + hits[i];
			(*miss_count)++;
		}
	}
exit:

	return final_iovec;
}

static void update_dataCache(struct viovec *siovec, struct viovec *final_iovec,
			     int count, std::vector<bool> &hitArray)
{
        int j = 0;
        struct viovec *cur_siovec = NULL;
        struct viovec *cur_fiovec = NULL;

        for (int i = 0; i < count; ++i) {
		if (hitArray[i]) {
			continue;  // ignore full cache hit
		}
                cur_siovec = siovec + i;
		cur_fiovec = final_iovec + j++;
		if (cur_siovec->file.type != VFILE_DESCRIPTOR &&
		    cur_siovec->file.type != VFILE_PATH) {
			memcpy(cur_siovec, cur_fiovec, sizeof(struct viovec));
			continue;
		}

		const char *p = get_path(&cur_siovec->file);
		dataCache->put(p, cur_fiovec->offset, cur_fiovec->length,
			       cur_fiovec->data);
		cur_siovec->length = cur_fiovec->length + cur_fiovec->offset -
				     cur_siovec->offset;
		cur_siovec->is_failure = cur_fiovec->is_failure;
		cur_siovec->is_eof = cur_fiovec->is_eof;
		cur_siovec->is_write_stable = cur_fiovec->is_write_stable;
        }
}

vres nfs_readv(struct viovec *iovs, int count, bool istxn)
{
	vres tcres = { .index = count, .err_no = 0 };
	std::vector<bool> hitArray(count, false);
	int miss_count = 0;
	std::vector<struct vattrs> attrs(count);

	viovec *final_iovec =
	    check_dataCache(iovs, count, &miss_count, hitArray);
	if (final_iovec == NULL) {
		return vfailure(0, ENOMEM);
	}

	vector<vfile> saved_tcfs =
	    nfs_updateIovec_FilenameToFh(final_iovec, miss_count);

	g_miss_count += miss_count;
	if (miss_count == 0) {
		/* Full cache hit */
		goto exit;
	}

	tcres = nfs4_readv(final_iovec, miss_count, istxn, attrs.data());
	if (vokay(tcres)) {
		nfs_restoreIovec_FhToFilename(final_iovec, miss_count,
					      saved_tcfs);
		for (int i = 0; i < miss_count; i++) {
			if (final_iovec[i].file.type == VFILE_PATH ||
			    final_iovec[i].file.type == VFILE_DESCRIPTOR) {
				const char *p = get_path(&final_iovec[i].file);
				DirEntry de(p, &attrs[i]);
				mdCache->add(p, de);
			}
		}
		update_dataCache(iovs, final_iovec, count, hitArray);
	}

exit:
	// TODO fix new_path memory leak
	free(final_iovec);
	return tcres;
}

vres check_and_remove(struct viovec *writes, int write_count,
		      struct vattrs *old_attrs)
{
	int hit_count = 0;
	vres tcres = { .index = hit_count, .err_no = 0 };

	for (int i = 0; i < write_count; i++) {
		if (writes[i].file.type != VFILE_DESCRIPTOR &&
		    writes[i].file.type != VFILE_PATH) {
			continue;
		}
		const char *p = get_path(&writes[i].file);
		SharedPtr<DirEntry> ptrElem = mdCache->get(p);
		if (!ptrElem.isNull() && dataCache->isCached(p)) {
			//If present in both, validate
			if (!ptrElem->validate(&old_attrs[i].ctime)) {
				dataCache->remove(p);
			}
		}
	}

	return tcres;
}

vres nfs_writev(struct viovec *writes, int write_count, bool is_transaction)
{
	vres tcres = { .index = write_count, .err_no = 0 };
	vector<struct vattrs> attrs(write_count);     // attrs after writes
	vector<struct vattrs> old_attrs(write_count); // attrs before writes

	vector<vfile> saved_tcfs =
	    nfs_updateIovec_FilenameToFh(writes, write_count);

	tcres = nfs4_writev(writes, write_count, is_transaction,
			    old_attrs.data(), attrs.data());

	if (vokay(tcres)) {
		tcres = check_and_remove(writes, write_count, old_attrs.data());
		if (!vokay(tcres))
		{
			return tcres;
		}
		for (int i = 0; i < write_count; i++) {
			if (writes[i].file.type == VFILE_PATH ||
			    writes[i].file.type == VFILE_DESCRIPTOR) {
				const char *p = get_path(&writes[i].file);
				DirEntry de(p, &attrs[i]);
				mdCache->add(p, de);
				if (writes[i].offset != TC_OFFSET_END &&
					writes[i].offset != TC_OFFSET_CUR) {
					dataCache->put(p, writes[i].offset,
						       writes[i].length,
						       writes[i].data);
				}
				else if (writes[i].offset == TC_OFFSET_END){
					dataCache->put(
					    p, attrs[i].size - writes[i].length,
					    writes[i].length, writes[i].data);
				}
				else if (writes[i].offset == TC_OFFSET_CUR) {
					//Can this be handled?
				}
			}
		}
	}

	nfs_restoreIovec_FhToFilename(writes, write_count, saved_tcfs);

	return tcres;
}

void fill_newAttr(struct vattrs *fAttrs, struct vattrs *sAttrs)
{
	memcpy((void *)&fAttrs->file, (void *)&sAttrs->file, sizeof(vfile));
}

static char *join_path(const char *parent, const char *child)
{
	if (child == nullptr) {
		return strndup(parent, PATH_MAX);
	}
	size_t len = strlen(parent) + strlen(child) + 2;
	char *new_path = (char *)malloc(len);
	if (new_path) {
		snprintf(new_path, len, "%s/%s", parent, child);
	}
	return new_path;
}

static struct vattrs *getattr_check_metacache(struct vattrs *sAttrs, int count,
					      int *miss_count,
					      vector<bool> &hitArray)
{
	struct vattrs *final_attrs = NULL;

	final_attrs = (struct vattrs *)malloc(sizeof(struct vattrs) * count);
	if (final_attrs == NULL)
		return NULL;

	auto addMissedAttrs = [&final_attrs, &miss_count](
	    const struct vattrs *va) {
		struct vattrs *fva = &final_attrs[(*miss_count)++];
		*fva = *va;
		fva->masks = VATTRS_MASK_ALL;
		return fva;
	};

	vfile prev_file = {0};  // NULL, PATH, or HANDLE
	for (int i = 0; i < count; ++i) {
		vfile cur_file = sAttrs[i].file;
		const char *p = nullptr;
		if (sAttrs[i].file.type == VFILE_CURRENT) {
			assert(prev_file.type != VFILE_NULL);
			if (prev_file.type == VFILE_HANDLE) {
				addMissedAttrs(&sAttrs[i]);
				prev_file = cur_file;
				continue;
			} else {
				assert(prev_file.type == VFILE_PATH);
				p = join_path(prev_file.path, cur_file.path);
			}
		} else if (cacheable(&cur_file)) {
			// VFILE_PATH or VFILE_DESCRIPTOR
			p = get_path(&cur_file);
		} else {
			// VFILE_HANDLE or others
			addMissedAttrs(&sAttrs[i]);
			prev_file = cur_file;
			continue;
		}

		SharedPtr<DirEntry> ptrElem = mdCache->get(p);
		if (!ptrElem.isNull() &&
		    (time(NULL) - ptrElem->getTimestamp() <= MD_REFRESH_TIME)) {
			/* Cache hit */
			ptrElem->getAttrs(&sAttrs[i]);
			hitArray[i] = true;
		} else {
			addMissedAttrs(&sAttrs[i]);
		}

		prev_file.type = VFILE_PATH;
		prev_file.path = p;
	}

	return final_attrs;
}

static void getattr_update_metacache(struct vattrs *sAttrs,
				     struct vattrs *final_attrs, int count,
				     vector<bool> &hitArray)
{
	int j = 0;
	struct vattrs *cur_sAttr = NULL;
	struct vattrs *cur_fAttr = NULL;

	for (int i = 0; i < count; ++i) {
		if (hitArray[i]) {
			continue;
		}

		cur_sAttr = sAttrs + i;
		cur_fAttr = final_attrs + j++;
		if (cacheable(&cur_fAttr->file)) {
			const char *p = get_path(&cur_fAttr->file);
			DirEntry de1(p, cur_fAttr);
			mdCache->add(p, de1);
		}
		tc_attrs2attrs(cur_sAttr, cur_fAttr);
	}
}

vres nfs_lgetattrsv(struct vattrs *attrs, int count, bool is_transaction)
{
	vres tcres = { .index = count, .err_no = 0 };
	struct vattrs *final_attrs = NULL;
	int miss_count = 0;
	vector<bool> hitArray(count, false);

	final_attrs =
	    getattr_check_metacache(attrs, count, &miss_count, hitArray);
	if (final_attrs == NULL)
		return vfailure(0, ENOMEM);

	g_miss_count += miss_count;
	if (miss_count == 0) {
	/* Full cache hit */
		goto exit;
	}

	tcres = nfs4_lgetattrsv(final_attrs, miss_count, is_transaction);

	if (vokay(tcres)) {
		getattr_update_metacache(attrs, final_attrs, count, hitArray);
	}

exit:
	// this should be done above too
	int j = 0;
	for (int i = 0; i < count; i++) {
		if (hitArray[i] == false) {
			if (attrs[i].file.type == VFILE_CURRENT &&
			    final_attrs[j].file.type == VFILE_PATH)
				free((void *) final_attrs[j].file.path);
			j++;
		}
	}
	free(final_attrs);

	return tcres;
}

static void setattr_update_pagecache(struct vattrs *sAttrs, int count)
{
	int i = 0;
	struct vattrs *cur_sAttr = NULL;

	while (i < count) {
		cur_sAttr = sAttrs + i;
		if (cur_sAttr->file.type != VFILE_PATH) {
			i++;
			continue;
		}
		SharedPtr<DirEntry> ptrElem =
		    mdCache->get(cur_sAttr->file.path);
		if (!ptrElem.isNull()) {
			/* Update cache */
			ptrElem->refreshAttrs(cur_sAttr, false);
		}

		i++;
	}
}

vres nfs_lsetattrsv(struct vattrs *attrs, int count, bool is_transaction)
{
	vector<vfile> saved_tcfs = nfs_updateAttr_FilenameToFh(attrs, count);

	vres tcres = nfs4_lsetattrsv(attrs, count, is_transaction);
	nfs_restoreAttr_FhToFilename(attrs, count, saved_tcfs);
	if (vokay(tcres)) {
		setattr_update_pagecache(attrs, count);
	}

	return tcres;
}

struct listDirPxy
{
	vec_listdir_cb cb;
	void *cbarg;
	struct vattrs_masks masks;
};

static bool poco_direntry(const struct vattrs *dentry, const char *dir,
			  void *cbarg)
{
	struct listDirPxy *temp = (struct listDirPxy *)cbarg;
	struct vattrs finalDentry;

	SharedPtr<DirEntry> parentElem = mdCache->get(dir);
	if (!parentElem.isNull()) {
		SharedPtr<DirEntry> ptrElem = mdCache->get(dentry->file.path);
		if (ptrElem.isNull()) {
			DirEntry de1(dentry, parentElem);
			mdCache->add(dentry->file.path, de1);
			ptrElem = mdCache->get(dentry->file.path);
		} else {
			ptrElem->setAttrsAndParent(dentry, parentElem);
		}

		parentElem->addChild(dentry->file.path, ptrElem);
	}

	finalDentry.masks = temp->masks;
	tc_attrs2attrs(&finalDentry, dentry);
	(temp->cb)(&finalDentry, dir, temp->cbarg);

	return true;
}

static const char **listdir_check_pagecache(vector<bool> &hitArray,
					    const char **dirs, int count,
					    int *miss_count)
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
		    (time(NULL) - curElem->getTimestamp() > MD_REFRESH_TIME)) {
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
		     vec_listdir_cb cb, void *cbarg,
		     struct vattrs_masks masks)
{
	SharedPtr<DirEntry> fileEntry = NULL;
	struct vattrs finalDentry;

	for (const auto &mypair : (curElem)->children) {
		fileEntry = mypair.second;
		std::string path = fileEntry->path();
		finalDentry.masks = masks;
		finalDentry.file = vfile_from_path(path.c_str());
		fileEntry->getAttrs(&finalDentry);

		cb(&finalDentry, curDir, cbarg);
	}
}

void reply_from_pagecache(const char **dirs, int count, vector<bool> &hitArray,
			  vec_listdir_cb cb, void *cbarg,
			  struct vattrs_masks masks)
{
	SharedPtr<DirEntry> curElem;
	const char *curDir = NULL;

	for (int i = 0; i < count; ++i) {
		curDir = dirs[i];
		curElem = mdCache->get(curDir);
		if (!curElem.isNull()) {
			(curElem)->has_listdir = true;
			if (hitArray[i] == true) {
				invoke_callback(dirs[i], curElem, cb, cbarg,
						masks);
			}
		}
	}
}

vres nfs_listdirv(const char **dirs, int count, struct vattrs_masks masks,
		    int max_entries, bool recursive, vec_listdir_cb cb,
		    void *cbarg, bool is_transaction)
{
	vres tcres = { .index = count, .err_no = 0 };
	struct listDirPxy *temp = NULL;
	// TODO fix memory leak
	const char **finalDirs;
	int miss_count = 0;
	vector<bool> hitArray(count, false);
	temp = new listDirPxy;
	if (temp == NULL)
		goto failure;

	finalDirs = listdir_check_pagecache(hitArray, dirs, count, &miss_count);

	if (miss_count > 0) {
		temp->cb = cb;
		temp->cbarg = cbarg;
		temp->masks = masks;
		masks = VMASK_INIT_ALL;
		tcres = nfs4_listdirv(finalDirs, miss_count, masks, max_entries,
				      recursive, poco_direntry, temp,
				      is_transaction);
	}

	reply_from_pagecache(dirs, count, hitArray, cb, cbarg, masks);

bool_failure:
	delete temp;
failure:
	return tcres;
}

/**
 * Translate filename to file handle and save the original vfile.
 * For vfile_pair
 */
vector<vfile> nfs_updatePair_FilenameToFh(vfile_pair *pairs, int count)
{
	int i, j;
	vector<vfile> saved_tcfs(count * 2);

	for (i = 0, j=0; i < count; ++i) {
		vfile *tcf = &pairs[i].src_file;
		saved_tcfs[j] = *tcf;
		if (tcf->type == VFILE_PATH) {
			metacache_path_to_handle(tcf);
		}

		j++;

		tcf = &pairs[i].dst_file;
		saved_tcfs[j] = *tcf;
		if (tcf->type == VFILE_PATH) {
			metacache_path_to_handle(tcf);
		}

		j++;
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For vfile_pair
 */
void nfs_restorePair_FhToFilename(vfile_pair *pairs, int count,
				  const vector<vfile> &saved_tcfs)
{
	int i, j;

	for (i = 0, j = 0; i < count; ++i) {
		if (saved_tcfs[j].type == VFILE_PATH &&
		    pairs[i].src_file.type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)pairs[i].src_file.handle);
		}
		pairs[i].src_file = saved_tcfs[j];
		j++;

		if (saved_tcfs[j].type == VFILE_PATH &&
		    pairs[i].dst_file.type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)pairs[i].dst_file.handle);
		}
		pairs[i].dst_file = saved_tcfs[j];
		j++;
	}
}

vres nfs_renamev(vfile_pair *pairs, int count, bool is_transaction)
{
	vector<vfile> saved_tcfs = nfs_updatePair_FilenameToFh(pairs, count);

	vres tcres = nfs4_renamev(pairs, count, is_transaction);

	nfs_restorePair_FhToFilename(pairs, count, saved_tcfs);

	return tcres;
}

/**
 * Translate filename to file handle and save the original vfile.
 * For vfile
 */
vector<vfile> nfs_updateFile_FilenameToFh(vfile *vfiles, int count)
{
	vector<vfile> saved_tcfs(count * 2);

	for (int i = 0; i < count; ++i) {
		vfile *tcf = &vfiles[i];
		saved_tcfs[i] = *tcf;
		if (tcf->type == VFILE_PATH) {
			metacache_path_to_handle(tcf);
		}
	}

	return saved_tcfs;
}

/*
 * Restore filehandles back to filenames if application gave us filename
 * For vfile
 */
void nfs_restoreFile_FhToFilename(vfile *vfiles, int count,
				  const vector<vfile> &saved_tcfs)
{
	for (int i = 0; i < count; ++i) {
		if (saved_tcfs[i].type == VFILE_PATH &&
		    vfiles[i].type == VFILE_HANDLE) {
			del_file_handle(
			    (struct file_handle *)vfiles[i].handle);
		}
		vfiles[i] = saved_tcfs[i];
	}
}

vres nfs_removev(vfile *vfiles, int count, bool is_transaction)
{
	vector<vfile> saved_tcfs = nfs_updateFile_FilenameToFh(vfiles, count);

	vres tcres = nfs4_removev(vfiles, count, is_transaction);

	nfs_restoreFile_FhToFilename(vfiles, count, saved_tcfs);

	if (vokay(tcres)) {
		for (int i = 0; i < count; i++) {
			if (vfiles[i].type == VFILE_PATH) {
				dataCache->remove(vfiles[i].path);
				mdCache->remove(vfiles[i].path);
			}
		}
	}
	return tcres;
}

vres nfs_mkdirv(struct vattrs *dirs, int count, bool is_transaction)
{
	vector<vfile> saved_tcfs = nfs_updateAttr_FilenameToFh(dirs, count);

	vres tcres = nfs4_mkdirv(dirs, count, is_transaction);

	nfs_restoreAttr_FhToFilename(dirs, count, saved_tcfs);

	return tcres;
}

vres nfs_lcopyv(struct vextent_pair *pairs, int count, bool is_transaction)
{
	return nfs4_lcopyv(pairs, count, is_transaction);
}

vres nfs_hardlinkv(const char **oldpaths, const char **newpaths, int count,
		     bool istxn)
{
	return nfs4_hardlinkv(oldpaths, newpaths, count, istxn);
}

vres nfs_symlinkv(const char **oldpaths, const char **newpaths, int count,
		    bool istxn)
{
	return nfs4_symlinkv(oldpaths, newpaths, count, istxn);
}

vres nfs_readlinkv(const char **paths, char **bufs, size_t *bufsizes,
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

