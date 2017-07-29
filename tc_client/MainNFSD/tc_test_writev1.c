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
 * This is an example creating and writing to 2 files with one RPC.
 * It has the same effect as the bash command:
 *
 *  $ echo "hello world" > /vfs0/test/a/abcd0
 *  $ echo "hello world" > /vfs0/test/a/abcd1
 *
 * @file tc_test_writev1.c
 * @brief Test create and write to 2 small files from NFS using TC.
 *
 */
#include "config.h"
#include "nfs_init.h"
#include "fsal.h"
#include "log.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>		/* for sigaction */
#include <errno.h>
#include <libgen.h>		/* used for 'dirname' */
#include "../nfs4/nfs4_util.h"
#include "tc_helper.h"

static char tc_config_path[PATH_MAX];

#define DEFAULT_LOG_FILE "/tmp/tc_test_writev1.log"

#define TC_TEST_NFS_FILE0 "/vfs0/test/a/abcd0"
#define TC_TEST_NFS_FILE1 "/vfs0/test/a/abcd1"

int main(int argc, char *argv[])
{
	void *context = NULL;
	struct viovec write_iovec[2];
	vres res;
	const char *data = "hello world";

	/* Initialize TC services and daemons */
	context = vinit(get_tc_config_file(tc_config_path, PATH_MAX),
			DEFAULT_LOG_FILE, 77);
	if (context == NULL) {
		NFS4_ERR("Error while initializing tc_client using config "
			 "file: %s; see log at %s",
			 tc_config_path, DEFAULT_LOG_FILE);
		return EIO;
	}

	/* Setup I/O request */
	write_iovec[0].file = vfile_from_path(TC_TEST_NFS_FILE0);
	write_iovec[0].is_creation = true;
	write_iovec[0].offset = 0;
	write_iovec[0].length = strlen(data);
	write_iovec[0].data = (char *)data;

	write_iovec[1].file = vfile_from_path(TC_TEST_NFS_FILE1);
	write_iovec[1].is_creation = true;
	write_iovec[1].offset = 0;
	write_iovec[1].length = strlen(data);
	write_iovec[1].data = (char *)data;

	/* Write the file using NFS compounds; nfs4_writev() will open the file
	 * with CREATION flag, write to it, and then close it. */
	res = vec_write(write_iovec, 2, true);

	/* Check results. */
	if (vokay(res)) {
		fprintf(stderr,
			"Successfully write the first %zu bytes of file \"%s\" "
			"via NFS.\n",
			write_iovec[0].length, TC_TEST_NFS_FILE0);
		fprintf(stderr,
			"Successfully write the first %zu bytes of file \"%s\" "
			"via NFS.\n",
			write_iovec[1].length, TC_TEST_NFS_FILE1);
	} else {
		fprintf(stderr,
			"Failed to write file at the %d-th operation "
			"with error code %d (%s). See log file for details: "
			"%s\n",
			res.index, res.err_no,
			strerror(res.err_no),
			DEFAULT_LOG_FILE);
	}

	vdeinit(context);

	return res.err_no;
}
