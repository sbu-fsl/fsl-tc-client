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
#include <list>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "tc_test.hpp"

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

TYPED_TEST_P(TcTxnTest, UUIDExclFlagCheck)
{
	const int N = 4;
	const char *PATHS[] = { "TcTxnTest-1-open.txt",
	                        "TcTxnTest-2-open.txt",
	                        "TcTxnTest-3-open.txt",
	                        "TcTxnTest-4-open.txt" };
	vfile *files;

	Removev(PATHS, 4);

	files = vec_open_simple(PATHS, N, O_EXCL, 0);
	ASSERT_TRUE(files == NULL);
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
	ASSERT_TRUE(files == NULL);
	vec_close(files, N);

}

REGISTER_TYPED_TEST_CASE_P(TcTxnTest,
        BadMkdir,
        BadMkdir2,
        BadFileCreation,
        BadFileCreation2,
        UUIDOpenExclFlagCheck,
        UUIDExclFlagCheck,
        UUIDOpenFlagCheck,
        UUIDReadFlagCheck);

typedef ::testing::Types<TcNFS4Impl> TcTxnImpls;
INSTANTIATE_TYPED_TEST_CASE_P(TC, TcTxnTest, TcTxnImpls);
