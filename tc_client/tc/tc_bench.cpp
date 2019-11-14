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

#include <error.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <benchmark/benchmark.h>

#include "tc_api.h"
#include "tc_helper.h"

#include <string>
#include <vector>

using std::vector;
using namespace benchmark;

const size_t BUFSIZE = 4096;
DEFINE_bool(tc, true, "Use vNFS compounding");

static void ResetTestDirectory(const char *dir)
{
	vec_unlink_recursive(&dir, 1);
	sca_ensure_dir(dir, 0755, NULL);
}

static vector<const char *> NewPaths(const char *format, int n)
{
	vector<const char *> paths(n);
	for (int i = 0; i < n; ++i) {
		char *p = (char *)malloc(PATH_MAX);
		assert(p);
		snprintf(p, PATH_MAX, format, i);
		paths[i] = p;
	}
	return paths;
}

static void FreePaths(vector<const char *> *paths)
{
	for (auto p : *paths)
		free((char *)p);
}

static vector<viovec> NewIovecs(vfile *files, int n, size_t offset = 0)
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

static void FreeIovecs(vector<viovec> *iovs)
{
	for (auto iov : *iovs)
		free((char *)iov.data);
}

static void BM_CreateEmpty(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> paths = NewPaths("file-%d", nfiles);

	while (state.KeepRunning()) {
		// state.iterators()
		state.PauseTiming();
		vec_unlink(paths.data(), nfiles);
		state.ResumeTiming();

		vfile *files = vec_open_simple(paths.data(), nfiles,
						 O_CREAT | O_WRONLY, 0);
		assert(files);
		vres tcres = vec_close(files, nfiles);
		assert(vokay(tcres));
	}

	FreePaths(&paths);
}
BENCHMARK(BM_CreateEmpty)->RangeMultiplier(2)->Range(1, 256);

static void BM_OpenClose(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> paths = NewPaths("/vfs0/file-%d", nfiles);

	while (state.KeepRunning()) {
		vfile *files =
		    vec_open_simple(paths.data(), nfiles, O_RDONLY, 0);
		assert(files);
		vres tcres = vec_close(files, nfiles);
		assert(vokay(tcres));
	}

	FreePaths(&paths);
}
BENCHMARK(BM_OpenClose)->RangeMultiplier(2)->Range(1, 256);

static void ReadWrite(benchmark::State &state, int flags, bool read)
{
	size_t nfiles = state.range(0);
	vector<const char *> paths = NewPaths("/vfs0/file-%d", nfiles);
	auto iofunc = read ? vec_read : vec_write;
	size_t offset = (flags & O_APPEND) ? TC_OFFSET_END : 0;

	vfile *files =
	    vec_open_simple(paths.data(), nfiles, flags, 0644);
	assert(files);
	vector<viovec> iovs = NewIovecs(files, nfiles, offset);

	while (state.KeepRunning()) {
		vres tcres = iofunc(iovs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	vec_close(files, nfiles);
	FreeIovecs(&iovs);
	FreePaths(&paths);
}

static void BM_Write4K(benchmark::State &state)
{
	ReadWrite(state, O_WRONLY | O_CREAT, false);
}
BENCHMARK(BM_Write4K)->RangeMultiplier(2)->Range(1, 256);

static void BM_Write4KSync(benchmark::State &state)
{
	ReadWrite(state, O_WRONLY | O_CREAT | O_SYNC | O_DIRECT, false);
}
BENCHMARK(BM_Write4KSync)->RangeMultiplier(2)->Range(1, 256);

static void BM_Append4K(benchmark::State &state)
{
	ReadWrite(state, O_WRONLY | O_CREAT | O_APPEND, false);
}
BENCHMARK(BM_Append4K)->RangeMultiplier(2)->Range(1, 256);

static void BM_Append4KSync(benchmark::State &state)
{
	ReadWrite(state, O_WRONLY | O_CREAT | O_APPEND | O_SYNC, false);
}
BENCHMARK(BM_Append4KSync)->RangeMultiplier(2)->Range(1, 256);

static void BM_Read4K(benchmark::State &state)
{
	ReadWrite(state, O_RDONLY, true);
}
BENCHMARK(BM_Read4K)->RangeMultiplier(2)->Range(1, 256);

static void BM_Read4KSync(benchmark::State &state)
{
	ReadWrite(state, O_RDONLY | O_SYNC | O_DIRECT, true);
}
BENCHMARK(BM_Read4KSync)->RangeMultiplier(2)->Range(1, 256);

static void BM_Read4KOpenClose(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> paths = NewPaths("file-%d", nfiles);
	vector<vfile> files(nfiles);

	for (size_t i = 0; i < nfiles; ++i) {
		files[i] = vfile_from_path(paths[i]);
	}

	vector<viovec> iovs = NewIovecs(files.data(), nfiles);

	while (state.KeepRunning()) {
		vres tcres = vec_read(iovs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeIovecs(&iovs);
	FreePaths(&paths);
}
BENCHMARK(BM_Read4KOpenClose)->RangeMultiplier(2)->Range(1, 256);

static vector<vattrs> NewTcAttrs(size_t nfiles, vattrs *values = nullptr)
{
	vector<const char *> paths = NewPaths("file-%d", nfiles);
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

static void FreeTcAttrs(vector<vattrs> *attrs)
{
	for (const auto& at : *attrs) {
		free((char *)at.file.path);
	}
}

static inline struct timespec totimespec(long sec, long nsec)
{
	struct timespec tm = {
		.tv_sec = sec,
		.tv_nsec = nsec,
	};
	return tm;
}

static vattrs GetAttrValuesToSet(int nattrs)
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

static void BM_Getattrs(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<vattrs> attrs = NewTcAttrs(nfiles);

	while (state.KeepRunning()) {
		vres tcres = vec_getattrs(attrs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeTcAttrs(&attrs);
}
BENCHMARK(BM_Getattrs)->RangeMultiplier(2)->Range(1, 256);

static void BM_Setattr1(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vattrs values = GetAttrValuesToSet(1);
	vector<vattrs> attrs = NewTcAttrs(nfiles, &values);

	while (state.KeepRunning()) {
		vres tcres = vec_setattrs(attrs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeTcAttrs(&attrs);
}
BENCHMARK(BM_Setattr1)->RangeMultiplier(2)->Range(1, 256);

static void BM_Setattr2(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vattrs values = GetAttrValuesToSet(2);
	vector<vattrs> attrs = NewTcAttrs(nfiles, &values);

	while (state.KeepRunning()) {
		vres tcres = vec_setattrs(attrs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeTcAttrs(&attrs);
}
BENCHMARK(BM_Setattr2)->RangeMultiplier(2)->Range(1, 256);

static void BM_Setattr3(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vattrs values = GetAttrValuesToSet(3);
	vector<vattrs> attrs = NewTcAttrs(nfiles, &values);

	while (state.KeepRunning()) {
		vres tcres = vec_setattrs(attrs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeTcAttrs(&attrs);
}
BENCHMARK(BM_Setattr3)->RangeMultiplier(2)->Range(1, 256);

static void BM_Setattr4(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vattrs values = GetAttrValuesToSet(4);
	vector<vattrs> attrs = NewTcAttrs(nfiles, &values);

	while (state.KeepRunning()) {
		vres tcres = vec_setattrs(attrs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeTcAttrs(&attrs);
}
BENCHMARK(BM_Setattr4)->RangeMultiplier(2)->Range(1, 256);

static void CreateFiles(vector<const char *>& paths)
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

static vector<vextent_pair> NewFilePairsToCopy(size_t nfiles)
{
	vector<const char *> srcs = NewPaths("file-%d", nfiles);
	CreateFiles(srcs);
	vector<const char *> dsts = NewPaths("dst-%d", nfiles);
	vector<vextent_pair> pairs(nfiles);
	for (size_t i = 0; i < nfiles; ++i) {
		pairs[i].src_path = srcs[i];
		pairs[i].dst_path = dsts[i];
		pairs[i].src_offset = 0;
		pairs[i].dst_offset = 0;
		pairs[i].length = BUFSIZE;
	}
	return pairs;
}

static void FreeFilePairsToCopy(vector<vextent_pair> *pairs)
{
	for (auto& p : *pairs) {
		free((char *)p.src_path);
		free((char *)p.dst_path);
	}
}

static void BM_Copy(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<vextent_pair> pairs = NewFilePairsToCopy(nfiles);

	while (state.KeepRunning()) {
		vres tcres = vec_dup(pairs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeFilePairsToCopy(&pairs);
}
BENCHMARK(BM_Copy)->RangeMultiplier(2)->Range(1, 256);

static void BM_SSCopy(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<vextent_pair> pairs = NewFilePairsToCopy(nfiles);

	while (state.KeepRunning()) {
		vres tcres = vec_copy(pairs.data(), nfiles, false);
		assert(vokay(tcres));
	}

	FreeFilePairsToCopy(&pairs);
}
BENCHMARK(BM_SSCopy)->RangeMultiplier(2)->Range(1, 256);

static void BM_Mkdir(benchmark::State &state)
{
	size_t ndirs = state.range(0);
	vector<const char *> paths = NewPaths("Bench-Mkdir/dir-%d", ndirs);
	vector<vattrs> dirs(ndirs);

	while (state.KeepRunning()) {
		state.PauseTiming();
		ResetTestDirectory("Bench-Mkdir");
		for (size_t i = 0; i < ndirs; ++i) {
			vset_up_creation(&dirs[i], paths[i], 0755);
		}
		state.ResumeTiming();

		vres tcres = vec_mkdir(dirs.data(), ndirs, false);
		assert(vokay(tcres));

	}

	FreePaths(&paths);
}
BENCHMARK(BM_Mkdir)->RangeMultiplier(2)->Range(1, 256);

static void BM_Symlink(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> files = NewPaths("Bench-Symlink/file-%d", nfiles);
	vector<const char *> links = NewPaths("Bench-Symlink/link-%d", nfiles);

	ResetTestDirectory("Bench-Symlink");
	CreateFiles(files);
	while (state.KeepRunning()) {
		vres tcres =
		    vec_symlink(files.data(), links.data(), nfiles, false);
		assert(vokay(tcres));

		state.PauseTiming();
		vec_unlink(links.data(), nfiles);
		state.ResumeTiming();
	}

	FreePaths(&files);
	FreePaths(&links);
}
BENCHMARK(BM_Symlink)->RangeMultiplier(2)->Range(1, 256);

static void BM_Readlink(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> files = NewPaths("Bench-Readlink/file-%d", nfiles);
	vector<const char *> links = NewPaths("Bench-Readlink/link-%d", nfiles);
	vector<char *> bufs(nfiles);
	vector<size_t> buf_sizes(nfiles, PATH_MAX);

	for (size_t i = 0; i < nfiles; ++i) {
		bufs[i] = (char *)malloc(PATH_MAX);
	}

	ResetTestDirectory("Bench-Readlink");
	CreateFiles(files);
	vec_symlink(files.data(), links.data(), nfiles, false);
	while (state.KeepRunning()) {
		vres tcres = vec_readlink(links.data(), bufs.data(),
					    buf_sizes.data(), nfiles, false);
		assert(vokay(tcres));
	}

	for (size_t i = 0; i < nfiles; ++i) {
		free(bufs[i]);
	}
	FreePaths(&files);
	FreePaths(&links);
}
BENCHMARK(BM_Readlink)->RangeMultiplier(2)->Range(1, 256);

static void BM_Rename(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> srcs = NewPaths("Bench-Rename/src-%d", nfiles);
	vector<const char *> dsts = NewPaths("Bench-Rename/dst-%d", nfiles);
	vector<vfile_pair> pairs(nfiles);

	for (size_t i = 0; i < nfiles; ++i) {
		pairs[i].src_file = vfile_from_path(srcs[i]);
		pairs[i].dst_file = vfile_from_path(dsts[i]);
	}

	ResetTestDirectory("Bench-Rename");
	CreateFiles(srcs);
	while (state.KeepRunning()) {
		vres tcres = vec_rename(pairs.data(), nfiles, false);
		assert(vokay(tcres));

		// switch srcs and dsts
		state.PauseTiming();
		for (size_t i = 0; i < nfiles; ++i) {
			std::swap(pairs[i].src_file, pairs[i].dst_file);
		}
		state.ResumeTiming();
	}

	FreePaths(&srcs);
	FreePaths(&dsts);
}
BENCHMARK(BM_Rename)->RangeMultiplier(2)->Range(1, 256);

static void BM_Remove(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> paths = NewPaths("Bench-Removev/file-%d", nfiles);

	ResetTestDirectory("Bench-Removev");
	while (state.KeepRunning()) {
		state.PauseTiming();
		CreateFiles(paths);
		state.ResumeTiming();

		vres tcres = vec_unlink(paths.data(), nfiles);
		assert(vokay(tcres));
	}

	FreePaths(&paths);
}
BENCHMARK(BM_Remove)->RangeMultiplier(2)->Range(1, 256);

// dummy callback
static bool DummyListDirCb(const struct vattrs *entry, const char *dir,
			   void *cbarg)
{
	return true;
}

/*
There average directory width is 17:

#find linux-4.6.3/ -type d | \
while read dname; do ls -l $dname | wc -l; done  | \
awk '{s += $1} END {print s/NR;}'
16.8402
*/
static void CreateDirsWithContents(vector<const char *>& dirs)
{
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

static void BM_Listdir(benchmark::State &state)
{
	size_t nfiles = state.range(0);
	vector<const char *> dirs = NewPaths("Bench-Listdir/dir-%d", nfiles);

	ResetTestDirectory("Bench-Listdir");
	CreateDirsWithContents(dirs);

	while (state.KeepRunning()) {
		vres tcres =
		    vec_listdir(dirs.data(), nfiles, VATTRS_MASK_ALL, 0,
				false, DummyListDirCb, NULL, false);
		assert(vokay(tcres));
	}

	FreePaths(&dirs);
}
BENCHMARK(BM_Listdir)->RangeMultiplier(2)->Range(1, 256);


static void* SetUp(bool istc)
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

static void TearDown(void *context)
{
	vdeinit(context);
}

int main(int argc, char **argv)
{
	benchmark::Initialize(&argc, argv);
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	void *context = SetUp(FLAGS_tc);
	sca_chdir("/vfs0");
	benchmark::RunSpecifiedBenchmarks();
	TearDown(context);

	return 0;
}
