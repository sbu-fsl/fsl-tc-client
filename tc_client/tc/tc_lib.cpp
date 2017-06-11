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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tc_api.h"
#include "tc_helper.h"
#include "path_utils.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <vector>

using std::vector;
using std::queue;

struct rm_cb_args {
	vector<const char *> *dirs;
	vector<const char *> *files;
};

struct cp_cb_args {
	vector<const char *> *dirs;
	vector<struct vattrs> *files;
	vector<const char *> *symlinks;
};

static bool rm_list_callback(const struct vattrs *entry, const char *dir,
			     void *cbarg)
{
	struct rm_cb_args *args = (struct rm_cb_args *)cbarg;
	if (S_ISDIR(entry->mode)) {
		args->dirs->push_back(strdup(entry->file.path));
	} else {
		args->files->push_back(strdup(entry->file.path));
	}
	return true;
}

static void free_paths(vector<const char *> *paths)
{
	for (const char *p : *paths) {
		free((char *)p);
	}
	paths->clear();
}

static void free_attrs(vector<struct vattrs> *paths)
{
	for (auto&& attr : *paths) {
		free((char *)attr.file.path);
	}
	paths->clear();
}

static bool cp_list_callback(const struct vattrs *entry, const char *dir,
			     void *cbarg)
{
	struct cp_cb_args *args = (struct cp_cb_args *)cbarg;
	if (S_ISDIR(entry->mode)) {
		args->dirs->push_back(strdup(entry->file.path));
	} else if (S_ISLNK(entry->mode)) {
		args->symlinks->push_back(strdup(entry->file.path));
	} else {
		struct vattrs attr = *entry;
		attr.file.path = strdup(entry->file.path);
		args->files->push_back(attr);
	}
	return true;
}

static char *new_cp_target_path(const char *src_obj, const char *src_dir,
				const char *dst_dir)
{
	char *path = (char*) malloc(PATH_MAX);
	const char *p = src_obj;
	for (int j = 0; *p != '\0' && *p == src_dir[j]; j++)
		++p;
	tc_path_join(dst_dir, p, path, PATH_MAX);
	return path;
}

static vres tc_cp_mkdirs(const char *src_dir, const char **dirs, int count,
			 const char *dst_dir)
{
	vres res = {0};
	if (count <= 0) return res;
	vector<struct vattrs> attrs(count);
	for (int i = 0; i < count; i++) {
		attrs[i].file = vfile_from_path(
		    new_cp_target_path(dirs[i], src_dir, dst_dir));
		attrs[i].masks = VATTRS_MASK_NONE;
		attrs[i].masks.has_mode = true;
		attrs[i].mode = 0755;
	}
	res = vec_mkdir(attrs.data(), count, false);
	if (!vokay(res)) {
		printf("mkdirv-failed: %s (%d-th %s)\n", strerror(res.err_no),
		       res.index, attrs[res.index].file.path);
	}

	free_attrs(&attrs);

	return res;
}

static vres tc_symlink_objs(const vector<const char *> &srcs,
			    const char *src_dir, const char *dst_dir)
{
	vres tcres;
	const int count = srcs.size();
	vector<const char *> src_paths(count);
	vector<const char *> dst_paths(count);

	for (int i = 0; i < count; i++) {
		src_paths[i] = srcs[i];
		dst_paths[i] = new_cp_target_path(srcs[i], src_dir, dst_dir);
	}

	tcres = vec_symlink((const char **)(src_paths.data()), dst_paths.data(),
			    count, false);
	if (!vokay(tcres)) {
		fprintf(stderr, "vec_symlink: %s (%s -> %s)\n",
			strerror(tcres.err_no), src_paths[tcres.index],
			dst_paths[tcres.index]);
	}

	free_paths(&dst_paths);
	return tcres;
}

static vres tc_cp_files(vector<struct vattrs> &srcs, const char *src_dir,
			  const char *dst_dir)
{
	const int count = srcs.size();
	vres tcres;
	vector<struct vextent_pair> pairs(count);

	for (int i = 0; i < count; i++) {
		char *path =
		    new_cp_target_path(srcs[i].file.path, src_dir, dst_dir);
		pairs[i].src_path = srcs[i].file.path;
		pairs[i].dst_path = path;
		pairs[i].src_offset = 0;
		pairs[i].dst_offset = 0;
		pairs[i].length = 0;
	}
	tcres = vec_lcopy(pairs.data(), count, false);
	if (!vokay(tcres)) {
		fprintf(stderr, "vec_lcopy: %s (%s)\n", strerror(tcres.err_no),
			pairs[tcres.index].src_path);
	}

	for (auto& p : pairs) {
		free((char *)p.dst_path);
	}

	return tcres;
}

static vres tc_dup_files(const vector<struct vattrs> &srcs,
			   const char *src_dir, const char *dst_dir)
{
	const size_t kSizeLimit = 512 * 1024;
	const size_t count = srcs.size();
	vector<struct vattrs> attrs(count);
	for (size_t i = 0; i < count; ++i) {
		attrs[i].file = vfile_from_path(srcs[i].file.path);
		memset(&attrs[i].masks, 0, sizeof(attrs[i].masks));
		attrs[i].masks.has_size = true;
	}

	vres tcres = vec_lgetattrs(attrs.data(), count, false);
	if (!vokay(tcres)) {
		fprintf(stderr, "failed to getattrsv");
		return tcres;
	}

	vector<struct vextent_pair> small_files;
	vector<int> big_files_indices;
	small_files.reserve(count);
	vector<const char *> dst_paths(count);
	for (size_t i = 0; i < count; ++i) {
		dst_paths[i] =
		    new_cp_target_path(srcs[i].file.path, src_dir, dst_dir);
		if (attrs[i].size > kSizeLimit) {
			big_files_indices.push_back(i);
			continue;
		}
		struct vextent_pair ext = { 0 };
		ext.src_path = srcs[i].file.path;
		ext.dst_path = dst_paths[i];
		ext.src_offset = 0;
		ext.dst_offset = 0;
		ext.length = attrs[i].size;
		small_files.push_back(ext);
	}

	// Duplicate small files
	for (size_t i = 0; i < small_files.size(); ) {
		size_t bytes = 0;
		size_t n = 0;
		while (bytes < kSizeLimit && i + n < small_files.size() &&
		       n++ < 64) {
			bytes += small_files[i + n].length;
		}
		tcres = vec_ldup(small_files.data() + i, n, false);
		if (!vokay(tcres)) {
			fprintf(stderr, "failed to duplicate file %s to %s: %s",
				small_files[i + tcres.index].src_path,
				small_files[i + tcres.index].dst_path,
				strerror(tcres.err_no));
			break;
		}
		i += n;
	}

	// Duplicate large files
	for (size_t i : big_files_indices) {
		size_t offset = 0;
		struct vextent_pair ext;
		ext.src_path = srcs[i].file.path;
		ext.dst_path = dst_paths[i];
		while (offset < attrs[i].size) {
			ext.src_offset = offset;
			ext.dst_offset = offset;
			ext.length = std::min<size_t>(attrs[i].size - offset,
						      kSizeLimit);
			tcres = vec_ldup(&ext, 1, false);
			if (!vokay(tcres)) {
				fprintf(stderr, "failed to duplicate file %s "
						"to %s at offset %zu: %s",
					ext.src_path, ext.dst_path, offset,
					strerror(tcres.err_no));
				break;
			}
			offset += ext.length;
		}
		if (!vokay(tcres))
			break;
	}

	free_paths(&dst_paths);
	return tcres;
}

static vres tc_cp_setattrs(const vector<struct vattrs> &srcs,
			     const char *src_dir, const char *dst_dir)
{
	vector<struct vattrs> dsts(srcs);
	for (unsigned int i = 0; i < srcs.size(); i++) {
		// Workaround -- was getting invalid argument error from
		// lsetattrsv().  The issue was that vec_listdir() was not
		// honoring the mask I gave it, meaning the vattrs in the
		// srcs vector had incorrect masks set.  So here, we explicitly
		// override those incorrect masks with what we want to set.
		dsts[i].masks = VATTRS_MASK_NONE;
		dsts[i].mode &= ~(S_IFMT);  // TODO: do this in vec_lsetattrs()
		dsts[i].masks.has_mode = true;

		dsts[i].file = vfile_from_path(
		    new_cp_target_path(srcs[i].file.path, src_dir, dst_dir));
	}
	vres tcres = vec_lsetattrs(dsts.data(), dsts.size(), false);
	free_attrs(&dsts);
	return tcres;
}

vres tc_cp_symlinks(const vector<const char *> &links, const char *src_dir,
		    const char *dst_dir)
{
	vector<const char *> mylinks(links.begin(), links.end());
	const size_t count = mylinks.size();
	char *linkbufs;
	vector<char *> bufs(count);
	vector<size_t> bufsizes(count, PATH_MAX);

	linkbufs = (char *)malloc(count * PATH_MAX);
	for (size_t i = 0; i < count; ++i) {
		bufs[i] = linkbufs + i * PATH_MAX;
	}
	vres tcres = vec_readlink((const char **)(mylinks.data()), bufs.data(),
				    bufsizes.data(), count, false);
	if (!vokay(tcres)) {
		fprintf(stderr, "vec_readlink failed: %s at %d (%s)\n",
			strerror(tcres.err_no), tcres.index,
			mylinks[tcres.index]);
		free(linkbufs);
		return tcres;
	}

	vector<const char *> newlinks(count);
	for (size_t i = 0; i < count; ++i) {
		newlinks[i] = new_cp_target_path(mylinks[i], src_dir, dst_dir);
	}
	tcres = vec_symlink((const char **)bufs.data(), newlinks.data(), count,
			    false);
	if (!vokay(tcres)) {
		fprintf(stderr, "vec_readlink failed: %s at %d (%s)\n",
			strerror(tcres.err_no), tcres.index,
			mylinks[tcres.index]);
	}

	free(linkbufs);
	free_paths(&newlinks);
	return tcres;
}

vres sca_cp_recursive(const char *src_dir, const char *dst, bool symlink,
		      bool use_server_side_copy)
{
	vector<const char *> dirs;
	vector<struct vattrs> files_to_copy;
	vector<const char *> symlinks;
	struct vattrs_masks listdir_mask = VATTRS_MASK_NONE;
	listdir_mask.has_mode = true;
	struct cp_cb_args cbargs;
	vres tcres = {0};
	cbargs.dirs = &dirs;
	cbargs.files = &files_to_copy;
	cbargs.symlinks = &symlinks;

	dirs.push_back(strdup(src_dir));

	unsigned int created = 0;  // index to directories created so far
	while (created < dirs.size() || !files_to_copy.empty()) {
		int n = dirs.size() - created;
		if (n > 0) {
			// Make a copy of the dir listing because we might
			// invalidate its underlying ".data()" pointer when
			// appending new directories in the callback function.
			vector<const char *> dirs_listing(
			    dirs.begin() + created, dirs.end());
			tcres = vec_listdir(dirs_listing.data(), n,
					    listdir_mask, 0, false,
					    cp_list_callback, &cbargs, false);
			if (!vokay(tcres)) {
				break;
			}

			tcres =
			    tc_cp_mkdirs(src_dir, dirs_listing.data(), n, dst);
			if (!vokay(tcres)) {
				break;
			}
			created += n;
		}

		if (symlink) {
			std::vector<const char *> paths(files_to_copy.size());
			for (unsigned int i = 0; i < paths.size(); i++) {
				paths[i] = files_to_copy[i].file.path;
			}
			tcres = tc_symlink_objs(paths, src_dir, dst);
		} else if (use_server_side_copy) {
			tcres = tc_cp_files(files_to_copy, src_dir, dst);
		} else {
			tcres = tc_dup_files(files_to_copy, src_dir, dst);
		}
		if (!vokay(tcres)) {
			break;
		}

		if (!symlink) {
			tcres = tc_cp_setattrs(files_to_copy, src_dir, dst);
			if (!vokay(tcres)) {
				fprintf(stderr, "tc_cp_setattrs: %s\n",
					strerror(tcres.err_no));
			}
		}

		free_attrs(&files_to_copy);
	}

	if (vokay(tcres) && !symlinks.empty()) {
		if (symlink) {
			tcres = tc_symlink_objs(symlinks, src_dir, dst);
		} else {
			tcres = tc_cp_symlinks(symlinks, src_dir, dst);
		}
	}

	free_paths(&dirs);
	free_attrs(&files_to_copy);
	free_paths(&symlinks);

	return tcres;
}

vres vec_unlink_recursive(const char **objs, int count)
{
	vector<const char *> dirs;
	vector<const char *> files_to_remove;
	struct vattrs_masks listdir_mask;
	listdir_mask.has_mode = true;
	listdir_mask.has_nlink = true;
	struct rm_cb_args cbargs;
	cbargs.dirs = &dirs;
	cbargs.files = &files_to_remove;
	const char **dirs_p;

	// initialize "dirs"
	{
		vector<struct vattrs> attrs(count);
		for (int i = 0; i < count; ++i) {
			attrs[i].file = vfile_from_path(objs[i]);
			attrs[i].masks = VATTRS_MASK_NONE;
			attrs[i].masks.has_mode = true;
		}

		for (int i = 0; (size_t)i < attrs.size(); ) {
			vres tcres = vec_getattrs(attrs.data() + i,
						  attrs.size() - i, false);
			if (vokay(tcres)) {
				break;
			} else if (tcres.err_no == ENOENT) {
				// ignore not existed entries
				attrs.erase(attrs.begin() + (i + tcres.index));
				i += tcres.index - 1;
				if (i < 0)
					i = 0;
			} else {
				return tcres;
			}
		}

		for (unsigned int i = 0; i < attrs.size(); ++i) {
			if (S_ISDIR(attrs[i].mode)) {
				dirs.push_back(strdup(objs[i]));
			} else {
				files_to_remove.push_back(strdup(objs[i]));
			}
		}
	}

	unsigned int emptied = 0;  // index to directories emptied so far
	while (emptied < dirs.size() || !files_to_remove.empty()) {
		vres tcres =
		    vec_unlink(files_to_remove.data(), files_to_remove.size());
		if (!vokay(tcres)) {
			return tcres;
		}

		free_paths(&files_to_remove);

		int n = dirs.size() - emptied;
		dirs_p = (const char**)malloc(n * sizeof(char*));
		for (int i = 0; i < n; i++) {
			dirs_p[i] = *(dirs.data() + emptied + i);
		}
		tcres = vec_listdir(dirs_p, n, listdir_mask, 0,
				    false, rm_list_callback, &cbargs, false);
		if (!vokay(tcres)) {
			return tcres;
		}

		emptied += n;
	}

	while (!dirs.empty()) {
		vector<const char*> dirs_to_remove(dirs.rbegin(), dirs.rend());
		vres tcres = vec_unlink(dirs_to_remove.data(), dirs.size());
		if (!vokay(tcres)) {
			return tcres;
		}

		free_paths(&dirs);
	}

	return vfailure(0, 0);
}
