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
#include <list>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "tc_test.hpp"

template<typename T>
using TcTxnTest = TcTest<T>;

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

/**
 * Issue a compound of several mkdir commands, where there is an invalid one
 * in the middle (causing EEXIST).
 *
 * Expected: Compound fails, and no new directories will be created
 */
TYPED_TEST_P(TcTxnTest, BadMkdir)
{
  const int N = 4;
  const char *PATHS[] = { "bad-mkdir-a",
                          "bad-mkdir-b",
                          "bad-mkdir-c",
                          "bad-mkdir-d" };

  /* preparation: create a directory */
  ASSERT_TRUE(vec_mkdir_simple(&PATHS[2], 1, 0777));

  /* execute compound */
  EXPECT_FALSE(vec_mkdir_simple(PATHS, N, 0777));

  vec_unlink(&PATHS[2], 1);

  /* check existence of PATHS[0] and PATHS[1] */
  EXPECT_FALSE(sca_exists(PATHS[0]));
  EXPECT_FALSE(sca_exists(PATHS[1]));
  EXPECT_FALSE(sca_exists(PATHS[2]));
  EXPECT_FALSE(sca_exists(PATHS[3]));
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

  EXPECT_FALSE(sca_exists(paths2[0]));
  EXPECT_FALSE(sca_exists(paths2[1]));
  EXPECT_FALSE(sca_exists(paths2[2]));
  EXPECT_FALSE(sca_exists(paths2[3]));
  EXPECT_FALSE(sca_exists(paths2[4]));
  EXPECT_FALSE(sca_exists(paths2[5]));
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
        UUIDOpenExclFlagCheck,
        UUIDExclFlagCheck,
        UUIDOpenFlagCheck,
        UUIDReadFlagCheck);

typedef ::testing::Types<TcNFS4Impl> TcTxnImpls;
INSTANTIATE_TYPED_TEST_CASE_P(TC, TcTxnTest, TcTxnImpls);
