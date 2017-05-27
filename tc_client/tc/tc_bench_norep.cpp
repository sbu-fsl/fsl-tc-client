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

/**
 * This benchmark is different from tc_bench in that a file/directory/symlink
 * is accessed only one time instead of repeatly.  This is useful when we want
 * to exclude the caching effect on the NFS client.
 */

#include <error.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tc_api.h"
#include "tc_helper.h"
#include "path_utils.h"

#include "tc_bench_util.h"

#include <gflags/gflags.h>

#include <string>
#include <vector>

DEFINE_bool(tc, true, "Use TC implementation");

DEFINE_int32(nfiles, 10240, "Number of files");

DEFINE_int32(compound_size, 1, "Number of operations per compound");

DEFINE_string(copy_from, "files-4K/%04d", "pattern of files to copy");

DEFINE_string(copy_to, "files-copied/%04d", "pattern of copy dest");

DEFINE_string(op, "CreateEmpty", "Operation to perform: "
	      "[Symlink, Readlink, MkdirWithContents, CreateEmpty, OpenClose, "
	      " Listdir, Getattrs, Setattr1, Setattr2, Setattr3, Setattr4]");

using std::vector;

static void OpenClose(int start, int csize, int flags)
{
	vector<const char *> paths =
	    NewPaths("Bench-Files/file-%d", csize, start);

	// NOTE on the limit of #files we can open per process.
	vfile *files = vec_open_simple(paths.data(), csize, flags, 0644);
	assert(files);
	vres tcres = vec_close(files, csize);
	assert(vokay(tcres));

	FreePaths(&paths);
}

static void BM2_CreateEmpty(int start, int csize)
{
	if (start == 0) ResetTestDirectory("Bench-Files");
	OpenClose(start, csize, O_CREAT | O_WRONLY);
}

static void BM2_OpenClose(int start, int csize)
{
	OpenClose(start, csize, O_RDONLY);
}

static void ReadWrite4K(int start, int csize, int flags, bool read)
{
	vector<const char *> paths =
	    NewPaths("Bench-Files/file-%d", csize, start);
	vector<vfile> files = Paths2Files(paths);
	auto iovs = NewIovecs(files.data(), csize, 0, flags);

	auto iofunc = read ? vec_read : vec_write;

	vres tcres = iofunc(iovs.data(), csize, false);
	assert(vokay(tcres));

	FreePaths(&paths);
	FreeIovecs(&iovs);

}
static void BM2_Read4KDirect(int start, int csize)
{
	ReadWrite4K(start, csize, O_RDONLY | O_SYNC | O_DIRECT, true);

}
static void BM2_Write4KSync(int start, int csize)
{
	ReadWrite4K(start, csize, O_WRONLY | O_CREAT | O_SYNC, false);
}

static void BM2_Append(int start, int csize)
{
	vector<const char *> paths =
	    NewPaths("Bench-Files/%04d", csize, start);
	vector<vfile> files = Paths2Files(paths);
	auto iovs = NewIovecs(files.data(), csize, TC_OFFSET_END);

	vres tcres = vec_write(iovs.data(), csize, false);
	assert(vokay(tcres));

	FreePaths(&paths);
	FreeIovecs(&iovs);
}

static void BM2_SSCopy(int start, int csize)
{
	auto pairs = NewFilePairsToCopy(FLAGS_copy_from.c_str(),
					FLAGS_copy_to.c_str(), csize, start);

	vres tcres = vec_copy(pairs.data(), csize, false);
	assert(vokay(tcres));

	FreeFilePairsToCopy(&pairs);
}

static void BM2_Symlink(int start, int csize)
{
	if (start == 0) ResetTestDirectory("Bench-Symlinks");
	vector<const char *> files =
	    NewPaths("Bench-Symlinks/file-%d", csize, start);
	vector<const char *> links =
	    NewPaths("Bench-Symlinks/link-%d", csize, start);

	vres tcres = vec_symlink(files.data(), links.data(), csize, false);
	assert(vokay(tcres));

	FreePaths(&files);
	FreePaths(&links);
}

static void BM2_Readlink(int start, int csize)
{
	vector<const char *> links =
	    NewPaths("Bench-Symlinks/link-%d", csize, start);
	vector<char *> bufs(csize);
	vector<size_t> buf_sizes(csize, PATH_MAX);

	for (size_t i = 0; i < (size_t)csize; ++i) {
		bufs[i] = (char *)malloc(PATH_MAX);
	}

	vres tcres = vec_readlink(links.data(), bufs.data(), buf_sizes.data(),
				    csize, false);
	assert(vokay(tcres));

	for (size_t i = 0; i < (size_t)csize; ++i) {
		free(bufs[i]);
	}
	FreePaths(&links);
}

static void BM2_MkdirWithContents(int start, int csize)
{
	if (start == 0) ResetTestDirectory("Bench-Dirs");
	vector<const char *> dirs = NewPaths("Bench-Dirs/dir-%d", csize, start);
	CreateDirsWithContents(dirs);
	FreePaths(&dirs);
}

static void BM2_Listdir(int start, int csize)
{
	vector<const char *> dirs = NewPaths("Bench-Dirs/dir-%d", csize, start);
	vres tcres = vec_listdir(dirs.data(), csize, VATTRS_MASK_ALL, 0,
				   false, DummyListDirCb, NULL, false);
	assert(vokay(tcres));
	FreePaths(&dirs);
}

static void BenchmarkAttrs(int start, int csize, struct vattrs *values, bool getattr)
{
	auto fn = getattr ? vec_getattrs : vec_setattrs;
	vector<vattrs> attrs = NewTcAttrs(csize, values, start);
	vres tcres = fn(attrs.data(), csize, false);
	assert(vokay(tcres));
	FreeTcAttrs(&attrs);
}

static void BM2_Getattrs(int start, int csize)
{
	BenchmarkAttrs(start, csize, nullptr, true);
}

static void BM2_Setattr1(int start, int csize)
{
	vattrs values = GetAttrValuesToSet(1);
	BenchmarkAttrs(start, csize, &values, false);
}

static void BM2_Setattr2(int start, int csize)
{
	vattrs values = GetAttrValuesToSet(2);
	BenchmarkAttrs(start, csize, &values, false);
}

static void BM2_Setattr3(int start, int csize)
{
	vattrs values = GetAttrValuesToSet(3);
	BenchmarkAttrs(start, csize, &values, false);
}

static void BM2_Setattr4(int start, int csize)
{
	vattrs values = GetAttrValuesToSet(4);
	BenchmarkAttrs(start, csize, &values, false);
}

static void BM2_Remove(int start, int csize)
{
	vector<const char *> paths = NewPaths("Bench-Files/file-%d", csize, start);
	vres tcres = vec_unlink(paths.data(), csize);
	assert(vokay(tcres));
	FreePaths(&paths);
}

static void BM2_Mkdir(int start, int csize)
{
	vector<const char *> paths = NewPaths("Bench-Dirs/dir-%d", csize, start);
	vector<vattrs> dirs(csize);

	for (size_t i = 0; i < (size_t)csize; ++i) {
		vset_up_creation(&dirs[i], paths[i], 0755);
	}
	vres tcres = vec_mkdir(dirs.data(), csize, false);
	assert(vokay(tcres));

	FreePaths(&paths);
}

static void BM2_Rename(int start, int csize)
{
	vector<const char *> srcs = NewPaths("Bench-Files/file-%d", csize, start);
	vector<const char *> dsts = NewPaths("Bench-Files/newfile-%d", csize, start);
	vector<vfile_pair> pairs(csize);

	for (size_t i = 0; i < (size_t)csize; ++i) {
		pairs[i].src_file = vfile_from_path(srcs[i]);
		pairs[i].dst_file = vfile_from_path(dsts[i]);
	}

	vres tcres = vec_rename(pairs.data(), csize, false);
	assert(vokay(tcres));

	FreePaths(&srcs);
	FreePaths(&dsts);
}

typedef void (*BM_func)(int start, int csize);

static BM_func GetBenchmarkFunction()
{
	if (FLAGS_op == "Symlink") {
		return BM2_Symlink;
	} else if (FLAGS_op == "Readlink") {
		return BM2_Readlink;
	} else if (FLAGS_op == "MkdirWithContents") {
		return BM2_MkdirWithContents;
	} else if (FLAGS_op == "CreateEmpty") {
		return BM2_CreateEmpty;
	} else if (FLAGS_op == "OpenClose") {
		return BM2_OpenClose;
	} else if (FLAGS_op == "Listdir") {
		return BM2_Listdir;
	} else if (FLAGS_op == "Getattrs") {
		return BM2_Getattrs;
	} else if (FLAGS_op == "Setattr1") {
		return BM2_Setattr1;
	} else if (FLAGS_op == "Setattr2") {
		return BM2_Setattr2;
	} else if (FLAGS_op == "Setattr3") {
		return BM2_Setattr3;
	} else if (FLAGS_op == "Setattr4") {
		return BM2_Setattr4;
	} else if (FLAGS_op == "Mkdir") {
		return BM2_Mkdir;
	} else if (FLAGS_op == "Rename") {
		return BM2_Rename;
	} else if (FLAGS_op == "Remove") {
		return BM2_Remove;
	} else if (FLAGS_op == "Append") {
		return BM2_Append;
	} else if (FLAGS_op == "SSCopy") {
		return BM2_SSCopy;
	} else if (FLAGS_op == "Read4KDirect") {
		return BM2_Read4KDirect;
	} else if (FLAGS_op == "Write4KSync") {
		return BM2_Write4KSync;
	} else {
		fprintf(stderr, "Unknown operation: %s\n", FLAGS_op.c_str());
		return nullptr;
	}
}

void Run()
{
	void *data = SetUp(FLAGS_tc);
	sca_chdir("/vfs0");
	int iters = FLAGS_nfiles / FLAGS_compound_size;
	BM_func bmfunc = GetBenchmarkFunction();
	for (int i = 0; i < iters; ++i) {
		bmfunc(i * FLAGS_compound_size, FLAGS_compound_size);
	}
	TearDown(data);
}

int main(int argc, char *argv[])
{
	std::string usage(
	    "This program benchmarks NFS operations.\nUsage: ");
	usage += argv[0];
	gflags::SetUsageMessage(usage);
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	Run();
	return 0;
}
