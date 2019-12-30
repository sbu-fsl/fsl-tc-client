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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <experimental/filesystem>
#include <experimental/random>
#include <list>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tc_api_wrapper.hpp"
#include "tc_test.hpp"

namespace stdexp = std::experimental;
namespace fs = stdexp::filesystem;

/* Flags for Serializability tests */
/* number of writer threads */
DEFINE_int32(nwriter_threads, 2, "Number of writer threads");

/* number of reader threads */
DEFINE_int32(nreader_threads, 5, "Number of reader threads");

/* number of operations in writer thread */
DEFINE_int32(nwriter_iterations, 50, "Number of iterations in writer thread");

TYPED_TEST_CASE_P(TcTxnTest);

/* @brief Check if a file exists in local file system
 *
 * @param[in] base    The base directory
 * @param[in] path    The path relative to base
 *
 * @return true or false
 */
static bool posix_exists(std::string base, std::string path) {
  fs::path full_path(base);
  full_path /= path;

  return fs::exists(full_path);
}

/* @brief Check if a local file has the same length and content
 *        as the given data
 *
 * @param[in] base    The base directory
 * @param[in] path    The path relative to base
 * @param[in] data    The buffer of data to compare
 * @param[in] len     The length of data
 *
 * @returns True if the file size is equal to len and the content is the same
 * as data. Otherwise returns false. If the file does not exist, returns false.
 */
static bool posix_integrity(const std::string base, const std::string path,
                            void *data, const size_t len) {
  fs::path full_path(base);
  full_path /= path;

  FILE *fp = fopen(full_path.c_str(), "rb");
  if (fp == nullptr) {
    return false;
  }

  /* check size first */
  fseek(fp, 0, SEEK_END);
  if (ftell(fp) != len) {
    fclose(fp);
    return false;
  }
  rewind(fp);

  /* then check content */
  char *buffer = (char *)malloc(len);
  assert(buffer);

  fread(buffer, len, 1, fp);
  bool result = (memcmp(buffer, data, len) == 0);
  fclose(fp);
  return result;
}

/**
 * Issue a compound of several mkdir commands, where there is an invalid one
 * in the middle (causing EEXIST).
 *
 * Expected: Compound fails, and no new directories will be created
 */
TYPED_TEST_P(TcTxnTest, BadMkdir) {
  const std::vector<std::string> paths{"bad-mkdir-a", "bad-mkdir-b",
                                       "bad-mkdir-c", "bad-mkdir-d"};

  /* preparation: create a directory */
  ASSERT_TRUE(tc::sca_mkdir(paths[2], 0777));

  /* execute compound */
  EXPECT_FALSE(tc::vec_mkdir_simple(paths, 0777));

  EXPECT_OK(tc::sca_unlink(paths[2]));

  /* check existence of PATHS[0] and PATHS[1] */
  for (int i = 0; i < paths.size(); ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths[i]));
  }
}

TYPED_TEST_P(TcTxnTest, BadMkdir2) {
  const std::vector<std::string> paths1{"bad-mkdir", "bad-mkdir/c"};
  const std::vector<std::string> paths2{"bad-mkdir/a",   "bad-mkdir/a/b",
                                        "bad-mkdir/a/c", "bad-mkdir/b",
                                        "bad-mkdir/c",   "bad-mkdir/d"};

  ASSERT_TRUE(tc::vec_mkdir_simple(paths1, 0777));

  EXPECT_FALSE(tc::vec_mkdir_simple(paths2, 0777));

  EXPECT_TRUE(tc::sca_exists(paths1[0]));
  EXPECT_TRUE(tc::sca_exists(paths1[1]));

  EXPECT_OK(tc::sca_unlink(paths1[1]));
  EXPECT_OK(tc::sca_unlink(paths1[0]));

  for (int i = 0; i < paths2.size(); ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths2[i]));
  }
}

/* Invalid OPEN compound */
TYPED_TEST_P(TcTxnTest, BadFileCreation) {
  const std::string basedir("bad-creation");
  const std::string dir1("bad-creation/good");
  const std::string dir2("bad-creation/bad");
  const std::vector<std::string> paths{
      "bad-creation/good/a", "bad-creation/good/b", "bad-creation/good/c",
      "bad-creation/good/d", "bad-creation/bad/",   "bad-creation/good/f"};

  /* create dirs */
  ASSERT_TRUE(tc::sca_mkdir(basedir, 0777));
  ASSERT_TRUE(tc::sca_mkdir(dir1, 0777));
  ASSERT_TRUE(tc::sca_mkdir(dir2, 0000));

  vfile *files = tc::vec_open_simple(paths, O_CREAT, 0666);
  EXPECT_EQ(files, nullptr);

  /* expect the files NOT to exist */
  for (int i = 0; i < 4; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths[i])) << paths[i];
  }
  EXPECT_TRUE(posix_exists(this->posix_base, paths[4]));
  EXPECT_FALSE(posix_exists(this->posix_base, paths[5]));

  EXPECT_OK(tc::sca_unlink_recursive(basedir));
}

/* BadFileCreation with hierarchy dir structure */
TYPED_TEST_P(TcTxnTest, BadFileCreation2) {
  const std::vector<std::string> dirs{
      "bad-creation2",         "bad-creation2/a",
      "bad-creation2/a/b",     "bad-creation2/a/b/c",
      "bad-creation2/a/b/c/d", "bad-creation2/a/b/c/d/e"};
  const std::vector<std::string> paths{
      "bad-creation2/1",       "bad-creation2/a/2",
      "bad-creation2/a/b/3",   "bad-creation2/a/b/c/4",
      "bad-creation2/a/b/c/d", "bad-creation2/a/b/c/d/e/5"};

  ASSERT_TRUE(tc::vec_mkdir_simple(dirs, 0777));

  vfile *files = tc::vec_open_simple(paths, O_CREAT, 0666);
  EXPECT_EQ(files, nullptr);

  for (int i = 0; i < 4; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths[i])) << paths[i];
  }
  EXPECT_FALSE(posix_exists(this->posix_base, paths[5]));

  EXPECT_OK(tc::sca_unlink_recursive(dirs[0]));
}

/* Invalid OPEN with O_TRUNC */
TYPED_TEST_P(TcTxnTest, BadOpenWithTrunc) {
  const size_t datasize = 16387;
  const std::string dir("bad-opentrunc");
  std::vector<std::string> paths{"bad-opentrunc/a", "bad-opentrunc/b",
                                 "bad-opentrunc/c", "bad-opentrunc/d",
                                 "bad-opentrunc/e", "bad-opentrunc/f"};

  /* create dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  /* create files and write some data */
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  const int n = paths.size();
  struct viovec *v1 = (struct viovec *)calloc(n, sizeof(*v1));
  ASSERT_NE(v1, nullptr);

  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));

  /* issue open command with O_TRUNC
   * One of them is invalid because the name is not a regular file */
  paths[3] = "bad-opentrunc/";
  files = tc::vec_open_simple(paths, O_CREAT | O_TRUNC, 0666);
  EXPECT_EQ(files, nullptr);

  paths[3] = "bad-opentrunc/d";
  /* check state */
  for (int i = 0; i < paths.size(); ++i) {
    EXPECT_TRUE(
        posix_integrity(this->posix_base, paths[i], v1[i].data, datasize))
        << paths[i];
  }

  /* cleanup */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
  }
  free(v1);
}

/* Invalid OPEN compound with CREATE flag, but some files already exist. */
TYPED_TEST_P(TcTxnTest, BadCreationWithExisting) {
  const std::string dir1("bad-creation3");
  const std::string dir2("bad-creation3/no");
  const std::vector<std::string> paths1{"bad-creation3/c", "bad-creation3/d"};
  const std::vector<std::string> paths2{"bad-creation3/a",  "bad-creation3/b",
                                        "bad-creation3/c",  "bad-creation3/d",
                                        "bad-creation3/no", "bad-creation3/f"};

  ASSERT_TRUE(tc::sca_mkdir(dir1, 0777));
  ASSERT_TRUE(tc::sca_mkdir(dir2, 0777));

  /* create files in paths1[] */
  vfile *files1 = tc::vec_open_simple(paths1, O_CREAT, 0666);
  ASSERT_NE(files1, nullptr);
  vec_close(files1, paths1.size());

  /* create files in paths2[] - this will fail and rollback, but we expect that
   * files in paths1 won't be removed */
  vfile *files2 = tc::vec_open_simple(paths2, O_CREAT, 0666);
  EXPECT_EQ(files2, nullptr);

  /* check that files in paths1[] are not removed during rollback */
  for (int i = 0; i < paths1.size(); ++i) {
    EXPECT_TRUE(posix_exists(this->posix_base, paths1[i])) << paths1[i];
  }

  /* remove files in paths1[] as well as dir2 */
  EXPECT_OK(tc::vec_unlink(paths1));
  EXPECT_OK(tc::sca_unlink(dir2));

  for (int i = 0; i < paths2.size(); ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, paths2[i])) << paths2[i];
  }

  EXPECT_OK(tc::sca_unlink_recursive(dir1));
}

/* Invalid REMOVE operation */
TYPED_TEST_P(TcTxnTest, BadRemove) {
  const std::string dir("bad-remove");
  const std::string not_exist("bad-remove/no");
  const std::vector<std::string> paths{"bad-remove/a", "bad-remove/b",
                                       "bad-remove/c", "bad-remove/no",
                                       "bad-remove/d"};

  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));

  /* create files in paths[] */
  vfile *files = tc::vec_open_simple(paths, O_CREAT, 0666);
  ASSERT_NE(files, nullptr);
  vec_close(files, paths.size());

  /* remove `bad-remove/no` */
  EXPECT_OK(tc::sca_unlink(not_exist));

  /* remove files in paths[]
   * Since `bad-remove/no` no longer exists, the compound will fail and
   * trigger rollback */
  EXPECT_FAIL(tc::vec_unlink(paths));

  /* a, b, c, d should exist */
  EXPECT_TRUE(posix_exists(this->posix_base, paths[0]));
  EXPECT_TRUE(posix_exists(this->posix_base, paths[1]));
  EXPECT_TRUE(posix_exists(this->posix_base, paths[2]));
  EXPECT_TRUE(posix_exists(this->posix_base, paths[4]));

  /* cleanup */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
}

/* Invalid REMOVE operation - with content check */
TYPED_TEST_P(TcTxnTest, BadRemoveCheckContent) {
  const size_t datasize = 13457;
  const std::string dir("bad-remove2");
  const std::string not_exist("bad-remove2/no");
  const std::vector<std::string> paths{"bad-remove2/a", "bad-remove2/b",
                                       "bad-remove2/c", "bad-remove2/no",
                                       "bad-remove2/e"};

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));

  /* create files in paths[] */
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);

  /* write to these files */
  const int n = paths.size();
  struct viovec *iov = (struct viovec *)calloc(n, sizeof(*iov));
  ASSERT_NE(iov, nullptr);
  for (int i = 0; i < n; ++i) {
    viov2file(&iov[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(iov, n, true));
  vec_close(files, n);

  /* remove `bad-remove2/no` */
  EXPECT_OK(tc::sca_unlink(not_exist));

  /* remove files in paths[]
   * Since `bad-remove/no` no longer exists, the compound should fail and
   * rollback, and other files will be restored. */
  EXPECT_FAIL(tc::vec_unlink(paths));

  /* a, b, c, d should exist and their content should be intact */
  EXPECT_TRUE(
      posix_integrity(this->posix_base, paths[0], iov[0].data, datasize));
  EXPECT_TRUE(
      posix_integrity(this->posix_base, paths[1], iov[1].data, datasize));
  EXPECT_TRUE(
      posix_integrity(this->posix_base, paths[2], iov[2].data, datasize));
  EXPECT_TRUE(
      posix_integrity(this->posix_base, paths[4], iov[4].data, datasize));

  /* cleanup */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
  for (int i = 0; i < n; ++i) {
    free(iov[i].data);
  }
  free(iov);
}

/* Invalid LINK operation */
TYPED_TEST_P(TcTxnTest, BadLink) {
  const std::string dir("bad-link");
  const std::vector<std::string> src{"bad-link/a", "bad-link/b", "bad-link/c",
                                     "bad-link/d", "bad-link/e", "bad-link/f"};
  const std::vector<std::string> dest{"bad-link/1", "bad-link/2", "bad-link/3",
                                      "bad-link/d", "bad-link/5", "bad-link/6"};

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  /* create files in src[] */
  vfile *files = tc::vec_open_simple(src, O_CREAT, 0666);
  ASSERT_NE(files, nullptr);
  vec_close(files, src.size());

  /* LINK src[] to dest[]
   * This compound should fail because `bad-link/d` already exists. */
  EXPECT_FAIL(tc::vec_hardlink(src, dest, true));

  /* remove files in src[] */
  EXPECT_OK(tc::vec_unlink(src));

  /* now none of the file in dest[] should exist */
  for (int i = 0; i < dest.size(); ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, dest[i])) << dest[i];
  }

  /* cleanup */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
}

/* Invalid symlink operation - This actually tests undo_create */
TYPED_TEST_P(TcTxnTest, BadSymLink) {
  const std::vector<std::string> src{"bad-symlink-a",   "bad-symlink-a/1",
                                     "bad-symlink-a/2", "bad-symlink-a/3",
                                     "bad-symlink-a/4", "bad-symlink-a/5",
                                     "bad-symlink-a/6"};
  const std::vector<std::string> dst{
      "bad-symlink-b",       "bad-symlink-b/a",       "bad-symlink-b/a/b",
      "bad-symlink-b/a/b/c", "bad-symlink-b/a/b/c/d", "bad-symlink-a/5",
      "bad-symlink-b/f"};
  /* create sources */
  ASSERT_TRUE(tc::vec_mkdir_simple(src, 0777));

  /* Symlink src[] to dst[]
   * This should fail because `bad-symlink-a/5` exists */
  EXPECT_FAIL(tc::vec_symlink(src, dst, true));
  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(posix_exists(this->posix_base, dst[i]));
  }
  EXPECT_TRUE(posix_exists(this->posix_base, dst[5]));
  EXPECT_FALSE(posix_exists(this->posix_base, dst[6]));

  /* clean up */
  EXPECT_OK(tc::sca_unlink_recursive(src[0]));
}

/* Invalid WRITE operation */
TYPED_TEST_P(TcTxnTest, BadWrite) {
  const size_t datasize = 12345;
  const std::string dir("bad-write");
  const std::vector<std::string> paths{"bad-write/a", "bad-write/b",
                                       "bad-write/c", "bad-write/d",
                                       "bad-write/e", "bad-write/f"};

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  /* create files in paths[] and write some data */
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  const int n = paths.size();
  struct viovec *v1 = (struct viovec *)calloc(n, sizeof(*v1));
  ASSERT_TRUE(v1);
  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));
  /* now do a invalid vwrite operation
   * One of the file is opened in RDONLY mode, so the whole compound
   * will fail and rollback. Thus the content of other files should
   * remain that of v1[i].data */
  std::vector<int> flags(n);
  std::vector<mode_t> modes(n);
  std::fill_n(flags.begin(), n, O_RDWR);
  std::fill_n(modes.begin(), n, 0666);
  flags[4] = O_RDONLY;
  files = tc::vec_open(paths, flags, modes);
  ASSERT_NE(files, nullptr);
  struct viovec *v2 = (struct viovec *)calloc(n, sizeof(*v2));
  ASSERT_TRUE(v2);
  for (int i = 0; i < n; ++i) {
    viov2file(&v2[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_FAIL(vec_write(v2, n, true));
  EXPECT_OK(vec_close(files, n));
  /* check state */
  for (int i = 0; i < n; ++i) {
    EXPECT_TRUE(
        posix_integrity(this->posix_base, paths[i], v1[i].data, datasize))
        << paths[i];
  }
  /* clean up */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
    free(v2[i].data);
  }
  free(v1);
  free(v2);
}

/* Invalid WRITE operation, but write part of the file in the middle */
TYPED_TEST_P(TcTxnTest, BadWriteMiddle) {
  const size_t datasize = 34567;
  const std::string dir("bad-write2");
  const std::vector<std::string> paths{"bad-write2/a", "bad-write2/b",
                                       "bad-write2/c", "bad-write2/d",
                                       "bad-write2/e", "bad-write2/f"};

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  /* create files in paths[] and write random data */
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  int n = paths.size();
  struct viovec *v1 = (struct viovec *)calloc(n, sizeof(*v1));
  ASSERT_TRUE(v1);
  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));
  /* Issue a WRITE compound cmd that has a invalid req in the middle */
  std::vector<int> flags(n);
  std::vector<mode_t> modes(n);
  std::fill_n(flags.begin(), n, O_RDWR);
  std::fill_n(modes.begin(), n, 0666);
  flags[4] = O_RDONLY;
  files = tc::vec_open(paths, flags, modes);
  ASSERT_NE(files, nullptr);
  /* this time let's write at random offset and random size */
  struct viovec *v2 = (struct viovec *)calloc(n, sizeof(*v2));
  ASSERT_TRUE(v2);
  for (int i = 0; i < n; ++i) {
    size_t offset = stdexp::randint((size_t)1024, datasize);
    size_t len = stdexp::randint((size_t)1, datasize - offset);
    viov2file(&v2[i], &files[i], offset, len, getRandomBytes(len));
  }
  EXPECT_FAIL(vec_write(v2, n, true));
  EXPECT_OK(vec_close(files, n));
  /* check state */
  for (int i = 0; i < n; ++i) {
    EXPECT_TRUE(
        posix_integrity(this->posix_base, paths[i], v1[i].data, datasize))
        << paths[i];
  }
  /* clean up */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
    free(v2[i].data);
  }
  free(v1);
  free(v2);
}

/* Invalid WRITE operation, but expands the file */
TYPED_TEST_P(TcTxnTest, BadWriteExpanding) {
  const size_t datasize = 34567;
  const size_t maxsize = datasize * 2;
  const std::string dir("bad-write3");
  const std::vector<std::string> paths{"bad-write3/a", "bad-write3/b",
                                       "bad-write3/c", "bad-write3/d",
                                       "bad-write3/e", "bad-write3/f"};
  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  /* create files in paths[] and write random data */
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_WRONLY, 0666);
  ASSERT_NE(files, nullptr);
  const int n = paths.size();
  struct viovec *v1 = (struct viovec *)calloc(n, sizeof(*v1));
  ASSERT_TRUE(v1);
  for (int i = 0; i < n; ++i) {
    viov2file(&v1[i], &files[i], 0, datasize, getRandomBytes(datasize));
  }
  EXPECT_OK(vec_write(v1, n, true));
  EXPECT_OK(vec_close(files, n));
  /* Issue a WRITE compound cmd that has a invalid req in the middle */
  std::vector<int> flags(n);
  std::vector<mode_t> modes(n);
  std::fill_n(flags.begin(), n, O_RDWR);
  std::fill_n(modes.begin(), n, 0666);
  flags[4] = O_RDONLY;
  files = tc::vec_open(paths, flags, modes);
  ASSERT_NE(files, nullptr);
  /* this time let's write at random offset and random size. However
   * these requests will expand the original file */
  struct viovec *v2 = (struct viovec *)calloc(n, sizeof(*v2));
  ASSERT_TRUE(v2);
  for (int i = 0; i < n; ++i) {
    size_t offset = stdexp::randint(datasize / 2, datasize);
    size_t len = stdexp::randint(datasize - offset, maxsize);
    viov2file(&v2[i], &files[i], offset, len, getRandomBytes(len));
  }
  EXPECT_FAIL(vec_write(v2, n, true));
  EXPECT_OK(vec_close(files, n));
  /* check state */
  for (int i = 0; i < n; ++i) {
    EXPECT_TRUE(
        posix_integrity(this->posix_base, paths[i], v1[i].data, datasize))
        << paths[i];
  }
  /* clean up */
  EXPECT_OK(tc::sca_unlink_recursive(dir));
  for (int i = 0; i < n; ++i) {
    free(v1[i].data);
    free(v2[i].data);
  }
  free(v1);
  free(v2);
}

TYPED_TEST_P(TcTxnTest, UUIDOpenExclFlagCheck) {
  const std::vector<std::string> paths{
      "PRE-1-open-excl.txt", "PRE-2-open-excl.txt", "PRE-3-open-excl.txt",
      "PRE-4-open-excl.txt"};
  vfile *files;

  tc::vec_unlink(paths);

  files = tc::vec_open_simple(paths, O_EXCL | O_CREAT, 0);
  EXPECT_NOTNULL(files);
  vec_close(files, paths.size());
  tc::vec_unlink(paths);
}

TYPED_TEST_P(TcTxnTest, UUIDOpenFlagCheck) {
  const std::vector<std::string> paths{"PRE-1-open.txt", "PRE-2-open.txt",
                                       "PRE-3-open.txt", "PRE-4-open.txt"};
  vfile *files;

  tc::vec_unlink(paths);

  files = tc::vec_open_simple(paths, O_CREAT, 0);
  EXPECT_NOTNULL(files);
  vec_close(files, paths.size());
  tc::vec_unlink(paths);
}

TYPED_TEST_P(TcTxnTest, UUIDReadFlagCheck) {
  const std::vector<std::string> paths{
      "TcTxnTest-TestFileDesc1.txt", "TcTxnTest-TestFileDesc2.txt",
      "TcTxnTest-TestFileDesc3.txt", "TcTxnTest-TestFileDesc4.txt"};
  vfile *files;

  tc::vec_unlink(paths);

  files = tc::vec_open_simple(paths, 0, 0);
  EXPECT_EQ(files, nullptr);
  if (files) vec_close(files, paths.size());
}

void execute_posix_path_exists(const std::vector<std::string> &paths,
                               const std::string &base,
                               const bool &writers_finished) {
  EXPECT_GT(paths.size(), 0);
  /* path to test directory */
  auto base_dir = fs::path(paths[0]).parent_path();
  /* Get absolute path to NFS directory on server */
  auto abs_path = fs::absolute(base) / base_dir;
  while (!writers_finished) {
    size_t count = 0;
    DIR *dir = opendir(abs_path.c_str());
    EXPECT_NOTNULL(dir);
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
      // ignore special directories
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      count++;
    }
    closedir(dir);
    EXPECT_TRUE(count == 0 || count == paths.size()) << "count: " << count;
  }
}
void execute_create_remove_dir(const std::vector<std::string> &paths,
                               const size_t iteration_count) {
  for (size_t t = 0; t < iteration_count; t++) {
    EXPECT_TRUE(tc::vec_mkdir_simple(paths, 0777));

    EXPECT_OK(tc::vec_unlink(paths));
  }
}

template <size_t item_count, size_t max_item_size>
void execute_create_remove_file(
    const std::vector<std::string> &paths,
    const std::array<char[max_item_size], item_count> &write_data,
    const size_t iteration_count) {
  for (size_t t = 0; t < iteration_count; t++) {
    vfile *files = tc::vec_open_simple(paths, O_CREAT | O_RDWR, 0666);
    EXPECT_NOTNULL(files);
    std::array<struct viovec, item_count> v;
    for (int i = 0; i < item_count; ++i) {
      viov2file(&v[i], &files[i], 0, sizeof(write_data[i]),
                (char *)write_data[i]);
    }
    EXPECT_OK(vec_write(v.data(), item_count, true));
    EXPECT_OK(vec_close(files, item_count));

    EXPECT_OK(vec_remove(files, item_count, true));
  }
}

template <size_t item_count, size_t max_item_size>
void execute_create(
    const std::vector<std::string> &paths,
    const std::array<char[max_item_size], item_count> &write_data) {
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_RDWR, 0666);
  EXPECT_NOTNULL(files);
  std::array<struct viovec, item_count> v;
  for (int i = 0; i < item_count; ++i) {
    viov2file(&v[i], &files[i], 0, sizeof(write_data[i]),
              (char *)write_data[i]);
  }
  EXPECT_OK(vec_write(v.data(), item_count, true));
  EXPECT_OK(vec_close(files, item_count));
}

template <size_t item_count, size_t max_item_size>
void execute_write(
    const std::vector<std::string> &paths,
    const std::array<char[max_item_size], item_count> &write_data,
    const size_t iteration_count) {
  std::array<struct viovec, item_count> v;
  for (size_t t = 0; t < iteration_count; t++) {
    for (size_t i = 0; i < item_count; ++i) {
      viov2path(&v[i], paths[i].c_str(), 0, sizeof(write_data[i]),
                (char *)write_data[i]);
    }
    EXPECT_OK(vec_write(v.data(), item_count, true));
  }
}

template <size_t item_count, size_t max_item_size>
void execute_read(
    const std::vector<std::string> &paths,
    const std::vector<std::array<char[max_item_size], item_count>> &write_sets,
    const bool &writers_finished) {
  std::array<struct viovec, item_count> v;
  std::array<char[max_item_size], item_count> buffer{0};

  const size_t buflen = item_count * max_item_size;

  while (!writers_finished) {
    std::vector<std::string> read_set;
    for (size_t i = 0; i < item_count; ++i) {
      viov2path(&v[i], paths[i].c_str(), 0, max_item_size, buffer[i]);
    }
    EXPECT_OK(vec_read(v.data(), item_count, true));

    bool is_expected = false;
    for (const auto &write_set : write_sets) {
      is_expected |= (memcmp(buffer.data(), write_set.data(), buflen) == 0);
    }

    if (!is_expected) {
      for (size_t i = 0; i < item_count; i++) {
        std::cout << buffer[i] << " ";
      }
      std::cout << "\n";
      FAIL();
    }
  }
}
TYPED_TEST_P(TcTxnTest, SerializabilityRW) {
  const std::string dir("serializable-rw");
  std::srand(std::time(0));

  /* max size of each write */
  constexpr size_t max_write_size = 40;

  /* number of files to be written, should be same as number of element in
   * each write set */
  constexpr size_t num_writes = 3;

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_writes; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  EXPECT_EQ(paths.size(), num_writes);

  /* each thread performs a write with values in the set */
  const std::vector<std::array<char[max_write_size], num_writes>> write_sets{
      std::array<char[max_write_size], num_writes>{"apple", "banana", "mango"},
      std::array<char[max_write_size], num_writes>{"new york", "california",
                                                   "texas"}};
  /* reader and writer threads */
  std::vector<std::thread> writer_threads, reader_threads;

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));

  /* create files in paths[] and write from set1 */
  execute_create(paths, write_sets[0]);

  bool writers_finished = false;
  /* start a thread performing VWrite with values in each write set */
  for (size_t i = 0; i < FLAGS_nwriter_threads; i++) {
    const auto &write_set = write_sets[std::rand() % write_sets.size()];
    writer_threads.emplace_back(execute_write<num_writes, max_write_size>,
                                std::ref(paths), std::ref(write_set),
                                FLAGS_nwriter_iterations);
  }

  /* read from files */
  for (size_t i = 0; i < FLAGS_nreader_threads; i++) {
    reader_threads.emplace_back(execute_read<num_writes, max_write_size>,
                                std::ref(paths), std::ref(write_sets),
                                std::ref(writers_finished));
  }

  /* wait for threads */
  for (auto &thread : writer_threads) {
    thread.join();
  }
  writers_finished = true;
  for (auto &thread : reader_threads) {
    thread.join();
  }

  EXPECT_OK(tc::sca_unlink_recursive(dir));
}

TYPED_TEST_P(TcTxnTest, SerializabilityFileCR) {
  const std::string dir("serializable-file-cr");

  /* max size of each write */
  constexpr size_t max_write_size = 40;

  /* number of files to be written, should be same as number of element in
   * each write set */
  constexpr size_t num_writes = 3;

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_writes; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  EXPECT_EQ(paths.size(), num_writes);

  /* each thread performs a write with values in the set */
  std::array<char[max_write_size], num_writes> write_set{"apple", "banana",
                                                         "mango"};
  /* creator + remover and open threads */
  std::thread create_remove_thread;
  std::vector<std::thread> open_threads;

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  bool writers_finished = false;

  /* create files in paths[] and write from set1 */
  create_remove_thread = std::thread(
      execute_create_remove_file<num_writes, max_write_size>, std::ref(paths),
      std::ref(write_set), FLAGS_nwriter_iterations);

  /* use posix api's to verify either all files exist or none */
  for (size_t i = 0; i < FLAGS_nreader_threads; i++) {
    open_threads.emplace_back(execute_posix_path_exists, std::ref(paths),
                              std::ref(this->nfs_base),
                              std::ref(writers_finished));
  }

  /* wait for threads */
  create_remove_thread.join();
  writers_finished = true;
  for (auto &thread : open_threads) {
    thread.join();
  }

  EXPECT_OK(tc::sca_unlink_recursive(dir));
}
TYPED_TEST_P(TcTxnTest, SerializabilityDirCR) {
  const std::string dir("serializable-dir-cr");

  /* number of directories */
  constexpr size_t num_directories = 3;

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_directories; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  /* creator + remover and open threads */
  std::thread create_remove_thread;
  std::vector<std::thread> open_threads;

  /* create base dir */
  ASSERT_TRUE(tc::sca_mkdir(dir, 0777));
  bool writers_finished = false;

  /* create files in paths[] and write from set1 */
  create_remove_thread = std::thread(execute_create_remove_dir, std::ref(paths),
                                     FLAGS_nwriter_iterations);

  /* use posix api's to verify either all files exist or none */
  for (size_t i = 0; i < FLAGS_nreader_threads; i++) {
    open_threads.emplace_back(execute_posix_path_exists, std::ref(paths),
                              std::ref(this->nfs_base),
                              std::ref(writers_finished));
  }

  /* wait for threads */
  create_remove_thread.join();
  writers_finished = true;
  for (auto &thread : open_threads) {
    thread.join();
  }

  EXPECT_OK(tc::sca_unlink_recursive(dir));
}
REGISTER_TYPED_TEST_CASE_P(TcTxnTest, BadMkdir, BadMkdir2, BadFileCreation,
                           BadFileCreation2, BadOpenWithTrunc, BadRemove,
                           BadRemoveCheckContent, BadCreationWithExisting,
                           BadLink, BadSymLink, BadWrite, BadWriteMiddle,
                           BadWriteExpanding, UUIDOpenExclFlagCheck,
                           UUIDOpenFlagCheck, UUIDReadFlagCheck,
                           SerializabilityRW, SerializabilityFileCR,
                           SerializabilityDirCR);

typedef ::testing::Types<TcNFS4Impl> TcTxnImpls;
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  gflags::SetUsageMessage(
      "Note: Flags are only used for multi-threaded Serializability tests");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  INSTANTIATE_TYPED_TEST_CASE_P(TC, TcTxnTest, TcTxnImpls);

  return RUN_ALL_TESTS();
}
