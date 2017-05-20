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


#include "tc_bench_util.h"

#include <error.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tc_api.h"
#include "tc_helper.h"

#include <string>
#include <vector>
#include <memory>
#include <iostream>

using std::vector;

const size_t BUFSIZE = 4096;

void ResetTestDirectory(const char *dir)
{
	vec_unlink_recursive(&dir, 1);
	sca_ensure_dir(dir, 0755, NULL);
}

vector<const char *> NewPaths(const char *format, int n, int start)
{
	vector<const char *> paths(n);
	for (int i = 0; i < n; ++i) {
		char *p = (char *)malloc(PATH_MAX);
		assert(p);
		snprintf(p, PATH_MAX, format, start + i);
		paths[i] = p;
	}
	return paths;
}

void FreePaths(vector<const char *> *paths)
{
	for (auto p : *paths)
		free((char *)p);
}

vector<vfile> Paths2Files(const vector<const char *>& paths)
{
	vector<vfile> files(paths.size());
	for (int i = 0; i < files.size(); ++i) {
		files[i] = vfile_from_path(paths[i]);
	}
	return files;
}

vector<viovec> NewIovecs(vfile *files, int n, size_t offset)
{
	vector<viovec> iovs(n);
	for (int i = 0; i < n; ++i) {
		iovs[i].file = files[i];
		iovs[i].offset = offset;
		iovs[i].length = BUFSIZE;
		iovs[i].data = (char *)malloc(BUFSIZE);
		iovs[i].is_write_stable = true;
	}
	return iovs;
}

void FreeIovecs(vector<viovec> *iovs)
{
	for (auto iov : *iovs)
		free((char *)iov.data);
}

vector<vattrs> NewTcAttrs(size_t nfiles, vattrs *values, int start)
{
	vector<const char *> paths = NewPaths("Bench-Files/file-%d", nfiles, start);
	vector<vattrs> attrs(nfiles);

	for (size_t i = 0; i < nfiles; ++i) {
		if (values) {
			attrs[i] = *values;
		} else {
			attrs[i].masks = VATTRS_MASK_ALL;
		}
		attrs[i].file = vfile_from_path(paths[i]);
	}

	return attrs;
}

void FreeTcAttrs(vector<vattrs> *attrs)
{
	for (const auto& at : *attrs) {
		free((char *)at.file.path);
	}
}

vattrs GetAttrValuesToSet(int nattrs)
{
	vattrs attrs;

	attrs.masks = VATTRS_MASK_NONE;
	if (nattrs >= 1) {
		vattrs_set_mode(&attrs, S_IRUSR | S_IRGRP | S_IROTH);
	}
	if (nattrs >= 2) {
		vattrs_set_uid(&attrs, 0);
		vattrs_set_gid(&attrs, 0);
	}
	if (nattrs >= 3) {
		vattrs_set_atime(&attrs, totimespec(time(NULL), 0));
	}
	if (nattrs >= 4) {
		vattrs_set_size(&attrs, 8192);
	}
	return attrs;
}

void CreateFiles(vector<const char *>& paths)
{
	const size_t nfiles = paths.size();
	vfile *files =
	    vec_open_simple(paths.data(), nfiles, O_WRONLY | O_CREAT, 0644);
	assert(files);
	vector<viovec> iovs = NewIovecs(files, nfiles);
	vres tcres = vec_write(iovs.data(), nfiles, false);
	assert(vokay(tcres));
	vec_close(files, nfiles);
	FreeIovecs(&iovs);
}

std::vector<vextent_pair> NewFilePairsToCopy(const char *src_format,
					       const char *dst_format,
					       size_t nfiles, size_t start)
{
	auto srcs = NewPaths(src_format, nfiles, start);
	auto dsts = NewPaths(dst_format, nfiles, start);
	vector<vextent_pair> pairs(nfiles);
	for (size_t i = 0; i < nfiles; ++i) {
		pairs[i].src_path = srcs[i];
		pairs[i].dst_path = dsts[i];
		pairs[i].src_offset = 0;
		pairs[i].dst_offset = 0;
		pairs[i].length = 0;
	}
	return pairs;
}

void FreeFilePairsToCopy(vector<vextent_pair> *pairs)
{
	for (auto& p : *pairs) {
		free((char *)p.src_path);
		free((char *)p.dst_path);
	}
}

bool DummyListDirCb(const struct vattrs *entry, const char *dir, void *cbarg)
{
	return true;
}

// There average directory width is 17:
//
// #find linux-4.6.3/ -type d | \
//  while read dname; do ls -l $dname | wc -l; done  | \
//  awk '{s += $1} END {print s/NR;}'
// 16.8402
void CreateDirsWithContents(vector<const char *>& dirs)
{
	const int kFilesPerDir = 17;
	vector<vattrs> attrs(dirs.size());
	for (size_t i = 0; i < dirs.size(); ++i) {
		vset_up_creation(&attrs[i], dirs[i], 0755);
	}
	vres tcres = vec_mkdir(attrs.data(), dirs.size(), false);
	assert(vokay(tcres));

	for (size_t i = 0; i < dirs.size(); ++i) {
		char p[PATH_MAX];
		snprintf(p, PATH_MAX, "%s/%%d", dirs[i]);
		auto files = NewPaths(p, 17);
		CreateFiles(files);
		FreePaths(&files);
	}
}

void* SetUp(bool istc)
{
	void *context;
	if (istc) {
		char buf[PATH_MAX];
		context = vinit(get_tc_config_file(buf, PATH_MAX),
				  "/tmp/tc-bench-tc.log", 77);
		fprintf(stderr, "Using config file at %s\n", buf);
	} else {
		context = vinit(NULL, "/tmp/tc-bench-posix.log", 0);
	}
	return context;
}

void TearDown(void *context)
{
	vdeinit(context);
}

off_t ConvertSize(const char *size_str)
{
	double size = atof(size_str);
	char unit = size_str[strlen(size_str) - 1];
	off_t scale = 1;
	if (unit == 'k' || unit == 'K') {
		scale <<= 10;
	} else if (unit == 'm' || unit == 'M') {
		scale <<= 20;
	} else if (unit == 'g' || unit == 'G') {
		scale <<= 30;
	}
	return (off_t)(scale * size);
}

off_t GetFileSize(const char *file_path)
{
	struct stat file_status;
	if (sca_stat(file_path, &file_status) < 0) {
		error(1, errno, "Could not get size of %s", file_path);
	}
	return file_status.st_size;
}
