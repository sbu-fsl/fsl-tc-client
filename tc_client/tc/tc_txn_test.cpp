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
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>

#include <algorithm>
#include <cstdio>
#include <experimental/random>
#include <list>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "tc_test.hpp"

namespace stdexp = std::experimental;

TYPED_TEST_CASE_P(TcTxnTest);

static bool vec_mkdir_simple(const char **paths, int n, int mode)
{
  struct vattrs *attrs = (struct vattrs*)alloca(n * sizeof(*attrs));
  if (!attrs)
    return false;

  for (int i = 0; i < n; ++i) {
    vset_up_creation(&attrs[i], paths[i], mode);
  }

  return tx_vec_mkdir(attrs, n, true);
}

static bool posix_exists(std::string base, std::string path)
{
  std::string full_path(base + path);
  FILE *fp = fopen(full_path.c_str(), "rb");
  if (fp != nullptr) {
    fclose(fp);
    return true;
  }

  return false;
}

static bool posix_integrity(std::string base, std::string path, void *data,
                            size_t len)
{
  std::string full_path(base + path);
  FILE *fp = fopen(full_path.c_str(), "rb");
  char *buffer = nullptr;
  size_t fsize = 0;
  bool result;
  if (fp == nullptr) {
    return false;
  }

  /* check size first */
  fseek(fp, 0, SEEK_END);
  fsize = ftell(fp);
  if (fsize != len) {
    result = false;
    goto end;
  }
  rewind(fp);

  /* then check content */
  buffer = (char *)malloc(len);
  assert(buffer);

  fread(buffer, len, 1, fp);
  result = (memcmp(buffer, data, len) == 0);

end:
  fclose(fp);
  return result;
}

/**
 * Issue a compound of several mkdir commands, where there is an invalid one
 * in the middle (causing EEXIST).
 *
 * Expected: Compound fails, and no new directories will be created
 */
TYPED_TEST_P(TcTxnTest, BadMkdir)
{
  const int N = 4;
  const char *paths[] = { "bad-mkdir-a",
                          "bad-mkdir-b",
                          "bad-mkdir-c",
                          "bad-mkdir-d" };

  /* preparation: create a directory */
  ASSERT_TRUE(vec_mkdir_simple(&paths[2], 1, 0777));

  /* execute compound */
  EXPECT_FALSE(vec_mkdir_simple(paths, N, 0777));

  vec_unlink(&paths[2], 1);

  /* check existence of PATHS[0] and PATHS[1] */
  for (int i = 0; i < N; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths[i]));
  }
}

TYPED_TEST_P(TcTxnTest, BadMkdir2)
{
  const int N1 = 2, N2 = 6;
  const char *paths1[] = { "bad-mkdir",
                           "bad-mkdir/c" };
  const char *paths2[] = { "bad-mkdir/a",
                           "bad-mkdir/a/b",
                           "bad-mkdir/a/c",
                           "bad-mkdir/b",
                           "bad-mkdir/c",
                           "bad-mkdir/d" };

  ASSERT_TRUE(vec_mkdir_simple(paths1, N1, 0777));

  EXPECT_FALSE(vec_mkdir_simple(paths2, N2, 0777));

  EXPECT_TRUE(sca_exists(paths1[0]));
  EXPECT_TRUE(sca_exists(paths1[1]));

  vec_unlink(&paths1[1], 1);
  vec_unlink(&paths1[0], 1);

  for (int i = 0; i < N2; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths2[i]));
  }
}

/* Invalid OPEN compound */
TYPED_TEST_P(TcTxnTest, BadFileCreation)
{
  const int n = 6;
  const char *basedir = "bad-creation";
  const char *dir1 = "bad-creation/good";
  const char *dir2 = "bad-creation/bad";
  const char *paths[] = { "bad-creation/good/a",
                          "bad-creation/good/b",
                          "bad-creation/good/c",
                          "bad-creation/good/d",
                          "bad-creation/bad/",
                          "bad-creation/good/f" };
  vfile *files;

  /* create dirs */
  ASSERT_TRUE(vec_mkdir_simple(&basedir, 1, 0777));
  ASSERT_TRUE(vec_mkdir_simple(&dir1, 1, 0777));
  ASSERT_TRUE(vec_mkdir_simple(&dir2, 1, 0000));

  files = vec_open_simple(paths, n, O_CREAT, 0666);
  EXPECT_EQ(files, nullptr);

  /* expect the files NOT to exist */
  for (int i = 0; i < 4; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths[i])) << paths[i];
  }
  EXPECT_FALSE(posix_exists(this->posix_base, paths[5]));

  EXPECT_OK(vec_unlink(&dir2, 1));
  EXPECT_OK(vec_unlink(&dir1, 1));
  EXPECT_OK(vec_unlink(&basedir, 1));
}

/* BadFileCreation with hierarchy dir structure */
TYPED_TEST_P(TcTxnTest, BadFileCreation2)
{
  const int n = 6;
  const char *dirs[] = { "bad-creation2",
                         "bad-creation2/a",
                         "bad-creation2/a/b",
                         "bad-creation2/a/b/c",
                         "bad-creation2/a/b/c/d",
                         "bad-creation2/a/b/c/d/e" };
  const char *paths[] = { "bad-creation2/1",
                          "bad-creation2/a/2",
                          "bad-creation2/a/b/3",
                          "bad-creation2/a/b/c/4",
                          "bad-creation2/a/b/c/d",
                          "bad-creation2/a/b/c/d/e/5" };
  vfile *files;

  ASSERT_TRUE(vec_mkdir_simple(dirs, n, 0777));

  files = vec_open_simple(paths, n, O_CREAT, 0666);
  EXPECT_EQ(files, nullptr);

  for (int i = 0; i < 4; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths[i])) << paths[i];
  }
  EXPECT_FALSE(posix_exists(this->posix_base, paths[5]));

  for (int i = n - 1; i >= 0; --i) {
    EXPECT_OK(vec_unlink(&dirs[i], 1));
  }
}

/* Invalid OPEN compound with CREATE flag, but some files already exist. */
TYPED_TEST_P(TcTxnTest, BadCreationWithExisting)
{
  const int n1 = 2, n2 = 6;
  const char *dir1 = "bad-creation3";
  const char *dir2 = "bad-creation3/no";
  const char *paths1[] = { "bad-creation3/c",
                           "bad-creation3/d" };
  const char *paths2[] = { "bad-creation3/a",
                           "bad-creation3/b",
                           "bad-creation3/c",
                           "bad-creation3/d",
                           "bad-creation3/no",
                           "bad-creation3/f" };
  vfile *files1, *files2;

  ASSERT_TRUE(vec_mkdir_simple(&dir1, 1, 0777));
  ASSERT_TRUE(vec_mkdir_simple(&dir2, 1, 0777));

  /* create files in paths1[] */
  files1 = vec_open_simple(paths1, n1, O_CREAT, 0666);
  ASSERT_NE(files1, nullptr);
  vec_close(files1, n1);

  /* create files in paths2[] - this will fail and rollback, but we expect that
   * files in paths1 won't be removed */
  files2 = vec_open_simple(paths2, n2, O_CREAT, 0666);
  EXPECT_EQ(files2, nullptr);

  /* check that files in paths1[] are not removed during rollback */
  for (int i = 0; i < n1; ++i) {
    EXPECT_TRUE(posix_exists(this->posix_base, paths1[i])) << paths1[i];
  }
  
  /* remove files in paths1[] as well as dir2 */
  EXPECT_OK(vec_unlink(paths1, n1));
  EXPECT_OK(vec_unlink(&dir2, 1));

  for (int i = 0; i < n2; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths2[i])) << paths2[i];
  }

  EXPECT_OK(vec_unlink(&dir1, 1));
}

/* Invalid REMOVE operation */
TYPED_TEST_P(TcTxnTest, BadRemove)
{
  const int n = 5;
  const char *dir = "bad-remove";
  const char *not_exist = "bad-remove/no";
  const char *paths[] = { "bad-remove/a",
                          "bad-remove/b",
                          "bad-remove/c",
                          "bad-remove/no",
                          "bad-remove/d" };
  vfile *files;

  ASSERT_TRUE(vec_mkdir_simple(&dir, 1, 0777));

  /* create files in paths[] */
  files = vec_open_simple(paths, n, O_CREAT, 0666);
  ASSERT_NE(files, nullptr);
  vec_close(files, n);

  /* remove `bad-remove/no` */
  EXPECT_OK(vec_unlink(&not_exist, 1));

  /* remove files in paths[]
   * Since `bad-remove/no` no longer exists, the compound will fail and
   * trigger rollback */
  EXPECT_FAIL(vec_unlink(paths, n));

  /* a, b, c, d should exist */
  EXPECT_TRUE(posix_exists(this->posix_base, paths[0]));
  EXPECT_TRUE(posix_exists(this->posix_base, paths[1]));
  EXPECT_TRUE(posix_exists(this->posix_base, paths[2]));
  EXPECT_TRUE(posix_exists(this->posix_base, paths[4]));

  /* cleanup */
  EXPECT_OK(vec_unlink(paths, 3));
  EXPECT_OK(vec_unlink(&paths[4], 1));
  EXPECT_OK(vec_unlink(&dir, 1));
}

/* Invalid REMOVE operation - with content check */
TYPED_TEST_P(TcTxnTest, BadRemoveCheckContent)
{
  const int n = 5;
  const size_t datasize = 13457;
  const char *dir = "bad-remove2";
  const char *not_exist = "bad-remove2/no";
  const char *paths[] = { "bad-remove2/a",
                          "bad-remove2/b",
                          "bad-remove2/c",
                          "bad-remove2/no",
                          "bad-remove2/e" };
  struct viovec *iov = nullptr;
  vfile *files;

  /* create base dir */
  ASSERT_TRUE(vec_mkdir_simple(&dir, 1, 0777));

  /* create files in paths[] */
  files = vec_open_simple(paths, n, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);

  /* write to these files */
  iov = (struct viovec *)calloc(n, sizeof(*iov));
  ASSERT_NE(iov, nullptr);
  for (int i = 0; i < n; ++i) {
    viov2file(&iov[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(iov, n, true));
  vec_close(files, n);

  /* remove `bad-remove2/no` */
  EXPECT_OK(vec_unlink(&not_exist, 1));

  /* remove files in paths[]
   * Since `bad-remove/no` no longer exists, the compound should fail and
   * rollback, and other files will be restored. */
  EXPECT_FAIL(vec_unlink(paths, n));

  /* a, b, c, d should exist and their content should be intact */
  EXPECT_TRUE(posix_integrity(this->posix_base, paths[0], iov[0].data,
                              datasize));
  EXPECT_TRUE(posix_integrity(this->posix_base, paths[1], iov[1].data,
                              datasize));
  EXPECT_TRUE(posix_integrity(this->posix_base, paths[2], iov[2].data,
                              datasize));
  EXPECT_TRUE(posix_integrity(this->posix_base, paths[4], iov[4].data,
                              datasize));

  /* cleanup */
  EXPECT_OK(vec_unlink(paths, 3));
  EXPECT_OK(vec_unlink(&paths[4], 1));
  EXPECT_OK(vec_unlink(&dir, 1));
  for (int i = 0; i < n; ++i) {
    free(iov[i].data);
  }
  free(iov);
}

/* Invalid LINK operation */
TYPED_TEST_P(TcTxnTest, BadLink)
{
  const int n = 6;
  const char *dir = "bad-link";
  const char *src[] = { "bad-link/a",
                        "bad-link/b",
                        "bad-link/c",
                        "bad-link/d",
                        "bad-link/e",
                        "bad-link/f" };
  const char *dest[] = { "bad-link/1",
                         "bad-link/2",
                         "bad-link/3",
                         "bad-link/d",
                         "bad-link/5",
                         "bad-link/6" };
  vfile *files;

  /* create base dir */
  ASSERT_TRUE(vec_mkdir_simple(&dir, 1, 0777));
  /* create files in src[] */
  files = vec_open_simple(src, n, O_CREAT, 0666);
  ASSERT_NE(files, nullptr);
  vec_close(files, n);

  /* LINK src[] to dest[]
   * This compound should fail because `bad-link/d` already exists. */
  EXPECT_FAIL(vec_hardlink(src, dest, n, true));

  /* remove files in src[] */
  EXPECT_OK(vec_unlink(src, n));

  /* now none of the file in dest[] should exist */
  for (int i = 0; i < n; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, dest[i])) << dest[i];
  }

  /* cleanup */
  EXPECT_OK(vec_unlink(&dir, 1));
}

/* Invalid symlink operation - This actually tests undo_create */
TYPED_TEST_P(TcTxnTest, BadSymLink)
{
  const int n = 7;
  const char *src[] = { "bad-symlink-a",
                        "bad-symlink-a/1",
                        "bad-symlink-a/2",
                        "bad-symlink-a/3",
                        "bad-symlink-a/4",
                        "bad-symlink-a/5",
                        "bad-symlink-a/6" };
  const char *dst[] = { "bad-symlink-b",
                        "bad-symlink-b/a",
                        "bad-symlink-b/a/b",
                        "bad-symlink-b/a/b/c",
                        "bad-symlink-b/a/b/c/d",
                        "bad-symlink-a/5",
                        "bad-symlink-b/f" };
  /* create sources */
  ASSERT_TRUE(vec_mkdir_simple(src, n, 0777));

  /* Symlink src[] to dst[] 
   * This should fail because `bad-symlink-a/5` exists */
  EXPECT_FAIL(vec_symlink(src, dst, n, true));
  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, dst[i]));
  }

  /* clean up */
  EXPECT_OK(vec_unlink_recursive(&src[0], 1));
}

/* Invalid WRITE operation */
TYPED_TEST_P(TcTxnTest, BadWrite)
{
  const int n = 6;
  const size_t datasize = 12345;
  const char *dir = "bad-write";
  const char *paths[] = { "bad-write/a",
                          "bad-write/b",
                          "bad-write/c",
                          "bad-write/d",
                          "bad-write/e",
                          "bad-write/f" };
  int flags[n] = {0};
  mode_t modes[n] = {0};
  vfile *files;
  struct viovec *v1, *v2;

  /* create base dir */
  ASSERT_TRUE(vec_mkdir_simple(&dir, 1, 0777));
  /* create files in paths[] and write some data */
  files = vec_open_simple(paths, n, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  v1 = (struct viovec *)calloc(n, sizeof(*v1));
  v2 = (struct viovec *)calloc(n, sizeof(*v2));
  ASSERT_TRUE(v1 && v2);
  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));
  /* now do a invalid vwrite operation
   * One of the file is opened in RDONLY mode, so the whold compound
   * will fail and rollback. Thus the content of other files should
   * remain that of v1[i].data */
  for (int i = 0; i < n; ++i) {
    flags[i] = O_RDWR;
    modes[i] = 0666;
  }
  flags[3] = O_RDONLY;
  files = vec_open(paths, n, flags, modes);
  ASSERT_NE(files, nullptr);
  for (int i = 0; i < n; ++i) {
    viov2file(&v2[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_FAIL(vec_write(v2, n, true));
  EXPECT_OK(vec_close(files, n));
  /* check state */
  for (int i = 0; i < n; ++i) {
    EXPECT_TRUE(posix_integrity(this->posix_base, paths[i], v1[i].data,
                datasize)) << paths[i];
  }
  /* clean up */
  EXPECT_OK(vec_unlink(paths, n));
  EXPECT_OK(vec_unlink(&dir, 1));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
    free(v2[i].data);
  }
  free(v1);
  free(v2);
}

/* Invalid WRITE operation, but write part of the file in the middle */
TYPED_TEST_P(TcTxnTest, BadWriteMiddle)
{
  const int n = 6;
  const size_t datasize = 34567;
  const char *dir = "bad-write2";
  const char *paths[] = { "bad-write2/a",
                          "bad-write2/b",
                          "bad-write2/c",
                          "bad-write2/d",
                          "bad-write2/e",
                          "bad-write2/f" };
  int flags[n] = {O_RDWR};
  mode_t modes[n] = {0666};
  vfile *files;
  struct viovec *v1, *v2;

  /* create base dir */
  ASSERT_TRUE(vec_mkdir_simple(&dir, 1, 0777));
  /* create files in paths[] and write random data */
  files = vec_open_simple(paths, n, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  v1 = (struct viovec *)calloc(n, sizeof(*v1));
  v2 = (struct viovec *)calloc(n, sizeof(*v2));
  ASSERT_TRUE(v1 && v2);
  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));
  /* Issue a WRITE compound cmd that has a invalid req in the middle */
  flags[4] = O_RDONLY;
  files = vec_open(paths, n, flags, modes);
  ASSERT_NE(files, nullptr);
  /* this time let's write at random offset and random size */
  for (int i = 0; i < n; ++i) {
    size_t offset = stdexp::randint((size_t)1024, datasize);
    size_t len = stdexp::randint((size_t)1, datasize - offset);
    viov2file(&v2[i], &files[i], offset, len, getRandomBytes(len));
  }
  EXPECT_FAIL(vec_write(v2, n, true));
  EXPECT_OK(vec_close(files, n));
  /* check state */
  for (int i = 0; i < n; ++i) {
    EXPECT_TRUE(posix_integrity(this->posix_base, paths[i], v1[i].data,
                datasize)) << paths[i];
  }
  /* clean up */
  EXPECT_OK(vec_unlink(paths, n));
  EXPECT_OK(vec_unlink(&dir, 1));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
    free(v2[i].data);
  }
  free(v1);
  free(v2);
}

/* Invalid WRITE operation, but expands the file */
TYPED_TEST_P(TcTxnTest, BadWriteExpanding)
{
  const int n = 6;
  const size_t datasize = 34567;
  const size_t maxsize = datasize * 2;
  const char *dir = "bad-write3";
  const char *paths[] = { "bad-write3/a",
                          "bad-write3/b",
                          "bad-write3/c",
                          "bad-write3/d",
                          "bad-write3/e",
                          "bad-write3/f" };
  int flags[n] = {O_RDWR};
  mode_t modes[n] = {0666};
  vfile *files;
  struct viovec *v1, *v2;

  /* create base dir */
  ASSERT_TRUE(vec_mkdir_simple(&dir, 1, 0777));
  /* create files in paths[] and write random data */
  files = vec_open_simple(paths, n, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  v1 = (struct viovec *)calloc(n, sizeof(*v1));
  v2 = (struct viovec *)calloc(n, sizeof(*v2));
  ASSERT_TRUE(v1 && v2);
  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));
  /* Issue a WRITE compound cmd that has a invalid req in the middle */
  flags[4] = O_RDONLY;
  files = vec_open(paths, n, flags, modes);
  ASSERT_NE(files, nullptr);
  /* this time let's write at random offset and random size. However
   * these requests will expand the original file */
  for (int i = 0; i < n; ++i) {
    size_t offset = stdexp::randint(datasize / 2, datasize);
    size_t len = stdexp::randint(datasize - offset, maxsize);
    viov2file(&v2[i], &files[i], offset, len, getRandomBytes(len));
  }
  EXPECT_FAIL(vec_write(v2, n, true));
  EXPECT_OK(vec_close(files, n));
  /* check state */
  for (int i = 0; i < n; ++i) {
    EXPECT_TRUE(posix_integrity(this->posix_base, paths[i], v1[i].data,
                datasize)) << paths[i];
  }
  /* clean up */
  EXPECT_OK(vec_unlink(paths, n));
  EXPECT_OK(vec_unlink(&dir, 1));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
    free(v2[i].data);
  }
  free(v1);
  free(v2);
}

TYPED_TEST_P(TcTxnTest, UUIDOpenExclFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "PRE-1-open-excl.txt",
	                        "PRE-2-open-excl.txt",
	                        "PRE-3-open-excl.txt",
	                        "PRE-4-open-excl.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_EXCL | O_CREAT, 0);
	EXPECT_NOTNULL(files);
	vec_close(files, N);
}

TYPED_TEST_P(TcTxnTest, UUIDOpenFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "PRE-1-open.txt",
	                        "PRE-2-open.txt",
	                        "PRE-3-open.txt",
	                        "PRE-4-open.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_CREAT, 0);
	EXPECT_NOTNULL(files);
	vec_close(files, N);
}

TYPED_TEST_P(TcTxnTest, UUIDReadFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "TcTxnTest-TestFileDesc1.txt",
	                        "TcTxnTest-TestFileDesc2.txt",
	                        "TcTxnTest-TestFileDesc3.txt",
	                        "TcTxnTest-TestFileDesc4.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, 0, 0);
	EXPECT_EQ(files, nullptr);
  if (files)
	  vec_close(files, N);
}

REGISTER_TYPED_TEST_CASE_P(TcTxnTest,
        BadMkdir,
        BadMkdir2,
        BadFileCreation,
        BadFileCreation2,
        BadRemove,
        BadRemoveCheckContent,
        BadCreationWithExisting,
        BadLink,
        BadSymLink,
        BadWrite,
        BadWriteMiddle,
        BadWriteExpanding,
        UUIDOpenExclFlagCheck,
        UUIDOpenFlagCheck,
        UUIDReadFlagCheck);

typedef ::testing::Types<TcNFS4Impl> TcTxnImpls;
INSTANTIATE_TYPED_TEST_CASE_P(TC, TcTxnTest, TcTxnImpls);
