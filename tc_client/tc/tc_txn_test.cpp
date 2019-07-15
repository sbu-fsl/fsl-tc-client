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

TYPED_TEST_CASE_P(TcTxnTest);

static bool vec_mkdir_simple(const char **paths, int n)
{
  struct vattrs *attrs = (struct vattrs*)alloca(n * sizeof(*attrs));
  struct vattrs_masks mask;
  if (!attrs)
    return false;

  mask.has_mode = 1;
  mask.has_uid = 1;
  mask.has_gid = 1;

  for (int i = 0; i < n; ++i) {
    attrs[i].file = vfile_from_path(paths[i]);
    attrs[i].mode = 0777;
    attrs[i].uid = 0;
    attrs[i].gid = 0;
    attrs[i].masks = mask;
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
  ASSERT_TRUE(vec_mkdir_simple(&PATHS[2], 1));

  /* execute compound */
  EXPECT_FALSE(vec_mkdir_simple(PATHS, N));

  /* check existence of PATHS[0] and PATHS[1] */
  EXPECT_FALSE(sca_exists(PATHS[0]));
  EXPECT_FALSE(sca_exists(PATHS[1]));
  EXPECT_TRUE(sca_exists(PATHS[2]));
  vec_unlink(&PATHS[2], 1);
  EXPECT_FALSE(sca_exists(PATHS[3]));
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
        UUIDOpenExclFlagCheck,
        UUIDExclFlagCheck,
        UUIDOpenFlagCheck,
        UUIDReadFlagCheck);

typedef ::testing::Types<TcNFS4Impl> TcTxnImpls;
INSTANTIATE_TYPED_TEST_CASE_P(TC, TcTxnTest, TcTxnImpls);
