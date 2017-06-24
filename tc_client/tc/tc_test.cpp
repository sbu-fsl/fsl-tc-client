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
 * XXX: To add a new test, don't forget to register the test in
 * REGISTER_TYPED_TEST_CASE_P().
 *
 * This file uses an advanced GTEST feature called Type-Parameterized Test,
 * which is documented at
 * https://github.com/google/googletest/blob/master/googletest/docs/V1_7_AdvancedGuide.md
 */
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>

#include <algorithm>
#include <list>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "tc_api.h"
#include "tc_helper.h"
#include "path_utils.h"
#include "test_util.h"
#include "util/fileutil.h"
#include "path_utils.h"
#include "log.h"

#define new_auto_path(fmt, args...)                                            \
	tc_format_path((char *)alloca(PATH_MAX), fmt, ##args)

/**
 * Ensure files or directories do not exist before test.
 */
bool Removev(const char **paths, int count) {
	return vokay(vec_unlink(paths, count));
}

/**
 * Set the TC I/O vector
 */
static viovec *build_iovec(vfile *files, int count, int offset)
{
	int i = 0, N = 4096;
	viovec *iov = NULL;

	iov = (viovec *)calloc(count, sizeof(viovec));

	while (i < count) {
		viov2file(&iov[i], &files[i], offset, N, (char *)malloc(N));
		i++;
	}

	return iov;
}

static void tc_touchv(const char **paths, int count, int filesize)
{
	viovec *iovs;
	char *buf;

	iovs = (viovec *)alloca(count * sizeof(*iovs));
	buf = filesize ? getRandomBytes(filesize) : NULL;

	for (int i = 0; i < count; ++i) {
		viov4creation(&iovs[i], paths[i], filesize, buf);
	}

	EXPECT_OK(vec_write(iovs, count, false));

	if (buf) {
		free(buf);
	}
}

static inline void tc_touch(const char *path, int size)
{
	tc_touchv(&path, 1, size);
}

static inline char *tc_format_path(char *path, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintf(path, PATH_MAX, format, args);
	va_end(args);

	return path;
}

static inline void vensure_parent_dir(const char *path)
{
	char dirpath[PATH_MAX];
	slice_t dir = tc_path_dirname(path);
	strncpy(dirpath, dir.data, dir.size);
	dirpath[dir.size] = '\0';
	sca_ensure_dir(dirpath, 0755, NULL);
}

class TcPosixImpl {
public:
	static void *tcdata;
	static constexpr const char* POSIX_TEST_DIR = "/tmp/tc_posix_test";
	static void SetUpTestCase() {
		tcdata = vinit(NULL, "/tmp/tc-posix.log", 0);
		TCTEST_WARN("Global SetUp of Posix Impl\n");
		util::CreateOrUseDir(POSIX_TEST_DIR);
		if (chdir(POSIX_TEST_DIR) < 0) {
			TCTEST_ERR("chdir failed\n");
		}
	}
	static void TearDownTestCase() {
		TCTEST_WARN("Global TearDown of Posix Impl\n");
		vdeinit(tcdata);
		//sleep(120);
	}
	static void SetUp() {
		TCTEST_WARN("SetUp Posix Impl Test\n");
	}
	static void TearDown() {
		TCTEST_WARN("TearDown Posix Impl Test\n");
	}
};
void *TcPosixImpl::tcdata = NULL;

class TcNFS4Impl {
public:
	static void *tcdata;
	static void SetUpTestCase() {
		tcdata = vinit(
		    get_tc_config_file((char *)alloca(PATH_MAX), PATH_MAX),
		    "/tmp/tc-nfs4.log", 77);
		TCTEST_WARN("Global SetUp of NFS4 Impl\n");
		/* TODO: recreate test dir if exist */
		EXPECT_OK(sca_ensure_dir("/vfs0/tc_nfs4_test", 0755, NULL));
		sca_chdir("/vfs0/tc_nfs4_test");  /* change to mnt point */
	}
	static void TearDownTestCase() {
		TCTEST_WARN("Global TearDown of NFS4 Impl\n");
		vdeinit(tcdata);
	}
	static void SetUp() {
		TCTEST_WARN("SetUp NFS4 Impl Test\n");
	}
	static void TearDown() {
		TCTEST_WARN("TearDown NFS4 Impl Test\n");
	}
};
void *TcNFS4Impl::tcdata = NULL;

template <typename T>
class TcTest : public ::testing::Test {
public:
	static void SetUpTestCase() {
		T::SetUpTestCase();
	}
	static void TearDownTestCase() {
		T::TearDownTestCase();
	}
	void SetUp() override {
		T::SetUp();
	}
	void TearDown() override {
		T::TearDown();
	}
};

TYPED_TEST_CASE_P(TcTest);

/**
 * TC-Read and Write test using
 * File path
 */
TYPED_TEST_P(TcTest, WritevCanCreateFiles)
{
	const char *PATHS[] = { "WritevCanCreateFiles1.txt",
				"WritevCanCreateFiles2.txt",
				"WritevCanCreateFiles3.txt",
				"WritevCanCreateFiles4.txt" };
	const int count = sizeof(PATHS)/sizeof(PATHS[0]);

	Removev(PATHS, count);

	viovec *writev = (viovec *)calloc(count, sizeof(viovec));
	for (int i = 0; i < count; ++i) {
		viov4creation(&writev[i], PATHS[i], 4096,
				getRandomBytes(4096));
	}

	EXPECT_OK(vec_write(writev, count, false));

	viovec *readv = (viovec *)calloc(count, sizeof(viovec));
	for (int i = 0; i < count; ++i) {
		viov2path(&readv[i], PATHS[i], 0, 4096,
			    (char *)malloc(4096));
	}

	EXPECT_OK(vec_read(readv, count, false));

	EXPECT_TRUE(compare_content(writev, readv, count));

	free_iovec(writev, count);
	free_iovec(readv, count);
}

/**
 * TC-Read and Write test using
 * File Descriptor
 */
TYPED_TEST_P(TcTest, TestFileDesc)
{
	const int N = 4;
	const char *PATHS[] = { "TcTest-TestFileDesc1.txt",
				"TcTest-TestFileDesc2.txt",
				"TcTest-TestFileDesc3.txt",
				"TcTest-TestFileDesc4.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_RDWR | O_CREAT, 0);
	EXPECT_NOTNULL(files);

	struct viovec *writev = NULL;
	writev = build_iovec(files, N, 0);
	EXPECT_FALSE(writev == NULL);

	EXPECT_OK(vec_write(writev, N, false));

	struct viovec *readv = NULL;
	readv = build_iovec(files, N, 0);
	EXPECT_FALSE(readv == NULL);

	EXPECT_OK(vec_read(readv, N, false));

	EXPECT_TRUE(compare_content(writev, readv, N));

	vec_close(files, N);
	free_iovec(writev, N);
	free_iovec(readv, N);
}

/**
 * Compare the attributes once set, to check if set properly
 */

bool compare_attrs(vattrs *attrs1, vattrs *attrs2, int count)
{
	int i = 0;
	vattrs *a = NULL;
	vattrs *b = NULL;

	for (i = 0; i < count; ++i) {
		a = attrs1 + i;
		b = attrs2 + i;
		if (a->masks.has_mode != b->masks.has_mode)
			return false;
		if (a->masks.has_mode &&
		    (a->mode & (S_IRWXU | S_IRWXG | S_IRWXO)) !=
		    (b->mode & (S_IRWXU | S_IRWXG | S_IRWXO))) {
			TCTEST_WARN("Mode does not match: %x vs %x\n",
				    a->mode, b->mode);
			TCTEST_WARN("TYPE BITS: %x vs %x\n", (a->mode & S_IFMT),
				    (b->mode & S_IFMT));
			TCTEST_WARN("OWNER BITS: %x vs %x\n",
				    (a->mode & S_IRWXU), (b->mode & S_IRWXU));
			TCTEST_WARN("GROUP BITS: %x vs %x\n",
				    (a->mode & S_IRWXO), (b->mode & S_IRWXO));
			return false;
		}

		if (a->masks.has_rdev != b->masks.has_rdev)
			return false;
		if (a->masks.has_rdev && a->rdev != b->rdev) {
			TCTEST_WARN("rdev does not match\n");
			TCTEST_WARN(" %lu %lu\n", a->rdev, b->rdev);
			return false;
		}

		if (a->masks.has_nlink != b->masks.has_nlink)
			return false;
		if (a->masks.has_nlink && a->nlink != b->nlink) {
			TCTEST_WARN("nlink does not match\n");
			TCTEST_WARN(" %lu %lu\n", a->nlink, b->nlink);
			return false;
		}

		if (a->masks.has_uid != b->masks.has_uid)
			return false;
		if (a->masks.has_uid && a->uid != b->uid) {
			TCTEST_WARN("uid does not match\n");
			TCTEST_WARN(" %d %d\n", a->uid, b->uid);
			return false;
		}

		if (a->masks.has_gid != b->masks.has_gid)
			return false;
		if (a->masks.has_gid && a->gid != b->gid) {
			TCTEST_WARN("gid does not match\n");
			TCTEST_WARN(" %d %d\n", a->gid, b->gid);
			return false;
		}

		if (a->masks.has_ctime != b->masks.has_ctime)
			return false;
		if (a->masks.has_ctime &&
		    memcmp((void *)&(a->ctime), (void *)&(b->ctime),
			   sizeof(b->ctime))) {
			TCTEST_WARN("ctime does not match\n");
			return false;
		}

		if (a->masks.has_mtime != b->masks.has_mtime)
			return false;
		if (a->masks.has_mtime &&
		    memcmp((void *)&(a->mtime), (void *)&(b->mtime),
			   sizeof(b->mtime))) {
			TCTEST_WARN("mtime does not match\n");
			return false;
		}
	}

	return true;
}

static inline struct timespec totimespec(long sec, long nsec)
{
	struct timespec tm = {
		.tv_sec = sec,
		.tv_nsec = nsec,
	};
	return tm;
}

/**
 * Set the TC test Attributes
 */
static vattrs *set_vattrs(struct vattrs *attrs, int count)
{
	int i = 0;
	const int N = 3;
	uid_t uid[N] = { 2711, 456, 789 };
	gid_t gid[N] = { 87, 4566, 2311 };
	mode_t mode[N] = { S_IRUSR | S_IRGRP | S_IROTH,
			  S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH, S_IRWXU };
	size_t size[N] = { 256, 56, 125 };
	time_t atime[N] = { time(NULL), 1234, 567 };

	for (i = 0; i < count; ++i) {
		int j = i % N;
		vattrs_set_mode(attrs + i, mode[j]);
		vattrs_set_size(attrs + i, size[j]);
		vattrs_set_uid(attrs + i, uid[j]);
		vattrs_set_gid(attrs + i, gid[j]);
		vattrs_set_atime(attrs + i, totimespec(atime[j], 0));
		vattrs_set_atime(attrs + i, totimespec(time(NULL), 0));
	}

	return attrs;
}

/* Set the TC attributes masks */
/*
static void set_attr_masks(vattrs *write, vattrs *read, int count)
{
	int i = 0;
	for (i = 0; i < count; ++i) {
		read[i].file = write[i].file;
		read[i].masks = write[i].masks;
	}
}
*/

/**
 * TC-Set/Get Attributes test
 * using File Path
 */
TYPED_TEST_P(TcTest, AttrsTestPath)
{
	const char *PATH[] = { "WritevCanCreateFiles1.txt",
			       "WritevCanCreateFiles2.txt",
			       "WritevCanCreateFiles3.txt" };
	int i;
	const int count = 3;
	struct vattrs *attrs1 = (vattrs *)calloc(count, sizeof(vattrs));
	struct vattrs *attrs2 = (vattrs *)calloc(count, sizeof(vattrs));

	EXPECT_NOTNULL(attrs1);
	EXPECT_NOTNULL(attrs2);

	for (i = 0; i < count; ++i) {
		attrs1[i].file = vfile_from_path(PATH[i]);
		attrs2[i].file = attrs1[i].file;
	}

	attrs1 = set_vattrs(attrs1, count);
	EXPECT_OK(vec_setattrs(attrs1, count, false));

	for (i = 0; i < count; ++i) {
		attrs2[i].masks = attrs1[i].masks;
	}
	EXPECT_OK(vec_getattrs(attrs2, count, false));

	EXPECT_TRUE(compare_attrs(attrs1, attrs2, count));

	free(attrs1);
	free(attrs2);
}

TYPED_TEST_P(TcTest, TestHardLinks)
{
	const char *dirname = "HardLinks";
	EXPECT_OK(vec_unlink_recursive(&dirname, 1));
	sca_ensure_dir("HardLinks", 0755, NULL);
	const int NFILES = 8;
	std::vector<const char *> files(NFILES);
	std::vector<const char *> links(NFILES);
	std::vector<viovec> olddata(NFILES);
	std::vector<viovec> newdata(NFILES);
	for (int i = 0; i < NFILES; ++i) {
		files[i] = new_auto_path("HardLinks/file-%d", i);
		links[i] = new_auto_path("HardLinks/link-%d", i);
		olddata[i].file = vfile_from_path(files[i]);
		newdata[i].file = vfile_from_path(links[i]);
		olddata[i].offset = newdata[i].offset = 0;
		olddata[i].length = newdata[i].length = 4096;
		olddata[i].data = (char *)malloc(4096);
		newdata[i].data = (char *)malloc(4096);
	}

	tc_touchv(files.data(), files.size(), false);
	EXPECT_OK(vec_read(olddata.data(), olddata.size(), false));

	EXPECT_OK(vec_hardlink(files.data(), links.data(), files.size(), false));
	EXPECT_OK(vec_unlink(files.data(), files.size()));

	EXPECT_OK(vec_read(newdata.data(), newdata.size(), false));
	EXPECT_TRUE(compare_content(olddata.data(), newdata.data(), olddata.size()));

	for (int i = 0; i < NFILES; ++i) {
		free((char *)olddata[i].data);
		free((char *)newdata[i].data);
	}
}

/**
 * TC-Set/Get Attributes test
 * with symlinks
 */
TYPED_TEST_P(TcTest, AttrsTestSymlinks)
{
	const char *PATHS[] = { "AttrsTestSymlinks-Linked1.txt",
				"AttrsTestSymlinks-Linked2.txt",
				"AttrsTestSymlinks-Linked3.txt" };
	const char *LPATHS[] = { "AttrsTestSymlinks-Link1.txt",
				 "AttrsTestSymlinks-Link2.txt",
				 "AttrsTestSymlinks-Link3.txt" };
	struct viovec iov;
	int i;
	const int count = 3;
	struct vattrs *attrs1 = (vattrs *)calloc(count, sizeof(vattrs));
	struct vattrs *attrs2 = (vattrs *)calloc(count, sizeof(vattrs));

	EXPECT_NOTNULL(attrs1);
	EXPECT_NOTNULL(attrs2);

	Removev(PATHS, count);
	Removev(LPATHS, count);

	EXPECT_OK(vec_symlink(PATHS, LPATHS, count, false));

	for (i = 0; i < count; ++i) {
		viov4creation(&iov, PATHS[i], 100, getRandomBytes(100));
		EXPECT_NOTNULL(iov.data);
		EXPECT_OK(vec_write(&iov, 1, false));

		attrs1[i].file = vfile_from_path(LPATHS[i]);
		vattrs_set_mode(&attrs1[i], S_IRUSR);
		vattrs_set_atime(&attrs1[i], totimespec(time(NULL), 0));
		attrs2[i] = attrs1[i];
	}

	EXPECT_OK(vec_setattrs(attrs1, count, false));
	EXPECT_OK(vec_getattrs(attrs2, count, false));
	EXPECT_TRUE(compare_attrs(attrs1, attrs2, count));

	vattrs_set_mode(&attrs1[0], S_IRUSR | S_IRGRP);
	EXPECT_OK(vec_setattrs(attrs1, count, false));
	EXPECT_OK(vec_lgetattrs(attrs2, count, false));

	EXPECT_FALSE(S_IROTH & attrs1[0].mode);
	EXPECT_TRUE(S_IROTH & attrs2[0].mode);
	EXPECT_FALSE(compare_attrs(attrs1, attrs2, count));

	EXPECT_OK(vec_getattrs(attrs2, count, false));
	EXPECT_TRUE(compare_attrs(attrs1, attrs2, count));

	free(attrs1);
	free(attrs2);
}

/*
 * TC-Set/Get Attributes test
 * using File Descriptor
 */
TYPED_TEST_P(TcTest, AttrsTestFileDesc)
{
	const char *PATHS[] = { "WritevCanCreateFiles4.txt",
			       "WritevCanCreateFiles5.txt",
			       "WritevCanCreateFiles6.txt" };
	int i = 0;
	const int count = 3;
	vfile *tcfs;
	struct vattrs *attrs1 = (vattrs *)calloc(count, sizeof(vattrs));
	struct vattrs *attrs2 = (vattrs *)calloc(count, sizeof(vattrs));

	EXPECT_NOTNULL(attrs1);
	EXPECT_NOTNULL(attrs2);

	Removev(PATHS, count);
	tcfs = vec_open_simple(PATHS, count, O_RDWR | O_CREAT, 0);
	EXPECT_NOTNULL(tcfs);

	for (int i = 0; i < count; ++i) {
		attrs2[i].file = attrs1[i].file = tcfs[i];
	}

	set_vattrs(attrs1, count);
	EXPECT_OK(vec_setattrs(attrs1, count, false));

	for (i = 0; i < count; ++i) {
		attrs2[i].masks = attrs1[i].masks;
	}
	EXPECT_OK(vec_getattrs(attrs2, count, false));

	EXPECT_TRUE(compare_attrs(attrs1, attrs2, count));

	vec_close(tcfs, count);

	free(attrs1);
	free(attrs2);
}

/*
 * Set attributes of many files.
 */
TYPED_TEST_P(TcTest, SetAttrsOfManyFiles)
{
	const int N = 32;
	const char *PATHS[N];

	for (int i = 0; i < N; ++i) {
		char *p = (char *)malloc(64);
		snprintf(p, 64, "SetAttrsOfFile-%03d", i);
		PATHS[i] = p;
	}
	struct vattrs *attrs1 = (vattrs *)calloc(N, sizeof(struct vattrs));
	struct vattrs *attrs2 = (vattrs *)calloc(N, sizeof(struct vattrs));
	EXPECT_NOTNULL(attrs1);
	EXPECT_NOTNULL(attrs2);

	vfile *tcfs = vec_open_simple(PATHS, N, O_RDWR | O_CREAT, 0);
	EXPECT_NOTNULL(tcfs);

	for (int i = 0; i < N; ++i) {
		attrs2[i].file = attrs1[i].file = tcfs[i];
	}

	set_vattrs(attrs1, N);
	EXPECT_OK(vec_setattrs(attrs1, N, false));

	for (int i = 0; i < N; ++i) {
		attrs2[i].masks = attrs1[i].masks;
	}
	EXPECT_OK(vec_getattrs(attrs2, N, false));

	EXPECT_TRUE(compare_attrs(attrs1, attrs2, N));

	vec_close(tcfs, N);

	for (int i = 0; i < N; ++i) {
		free((void *)PATHS[i]);
	}
	free(attrs1);
	free(attrs2);
}

static int tc_cmp_attrs_by_name(const void *a, const void *b)
{
	const vattrs *attrs1 = (const vattrs *)a;
	const vattrs *attrs2 = (const vattrs *)b;
	return strcmp(attrs1->file.path, attrs2->file.path);
}

/**
 * List Directory Contents Test
 */
TYPED_TEST_P(TcTest, ListDirContents)
{
	const char *DIR_PATH = "TcTest-ListDir";
	vattrs *contents;
	int count = 0;

	EXPECT_OK(sca_ensure_dir(DIR_PATH, 0755, 0));
	tc_touch("TcTest-ListDir/file1.txt", 1);
	tc_touch("TcTest-ListDir/file2.txt", 2);
	tc_touch("TcTest-ListDir/file3.txt", 3);

	EXPECT_OK(sca_listdir(DIR_PATH, VATTRS_MASK_ALL, 3, false, &contents,
			     &count));
	EXPECT_EQ(3, count);
	qsort(contents, count, sizeof(*contents), tc_cmp_attrs_by_name);

	vattrs *read_attrs = (vattrs *)calloc(count, sizeof(vattrs));
	read_attrs[0].file = vfile_from_path("TcTest-ListDir/file1.txt");
	read_attrs[1].file = vfile_from_path("TcTest-ListDir/file2.txt");
	read_attrs[2].file = vfile_from_path("TcTest-ListDir/file3.txt");
	read_attrs[0].masks = read_attrs[1].masks = read_attrs[2].masks =
	    VATTRS_MASK_ALL;
	EXPECT_OK(vec_getattrs(read_attrs, count, false));

	EXPECT_TRUE(compare_attrs(contents, read_attrs, count));

	vfree_attrs(contents, count, true);
	free(read_attrs);
}

TYPED_TEST_P(TcTest, ListLargeDir)
{
	EXPECT_OK(sca_ensure_dir("TcTest-ListLargeDir", 0755, 0));
	buf_t *name = new_auto_buf(PATH_MAX);
	const int N = 512;
	for (int i = 1; i <= N; ++i) {
		buf_printf(name, "TcTest-ListLargeDir/large-file%05d", i);
		tc_touch(asstr(name), i);
	}

	vattrs *contents;
	int count = 0;
	EXPECT_OK(sca_listdir("TcTest-ListLargeDir", VATTRS_MASK_ALL, 0,
			     false, &contents, &count));
	EXPECT_EQ(N, count);
	qsort(contents, count, sizeof(*contents), tc_cmp_attrs_by_name);
	for (int i = 1; i <= N; ++i) {
		buf_printf(name, "TcTest-ListLargeDir/large-file%05d", i);
		EXPECT_STREQ(asstr(name), contents[i - 1].file.path);
	}
	vfree_attrs(contents, count, true);
}

TYPED_TEST_P(TcTest, ListDirRecursively)
{
	EXPECT_OK(sca_ensure_dir("TcTest-ListDirRecursively/00/00", 0755, 0));
	EXPECT_OK(sca_ensure_dir("TcTest-ListDirRecursively/00/01", 0755, 0));
	EXPECT_OK(sca_ensure_dir("TcTest-ListDirRecursively/01", 0755, 0));

	tc_touch("TcTest-ListDirRecursively/00/00/1.txt", 0);
	tc_touch("TcTest-ListDirRecursively/00/00/2.txt", 0);
	tc_touch("TcTest-ListDirRecursively/00/01/3.txt", 0);
	tc_touch("TcTest-ListDirRecursively/00/01/4.txt", 0);
	tc_touch("TcTest-ListDirRecursively/01/5.txt", 0);

	vattrs *contents;
	int count = 0;
	EXPECT_OK(sca_listdir("TcTest-ListDirRecursively", VATTRS_MASK_ALL, 0,
			     true, &contents, &count));
	qsort(contents, count, sizeof(*contents), tc_cmp_attrs_by_name);
	const char *expected[] = {
		"TcTest-ListDirRecursively/00",
		"TcTest-ListDirRecursively/00/00",
		"TcTest-ListDirRecursively/00/00/1.txt",
		"TcTest-ListDirRecursively/00/00/2.txt",
		"TcTest-ListDirRecursively/00/01",
		"TcTest-ListDirRecursively/00/01/3.txt",
		"TcTest-ListDirRecursively/00/01/4.txt",
		"TcTest-ListDirRecursively/01",
		"TcTest-ListDirRecursively/01/5.txt",
	};
	EXPECT_EQ((size_t)count, sizeof(expected) / sizeof(expected[0]));
	for (int i = 0; i < count; ++i) {
		EXPECT_STREQ(expected[i], contents[i].file.path);
	}
	vfree_attrs(contents, count, true);
}

/**
 * Rename File Test
 */
TYPED_TEST_P(TcTest, RenameFile)
{
	int i = 0;
	const char *src_path[] = { "WritevCanCreateFiles1.txt",
				   "WritevCanCreateFiles2.txt",
				   "WritevCanCreateFiles3.txt",
				   "WritevCanCreateFiles4.txt" };

	const char *dest_path[] = { "rename1.txt", "rename2.txt",
				    "rename3.txt", "rename4.txt" };
	std::vector<vfile_pair> files(4);
	for (i = 0; i < 4; ++i) {
		files[i].src_file = vfile_from_path(src_path[i]);
		files[i].dst_file = vfile_from_path(dest_path[i]);
	}

	EXPECT_OK(vec_rename(files.data(), 4, false));

	/* TODO use listdir to check src files no longer exist */
}

/**
 * Remove File Test
 */
TYPED_TEST_P(TcTest, RemoveFileTest)
{
	const char *path[] = { "rename1.txt", "rename2.txt",
			       "rename3.txt", "rename4.txt" };
	std::vector<vfile> files(4);
	for (int i = 0; i < 4; ++i) {
		files[i] = vfile_from_path(path[i]);
	}

	EXPECT_OK(vec_remove(files.data(), 4, false));
}

TYPED_TEST_P(TcTest, MakeDirectories)
{
	const char *path[] = { "a", "b", "c" };
	struct vattrs dirs[3];

	Removev(path, 3);

	for (int i = 0; i < 3; ++i) {
		vset_up_creation(&dirs[i], path[i], 0755);
	}

	EXPECT_OK(vec_mkdir(dirs, 3, false));
}

TYPED_TEST_P(TcTest, MakeManyDirsDontFitInOneCompound)
{
	const int NDIRS = 64;
	std::vector<vattrs> dirs;
	const char *dirname = "ManyDirs";

	EXPECT_OK(vec_unlink_recursive(&dirname, 1));
	char buf[PATH_MAX];
	std::vector<std::string> paths;

	for (int i = 0; i < NDIRS; ++i) {
		snprintf(buf, PATH_MAX, "ManyDirs/a%d/b/c/d/e/f/g/h", i);
		std::string p(buf);
		size_t n = p.length();
		while (n != std::string::npos) {
			paths.emplace_back(p.data(), n);
			n = p.find_last_of('/', n - 1);
		}
	}

	std::sort(paths.begin(), paths.end());
	auto end = std::unique(paths.begin(), paths.end());
	for (auto it = paths.begin(); it != end; ++it) {
		vattrs tca;
		vset_up_creation(&tca, it->c_str(), 0755);
		dirs.push_back(tca);
	}

	EXPECT_OK(vec_mkdir(dirs.data(), dirs.size(), false));
}

/**
 * Append test case
 */
TYPED_TEST_P(TcTest, Append)
{
	const char *PATH = "TcTest-Append.txt";
	int i = 0;
	const int N = 4096;
	char *data;
	char *data_read;
	struct viovec iov;

	Removev(&PATH, 1);

	data = (char *)getRandomBytes(3 * N);
	data_read = (char *)malloc(3 * N);
	EXPECT_NOTNULL(data);
	EXPECT_NOTNULL(data_read);

	viov4creation(&iov, PATH, N, data);

	EXPECT_OK(vec_write(&iov, 1, false));

	for (i = 0; i < 2; ++i) {
		iov.offset = TC_OFFSET_END;
		iov.data = data + N * (i + 1);
		iov.is_creation = false;
		EXPECT_OK(vec_write(&iov, 1, false));
	}

	iov.offset = 0;
	iov.length = 3 * N;
	iov.data = data_read;
	EXPECT_OK(vec_read(&iov, 1, false));
	EXPECT_TRUE(iov.is_eof);
	EXPECT_EQ((size_t)(3 * N), iov.length);
	EXPECT_EQ(0, memcmp(data, data_read, 3 * N));

	free(data);
	free(data_read);
}

TYPED_TEST_P(TcTest, SuccessiveReads)
{
	const char *path = "TcTest-SuccesiveReads.txt";
	struct viovec iov;
	const int N = 4096;
	char *data;
	char *read;
	vfile *tcf;

	Removev(&path, 1);

	data = (char *)getRandomBytes(5 * N);
	viov4creation(&iov, path, 5 * N, data);

	EXPECT_OK(vec_write(&iov, 1, false));

	read = (char *)malloc(5 * N);
	EXPECT_NOTNULL(read);

	tcf = sca_open(path, O_RDONLY, 0);
	EXPECT_EQ(0, sca_fseek(tcf, 0, SEEK_CUR));
	EXPECT_NOTNULL(tcf);
	viov2file(&iov, tcf, TC_OFFSET_CUR, N, read);
	EXPECT_OK(vec_read(&iov, 1, false));
	EXPECT_EQ(N, sca_fseek(tcf, 0, SEEK_CUR));

	iov.data = read + N;
	EXPECT_OK(vec_read(&iov, 1, false));
	EXPECT_EQ(2 * N, sca_fseek(tcf, 0, SEEK_CUR));

	EXPECT_EQ(3 * N, sca_fseek(tcf, N, SEEK_CUR));
	iov.data = read + 3 * N;
	EXPECT_OK(vec_read(&iov, 1, false));

	EXPECT_EQ(2 * N, sca_fseek(tcf, 2 * N, SEEK_SET));
	iov.data = read + 2 * N;
	EXPECT_OK(vec_read(&iov, 1, false));

	EXPECT_EQ(4 * N, sca_fseek(tcf, -N, SEEK_END));
	iov.data = read + 4 * N;
	EXPECT_OK(vec_read(&iov, 1, false));
	EXPECT_TRUE(iov.is_eof);

	EXPECT_EQ(0, memcmp(data, read, 5 * N));

	free(data);
	free(read);
	sca_close(tcf);
}

TYPED_TEST_P(TcTest, SuccessiveWrites)
{
	const char *path = "SuccesiveWrites.dat";
	char *data = (char *)getRandomBytes(16_KB);
	/**
	 * open file one for actual writing
	 * other descriptor to verify
	 */
	vfile *tcf = sca_open(path, O_RDWR | O_CREAT, 0755);
	EXPECT_NOTNULL(tcf);
	vfile *tcf2 = sca_open(path, O_RDONLY, 0);
	EXPECT_NE(tcf->fd, tcf2->fd);

	struct viovec iov;
	viov2file(&iov, tcf, TC_OFFSET_CUR, 4_KB, data);
	EXPECT_OK(vec_write(&iov, 1, false));
	viov2file(&iov, tcf, TC_OFFSET_CUR, 4_KB, data + 4_KB);
	EXPECT_OK(vec_write(&iov, 1, false));

	char *readbuf = (char *)malloc(16_KB);
	viov2file(&iov, tcf2, 0, 8_KB, readbuf);
	EXPECT_OK(vec_read(&iov, 1, false));
	EXPECT_EQ(iov.length, 8_KB);
	EXPECT_EQ(0, memcmp(data, readbuf, 8_KB));

	viov2file(&iov, tcf, TC_OFFSET_CUR, 8_KB, data + 8_KB);
	EXPECT_OK(vec_write(&iov, 1, false));

	viov2file(&iov, tcf2, 0, 16_KB, readbuf);
	EXPECT_OK(vec_read(&iov, 1, false));
	EXPECT_EQ(iov.length, 16_KB);
	EXPECT_EQ(0, memcmp(data, readbuf, 16_KB));

	sca_close(tcf);
	sca_close(tcf2);
	free(data);
	free(readbuf);
}

TYPED_TEST_P(TcTest, SessionTimeout)
{
	const char *path = "SessionTimeout.dat";
	struct viovec iov = {0};
	int size = 4096;
	char *data1 = getRandomBytes(size);

	viov4creation(&iov, path, size, data1);

	EXPECT_OK(vec_write(&iov, 1, false));

	sleep(5);

	viov2path(&iov, path, 0, size, data1);

	EXPECT_OK(vec_read(&iov, 1, false));

	free(data1);
}

static void CopyOrDupFiles(const char *dir, bool copy, int nfiles)
{
	const int N = 4096;
	std::vector<struct vextent_pair> pairs(nfiles, {0});
	std::vector<struct viovec> iovs(nfiles, {0});
	std::vector<struct viovec> read_iovs(nfiles, {0});
	std::vector<std::string> src_paths(nfiles);
	std::vector<std::string> dst_paths(nfiles);
	char buf[PATH_MAX];

	EXPECT_OK(vec_unlink_recursive(&dir, 1));
	EXPECT_OK(sca_ensure_dir(dir, 0755, NULL));

	for (int i = 0; i < nfiles; ++i) {
		src_paths[i].assign(
		    buf, snprintf(buf, PATH_MAX, "%s/src-%d.txt", dir, i));
		dst_paths[i].assign(
		    buf, snprintf(buf, PATH_MAX, "%s/dst-%d.txt", dir, i));
		vfill_extent_pair(&pairs[i], src_paths[i].c_str(), 0,
				  dst_paths[i].c_str(), 0, N);

		viov4creation(&iovs[i], pairs[i].src_path, N,
			      getRandomBytes(N));
		EXPECT_NOTNULL(iovs[i].data);

		viov2path(&read_iovs[i], pairs[i].dst_path, 0, N,
			  (char *)malloc(N));
		EXPECT_NOTNULL(read_iovs[i].data);
	}

	EXPECT_OK(vec_write(iovs.data(), nfiles, false));

	// copy or move files
	if (copy) {
		EXPECT_OK(vec_copy(pairs.data(), nfiles, false));
	} else {
		EXPECT_OK(vec_dup(pairs.data(), nfiles, false));
	}

	EXPECT_OK(vec_read(read_iovs.data(), nfiles, false));

	compare_content(iovs.data(), read_iovs.data(), nfiles);

	for (int i = 0; i < nfiles; ++i) {
		free(iovs[i].data);
		free(read_iovs[i].data);
	}
}

TYPED_TEST_P(TcTest, CopyFiles)
{
	SCOPED_TRACE("CopyFiles");
	CopyOrDupFiles("TestCopy", true, 2);
	CopyOrDupFiles("TestCopy", true, 64);
}

TYPED_TEST_P(TcTest, DupFiles)
{
	SCOPED_TRACE("DupFiles");
	CopyOrDupFiles("TestDup", false, 2);
	CopyOrDupFiles("TestDup", false, 64);
}

TYPED_TEST_P(TcTest, CopyLargeDirectory)
{
	int i;
	int count;
	struct vattrs *contents;
	struct vattrs_masks masks = VATTRS_MASK_NONE;
	struct vextent_pair *dir_copy_pairs = NULL;
	char *dst_path;
	int file_count = 0;
	//Cannot be larger than 9999 or will not fit in str
	#define FILE_COUNT 10
	#define FILE_LENGTH_BYTES (100)
	struct viovec iov[FILE_COUNT];

	EXPECT_OK(sca_ensure_dir("TcTest-CopyLargeDirectory", 0755, NULL));
	EXPECT_OK(sca_ensure_dir("TcTest-CopyLargeDirectory-Dest", 0755, NULL));

	for (i = 0; i < FILE_COUNT; i++) {
		char *path = (char*) alloca(PATH_MAX);
		char *str = (char*) alloca(5);
		sprintf(str, "%d", i);
		tc_path_join("TcTest-CopyLargeDirectory", str, path, PATH_MAX);
		viov4creation(&iov[i], path, FILE_LENGTH_BYTES,
				getRandomBytes(FILE_LENGTH_BYTES));
		EXPECT_NOTNULL(iov[i].data);
	}
	EXPECT_OK(vec_write(iov, FILE_COUNT, false));

	masks.has_mode = true;

	EXPECT_OK(sca_listdir("TcTest-CopyLargeDirectory", masks, 0, true,
			     &contents, &count));

	dir_copy_pairs = (struct vextent_pair *)alloca(
	    sizeof(struct vextent_pair) * count);

	for (i = 0; i < count; i++) {
		dst_path = (char *) malloc(sizeof(char) * PATH_MAX);
		const char *dst_suffix = contents[i].file.path;

		while (*dst_suffix++ != '/')

			tc_path_join("TcTest-CopyLargeDirectory-Dest",
				     dst_suffix, dst_path, PATH_MAX);

		if (!S_ISDIR(contents[i].mode)) {
			dir_copy_pairs[file_count].src_path =
			    contents[i].file.path;
			dir_copy_pairs[file_count].dst_path = dst_path;
			dir_copy_pairs[file_count].src_offset = 0;
			dir_copy_pairs[file_count].dst_offset = 0;
			dir_copy_pairs[file_count].length = 0;

			file_count++;
		} else {
			EXPECT_OK(sca_ensure_dir (dst_path, 0755, NULL));
			free(dst_path);
		}

	}


	EXPECT_OK(vec_copy(dir_copy_pairs, file_count, false));
	for (i = 0; i < file_count; i++) {
		free((char *) dir_copy_pairs[i].dst_path);
	}
}

TYPED_TEST_P(TcTest, RecursiveCopyDirWithSymlinks)
{
#define TCT_RCD_DIR "RecursiveCopyDirWithSymlinks"
	const char *dirname1 = TCT_RCD_DIR;
	const char *dirname2 = "RCDest";

	vec_unlink_recursive(&dirname1, 1);
	vec_unlink_recursive(&dirname2, 1);
	EXPECT_OK(sca_ensure_dir(TCT_RCD_DIR, 0755, NULL));
	const int NFILES = 8;
	const char * files[NFILES];
	for (int i = 0; i < NFILES; ++i) {
		files[i] =
		    new_auto_path(TCT_RCD_DIR "/file-%d", i);
	}
	tc_touchv(files, NFILES, false);
	EXPECT_EQ(0, sca_symlink("file-0", TCT_RCD_DIR "/link"));

	EXPECT_OK(
	    sca_cp_recursive(TCT_RCD_DIR, "RCDest", false, true));
	char buf[PATH_MAX];
	EXPECT_EQ(0, sca_readlink("RCDest/link", buf, PATH_MAX));
	EXPECT_STREQ("file-0", buf);
#undef TCT_RCD_DIR
}

TYPED_TEST_P(TcTest, CopyFirstHalfAsSecondHalf)
{
	const int N = 8096;
	struct vextent_pair pairs[2];
	struct viovec iov;
	struct viovec read_iov;

	pairs[0].src_path = "OriginalFile.txt";
	pairs[0].src_offset = 0;
	pairs[0].dst_path = "ReversedFile.txt";
	pairs[0].dst_offset = N / 2;
	pairs[0].length = N / 2;

	pairs[1].src_path = "OriginalFile.txt";
	pairs[1].src_offset = N / 2;
	pairs[1].dst_path = "ReversedFile.txt";
	pairs[1].dst_offset = 0;
	pairs[1].length = UINT64_MAX;  // from src_offset to EOF, i.e., N/2

	// create source files
	viov4creation(&iov, pairs[0].src_path, N, getRandomBytes(N));
	EXPECT_NOTNULL(iov.data);
	EXPECT_OK(vec_write(&iov, 1, false));

	// remove dest files
	Removev(&pairs[0].dst_path, 1);

	// reverse a file using copy
	EXPECT_OK(vec_copy(pairs, 2, false));

	viov2path(&read_iov, pairs[1].dst_path, 0, N, (char *)malloc(N));
	EXPECT_NOTNULL(read_iov.data);

	EXPECT_OK(vec_read(&read_iov, 1, false));

	EXPECT_EQ(0, memcmp(iov.data, read_iov.data + N / 2, N / 2));
	EXPECT_EQ(0, memcmp(iov.data + N / 2, read_iov.data, N / 2));

	free(iov.data);
	free(read_iov.data);
}

TYPED_TEST_P(TcTest, CopyManyFilesDontFitInOneCompound)
{
	const int NFILES = 64;
	struct vextent_pair pairs[NFILES];
	char path[PATH_MAX];

	for (int i = 0; i < NFILES; ++i) {
		snprintf(path, PATH_MAX, "CopyMany/a%d/b/c/d/e/f/g/h", i);
		sca_ensure_dir(path, 0755, NULL);

		snprintf(path, PATH_MAX, "CopyMany/a%d/b/c/d/e/f/g/h/foo", i);
		tc_touch(path, 4_KB);

		char *dest_file = (char *)alloca(PATH_MAX);
		snprintf(dest_file, PATH_MAX, "CopyMany/foo%d", i);
		vfill_extent_pair(&pairs[i], path, 0, dest_file, 0,
				    UINT64_MAX);
	}

	EXPECT_OK(vec_copy(pairs, NFILES, false));
}

TYPED_TEST_P(TcTest, ListAnEmptyDirectory)
{
	const char *PATH = "TcTest-EmptyDir";
	vattrs *contents;
	int count;

	sca_ensure_dir(PATH, 0755, NULL);
	EXPECT_OK(
	    sca_listdir(PATH, VATTRS_MASK_ALL, 1, false, &contents, &count));
	EXPECT_EQ(0, count);
	EXPECT_EQ(NULL, contents);
}

/* Get "cannot access" error when listing 2nd-level dir.  */
TYPED_TEST_P(TcTest, List2ndLevelDir)
{
	const char *DIR_PATH = "TcTest-Dir/nested-dir";
	const char *FILE_PATH = "TcTest-Dir/nested-dir/foo";
	vattrs *attrs;
	int count;

	sca_ensure_dir(DIR_PATH, 0755, NULL);
	tc_touch(FILE_PATH, 0);
	EXPECT_OK(
	    sca_listdir(DIR_PATH, VATTRS_MASK_ALL, 1, false, &attrs, &count));
	EXPECT_EQ(1, count);
	EXPECT_EQ(0, attrs->size);
	vfree_attrs(attrs, count, true);
}

TYPED_TEST_P(TcTest, ShuffledRdWr)
{
	const char *PATH = "TcTest-ShuffledRdWr.dat";
	const int N = 8;  /* size of iovs */
	struct viovec iovs[N] = {0};
	const int S = 4096;
	tc_touch(PATH, N * S);

	char *data1 = getRandomBytes(N * S);
	char *data2 = (char *)malloc(N * S);
	std::vector<int> offsets(N);
	std::iota(offsets.begin(), offsets.end(), 0);
	std::mt19937 rng(8887);
	for (int i = 0; i < 10; ++i) { // repeat for 10 times
		for (int n = 0; n < N; ++n) {
			viov2path(&iovs[n], PATH, offsets[n] * S, S,
				    data1 + offsets[n] * S);
		}
		EXPECT_OK(vec_write(iovs, N, false));

		for (int n = 0; n < N; ++n) {
			iovs[n].data = data2 + offsets[n] * S;
		}
		EXPECT_OK(vec_read(iovs, N, false));
		EXPECT_EQ(0, memcmp(data1, data2, N * S));

		std::shuffle(offsets.begin(), offsets.end(), rng);
	}

	free(data1);
	free(data2);
}

TYPED_TEST_P(TcTest, ParallelRdWrAFile)
{
	const char *PATH = "TcTest-ParallelRdWrAFile.dat";
	const int T = 6;  /* # of threads */
	const int S = 4096;
	tc_touch(PATH, T * S);

	struct viovec iovs[T] = {0};
	char *data1 = getRandomBytes(T * S);
	char *data2 = (char *)malloc(T * S);
	for (int i = 0; i < 1; ++i) { // repeat for 10 times
		for (int t = 0; t < T; ++t) {
			viov2path(&iovs[t], PATH, t * S, S, data1 + t * S);
		}
		DoParallel(T, [&iovs](int i) {
			EXPECT_OK(vec_write(&iovs[i], 1, false));
		});

		memset(iovs, 0, sizeof(iovs));
		for (int t = 0; t < T; ++t) {
			viov2path(&iovs[t], PATH, t * S, S, data2 + t * S);
		}
		DoParallel(T, [&iovs](int i) {
			EXPECT_OK(vec_read(&iovs[i], 1, false));
		});
		EXPECT_EQ(0, memcmp(data1, data2, T * S));
	}

	free(data1);
	free(data2);
}

TYPED_TEST_P(TcTest, RdWrLargeThanRPCLimit)
{
	struct viovec iov = {0};
	char* data1 = getRandomBytes(2_MB);
	viov4creation(&iov, "TcTest-WriteLargeThanRPCLimit.dat", 2_MB, data1);

	EXPECT_OK(vec_write(&iov, 1, false));
	EXPECT_EQ(2_MB, iov.length);

	char* data2 = (char *)malloc(2_MB);
	iov.is_creation = false;
	iov.data = data2;
	for (size_t s = 8_KB; s <= 2_MB; s += 8_KB) {
		iov.length = s;
		EXPECT_OK(vec_read(&iov, 1, false));
		EXPECT_EQ(iov.length == 2_MB, iov.is_eof);
		EXPECT_EQ(s, iov.length);
		EXPECT_EQ(0, memcmp(data1, data2, s));
		if (s % 128_KB == 0)
			fprintf(stderr, "read size: %zu\n", s);
	}

	free(data1);
	free(data2);
}

TYPED_TEST_P(TcTest, CompressDeepPaths)
{
	const char *PATHS[] = { "TcTest-CompressDeepPaths/a/b/c0/001.dat",
				"TcTest-CompressDeepPaths/a/b/c0/002.dat",
				"TcTest-CompressDeepPaths/a/b/c1/001.dat",
				"TcTest-CompressDeepPaths/a/b/c1/002.dat",
				"TcTest-CompressDeepPaths/a/b/c1/002.dat",
				"TcTest-CompressDeepPaths/a/b/c1/002.dat", };
	const int N = sizeof(PATHS)/sizeof(PATHS[0]);

	sca_ensure_dir("TcTest-CompressDeepPaths/a/b/c0", 0755, NULL);
	sca_ensure_dir("TcTest-CompressDeepPaths/a/b/c1", 0755, NULL);

	vec_unlink(PATHS, N);
	struct viovec *iovs = (struct viovec *)calloc(N, sizeof(*iovs));
	for (int i = 0; i < N; ++i) {
		if (i == 0 || strcmp(PATHS[i], PATHS[i-1])) {
			viov4creation(&iovs[i], PATHS[i], 4_KB,
					new char[4_KB]);
		} else {
			viov2path(&iovs[i], PATHS[i], 0, 4_KB,
				    new char[4_KB]);
		}
	}

	EXPECT_OK(vec_write(iovs, N, false));
	for (int i = 0; i < N; ++i) {
		EXPECT_STREQ(iovs[i].file.path, PATHS[i]);
		delete[] iovs[i].data;
	}

	vattrs *attrs = new vattrs[N];
	for (int i = 0; i < N; ++i) {
		attrs[i].file = iovs[i].file;
		attrs[i].masks = VATTRS_MASK_ALL;
	}
	EXPECT_OK(vec_getattrs(attrs, N, false));

	free(iovs);
	delete[] attrs;
}

// Checked unnecessary SAVEFH and RESTOREFH are not used thanks to
// optimization.
TYPED_TEST_P(TcTest, CompressPathForRemove)
{
	sca_ensure_dir("TcTest-CompressPathForRemove/a/b/c/d1", 0755, NULL);
	sca_ensure_dir("TcTest-CompressPathForRemove/a/b/c/d2", 0755, NULL);
	const int FILES_PER_DIR = 8;
	vfile *files = (vfile *)alloca(FILES_PER_DIR * 2 * sizeof(vfile));
	for (int i = 0; i < FILES_PER_DIR; ++i) {
		char *p1 = new_auto_path(
		    "TcTest-CompressPathForRemove/a/b/c/d1/%d", i);
		char *p2 = new_auto_path(
		    "TcTest-CompressPathForRemove/a/b/c/d2/%d", i);
		const char *paths[2] = {p1, p2};
		tc_touchv(paths, 2, 4_KB);
		files[i] = vfile_from_path(p1);
		files[i + FILES_PER_DIR] = vfile_from_path(p2);
	}
	EXPECT_OK(vec_remove(files, FILES_PER_DIR * 2, false));
}

TYPED_TEST_P(TcTest, SymlinkBasics)
{
	const char *TARGETS[] = { "TcTest-SymlinkBasics/001.file",
				  "TcTest-SymlinkBasics/002.file",
				  "TcTest-SymlinkBasics/003.file",
				  "TcTest-SymlinkBasics/004.file",
				  "TcTest-SymlinkBasics/005.file", };
	const char *LINKS[] = { "TcTest-SymlinkBasics/001.link",
				"TcTest-SymlinkBasics/002.link",
				"TcTest-SymlinkBasics/003.link",
				"TcTest-SymlinkBasics/004.link",
				"TcTest-SymlinkBasics/005.link", };
	const char *CONTENTS[] = { "001.file", "002.file", "003.file",
				   "004.file", "005.file", };
	const int N = sizeof(TARGETS) / sizeof(TARGETS[0]);
	char **bufs = new char*[N];
	size_t *bufsizes = new size_t[N];

	EXPECT_OK(sca_ensure_dir("TcTest-SymlinkBasics", 0755, NULL));
	Removev(TARGETS, N);
	Removev(LINKS, N);

	for (int i = 0; i < N; ++i) {
		tc_touch(TARGETS[i], 4_KB);
		bufs[i] = new char[PATH_MAX];
		bufsizes[i] = PATH_MAX;
	}

	EXPECT_OK(vec_symlink(CONTENTS, LINKS, N, false));

	EXPECT_OK(vec_readlink(LINKS, bufs, bufsizes, N, false));

	for (int i = 0; i < N; ++i) {
		EXPECT_EQ(strlen(CONTENTS[i]), bufsizes[i]);
		EXPECT_EQ(0, strncmp(CONTENTS[i], bufs[i], bufsizes[i]));
		delete[] bufs[i];
	}
	delete[] bufs;
	delete[] bufsizes;
}

TYPED_TEST_P(TcTest, ManyLinksDontFitInOneCompound)
{
	const int NLINKS = 64;
	const char *targets[NLINKS];
	const char *links[NLINKS];
	char *bufs[NLINKS];
	size_t bufsizes[NLINKS];
	const char *dirname = "ManyLinks";

	EXPECT_OK(vec_unlink_recursive(&dirname, 1));
	for (int i = 0; i < NLINKS; ++i) {
		targets[i] = new_auto_path("ManyLinks/file%d", i);
		links[i] = new_auto_path("ManyLinks/a%d/b/c/d/e/f/h/link", i);
		vensure_parent_dir(links[i]);
		bufs[i] = (char *)alloca(PATH_MAX);
		bufsizes[i] = PATH_MAX;
	}
	tc_touchv(targets, NLINKS, 1_KB);
	EXPECT_OK(vec_symlink(targets, links, NLINKS, false));
	EXPECT_OK(vec_readlink(links, bufs, bufsizes, NLINKS, false));
	for (int i = 0; i < NLINKS; ++i) {
		EXPECT_STREQ(targets[i], bufs[i]);
	}
}

TYPED_TEST_P(TcTest, WriteManyDontFitInOneCompound)
{
	const int NFILES = 64; // 64 * 8 == 512
	struct viovec iovs[NFILES];
	const char *ROOTDIR = "WriteMany";

	EXPECT_OK(vec_unlink_recursive(&ROOTDIR, 1));
	for (int i = 0; i < NFILES; ++i) {
		char *p =
		    new_auto_path("WriteMany/a%03d/b/c/d/e/f/g/h/file", i);
		vensure_parent_dir(p);
		viov4creation(&iovs[i], p, strlen(p), p);
	}
	EXPECT_OK(vec_write(iovs, NFILES, false));
}

static bool listdir_test_cb(const struct vattrs *entry, const char *dir,
			    void *cbarg)
{
	std::set<std::string> *objs = (std::set<std::string> *)cbarg;
	objs->emplace(entry->file.path);
	return true;
}

TYPED_TEST_P(TcTest, RequestDoesNotFitIntoOneCompound)
{
	const int NFILES = 64; // 64 * 8 == 512
	const char *paths[NFILES];
	int flags[NFILES];
	struct vattrs attrs[NFILES];
	const char *new_paths[NFILES];
	struct vfile_pair pairs[NFILES];
	const char *ROOTDIR = "DontFit";

	EXPECT_OK(vec_unlink_recursive(&ROOTDIR, 1));
	for (int i = 0; i < NFILES; ++i) {
		paths[i] = new_auto_path("DontFit/a%03d/b/c/d/e/f/g/h/file", i);
		vensure_parent_dir(paths[i]);
		flags[i] = O_WRONLY | O_CREAT;
		attrs[i].file = vfile_from_path(paths[i]);
		new_paths[i] = new_auto_path("DontFit/file-%d", i);
		pairs[i].src_file = vfile_from_path(paths[i]);
		pairs[i].dst_file = vfile_from_path(new_paths[i]);
	}
	vfile *files = vec_open(paths, NFILES, flags, NULL);
	EXPECT_NOTNULL(files);
	EXPECT_OK(vec_close(files, NFILES));
	EXPECT_OK(vec_getattrs(attrs, NFILES, false));

	struct vattrs_masks listdir_mask = { .has_mode = true };
	std::set<std::string> objs;
	EXPECT_OK(vec_listdir(&ROOTDIR, 1, listdir_mask, 0, true,
			      listdir_test_cb, &objs, false));
	std::set<std::string> expected;
	for (int i = 0; i < NFILES; ++i) {
		std::string p(paths[i]);
		size_t n = p.length();
		while (n != std::string::npos) {
			expected.emplace(p.data(), n);
			n = p.find_last_of('/', n - 1);
		}
	}
	expected.erase("DontFit");
	EXPECT_THAT(objs, testing::ContainerEq(expected));

	EXPECT_OK(vec_rename(pairs, NFILES, false));
	EXPECT_OK(vec_unlink(new_paths, NFILES));
}

static bool is_same_stat(const struct stat *st1, const struct stat *st2)
{
	return st1->st_ino == st2->st_ino
	    && st1->st_mode == st2->st_mode
	    && st1->st_nlink == st2->st_nlink
	    && st1->st_uid == st2->st_uid
	    && st1->st_gid == st2->st_gid
	    && st1->st_rdev == st2->st_rdev
	    && st1->st_size == st2->st_size
	    && st1->st_mtime == st2->st_mtime
	    && st1->st_ctime == st2->st_ctime;
	    //&& st1->st_dev == st2->st_dev
	    //&& st1->st_blksize == st2->st_blksize
	    //&& st1->st_blocks == st2->st_blocks
}

TYPED_TEST_P(TcTest, TcStatBasics)
{
	const char *FPATH = "TcTest-TcStatFile.txt";
	const char *LPATH = "TcTest-TcStatLink.txt";

	sca_unlink(FPATH);
	sca_unlink(LPATH);
	tc_touch(FPATH, 4_KB);
	EXPECT_EQ(0, sca_symlink(FPATH, LPATH));

	struct stat st1;
	EXPECT_EQ(0, sca_stat(LPATH, &st1));

	struct stat st2;
	vfile *tcf = sca_open(FPATH, O_RDONLY, 0);
	EXPECT_EQ(0, sca_fstat(tcf, &st2));
	EXPECT_TRUE(is_same_stat(&st1, &st2));
	sca_close(tcf);

	struct stat st3;
	EXPECT_EQ(0, sca_lstat(LPATH, &st3));
	EXPECT_TRUE(S_ISLNK(st3.st_mode));
	EXPECT_FALSE(is_same_stat(&st1, &st3));
}

TYPED_TEST_P(TcTest, TcRmBasic)
{
#define TCRM_PREFIX "/vfs0/tc_nfs4_test/TcRmBasic"
	EXPECT_OK(sca_ensure_dir(TCRM_PREFIX "/dir-a/subdir-a1", 0755, NULL));
	EXPECT_OK(sca_ensure_dir(TCRM_PREFIX "/dir-a/subdir-a2", 0755, NULL));
	EXPECT_OK(sca_ensure_dir(TCRM_PREFIX "/dir-b/subdir-b1", 0755, NULL));

	tc_touch(TCRM_PREFIX "/dir-a/subdir-a1/a1-file1", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-a/subdir-a1/a1-file2", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-a/subdir-a1/a1-file3", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-a/subdir-a2/a2-file1", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-a/subdir-a2/a2-file2", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-b/subdir-b1/b1-file1", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-b/subdir-b1/b1-file2", 4_KB);
	tc_touch(TCRM_PREFIX "/dir-b/subdir-b1/b1-file3", 4_KB);
	tc_touch(TCRM_PREFIX "/file1", 4_KB);
	tc_touch(TCRM_PREFIX "/file2", 4_KB);

	const char *objs[4] = {
		TCRM_PREFIX "/dir-a",
		TCRM_PREFIX "/dir-b",
		TCRM_PREFIX "/file1",
		TCRM_PREFIX "/file2",
	};

	EXPECT_OK(vec_unlink_recursive(objs, 4));
#undef TCRM_PREFIX
}

/**
 * Test listing and removing a big directory.
 *
 * Wrap a big directory "RmMany/bb" with two small directories (i.e.,
 * "RmMany/aa" and "RmMany/cc") and make sure big directory are handled
 * correctly.
 */
TYPED_TEST_P(TcTest, TcRmManyFiles)
{
	const char *dirname = "RmMany";

	EXPECT_OK(sca_ensure_dir("RmMany", 0755, NULL));
	EXPECT_OK(sca_ensure_dir("RmMany/aa", 0755, NULL));
	EXPECT_OK(sca_ensure_dir("RmMany/bb", 0755, NULL));
	tc_touch("RmMany/aa/foo", 1_KB);
	const int N_PER_CPD = 64;
	char *scratch = (char *)malloc(PATH_MAX * N_PER_CPD);
	for (int i = 0; i < 32; ++i) {
		const char *FILES[N_PER_CPD];
		for (int j = 0; j < N_PER_CPD; ++j) {
			char *p = scratch + j * PATH_MAX;
			snprintf(p, PATH_MAX, "RmMany/bb/file-%d-%d", i, j);
			FILES[j] = p;
		}
		tc_touchv(FILES, N_PER_CPD, 64);
	}
	free(scratch);
	EXPECT_OK(sca_ensure_dir("RmMany/cc", 0755, NULL));
	tc_touch("RmMany/cc/bar", 1_KB);
	EXPECT_OK(vec_unlink_recursive(&dirname, 1));
}

TYPED_TEST_P(TcTest, TcRmRecursive)
{
	const char *dirname = "NonExistDir";
	EXPECT_FALSE(sca_exists("NonExistDir"));
	EXPECT_OK(vec_unlink_recursive(&dirname, 1));
}

TYPED_TEST_P(TcTest, UnalignedCacheRead)
{
        const char *PATHS[] = { "WritevCanCreateFiles1.txt",
                                "WritevCanCreateFiles2.txt",
                                "WritevCanCreateFiles3.txt",
                                "WritevCanCreateFiles4.txt" };
        const int count = sizeof(PATHS)/sizeof(PATHS[0]);

        Removev(PATHS, count);

        viovec *writev = (viovec *)calloc(count, sizeof(viovec));
        for (int i = 0; i < count; ++i) {
                viov4creation(&writev[i], PATHS[i], 4096,
                                getRandomBytes(4096));
        }

        EXPECT_OK(vec_write(writev, count, false));

        viovec *readv = (viovec *)calloc(count, sizeof(viovec));
        for (int i = 0; i < count; ++i) {
                viov2path(&readv[i], PATHS[i], 50, 2000,
                            (char *)malloc(2000));
        }

        EXPECT_OK(vec_read(readv, count, false));
	for (int i = 0; i < count; ++i) {
		EXPECT_EQ(0, memcmp(writev[i].data+50, readv[i].data, 2000));
	}

        free_iovec(writev, count);
        free_iovec(readv, count);
}

TYPED_TEST_P(TcTest, UnalignedCacheWrite)
{
        const char *PATHS[] = { "WritevCanCreateFiles1.txt",
                                "WritevCanCreateFiles2.txt",
                                "WritevCanCreateFiles3.txt",
                                "WritevCanCreateFiles4.txt" };
        const int count = sizeof(PATHS)/sizeof(PATHS[0]);

        Removev(PATHS, count);

        viovec *writev = (viovec *)calloc(count, sizeof(viovec));
        for (int i = 0; i < count; ++i) {
                viov4creation(&writev[i], PATHS[i], 4096,
                                getRandomBytes(4096));
        }

        EXPECT_OK(vec_write(writev, count, false));

	viovec *writev2 = (viovec *)calloc(count, sizeof(viovec));
	for (int i = 0; i < count; ++i) {
		viov2path(&writev2[i], PATHS[i], 50, 2000,
			getRandomBytes(2000));
	}

	EXPECT_OK(vec_write(writev2, count, false));

        viovec *readv = (viovec *)calloc(count, sizeof(viovec));
        for (int i = 0; i < count; ++i) {
                viov2path(&readv[i], PATHS[i], 50, 2000,
                            (char *)malloc(2000));
        }

        EXPECT_OK(vec_read(readv, count, false));
        for (int i = 0; i < count; ++i) {
		EXPECT_TRUE(compare_content(writev2, readv, count));
        }

        free_iovec(writev, count);
        free_iovec(writev2, count);
        free_iovec(readv, count);
}

// TODO; add test interaction between data cache and writes to TC_OFFSET_CUR
// See tc_cache.cpp:nfs_writev.


REGISTER_TYPED_TEST_CASE_P(TcTest,
			   WritevCanCreateFiles,
			   TestFileDesc,
			   AttrsTestPath,
			   AttrsTestFileDesc,
			   AttrsTestSymlinks,
			   SetAttrsOfManyFiles,
			   ListDirContents,
			   ListLargeDir,
			   ListDirRecursively,
			   RenameFile,
			   RemoveFileTest,
			   MakeDirectories,
			   MakeManyDirsDontFitInOneCompound,
			   Append,
			   SuccessiveReads,
			   SuccessiveWrites,
			   SessionTimeout,
			   CopyFiles,
			   DupFiles,
			   CopyFirstHalfAsSecondHalf,
			   CopyManyFilesDontFitInOneCompound,
			   WriteManyDontFitInOneCompound,
			   ListAnEmptyDirectory,
			   List2ndLevelDir,
			   ShuffledRdWr,
			   ParallelRdWrAFile,
			   RdWrLargeThanRPCLimit,
			   CompressDeepPaths,
			   CompressPathForRemove,
			   TestHardLinks,
			   SymlinkBasics,
			   ManyLinksDontFitInOneCompound,
			   TcStatBasics,
			   CopyLargeDirectory,
			   RecursiveCopyDirWithSymlinks,
			   TcRmBasic,
			   TcRmManyFiles,
			   TcRmRecursive,
			   RequestDoesNotFitIntoOneCompound,
			   UnalignedCacheRead,
			   UnalignedCacheWrite);

typedef ::testing::Types<TcNFS4Impl, TcPosixImpl> TcImpls;
INSTANTIATE_TYPED_TEST_CASE_P(TC, TcTest, TcImpls);
