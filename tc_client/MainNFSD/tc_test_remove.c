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
 * This is an example removing multiple files in one RPC.
 * It has the same effect as the bash command:
 *
 *  $ rm /vfs0/rmdir/{a,b,c,d,e}
 *
 * @file tc_test_remove.c
 * @brief Test removing multiple files.
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

#define DEFAULT_LOG_FILE "/tmp/tc_test_remove.log"

#define TC_TEST_NFS_DIR "/vfs0/rmdir"

int main(int argc, char *argv[])
{
	void *context = NULL;
	const int N = 5;
	int i;
	vfile files[N];
	struct viovec file_iov[N];
	vres res;
	const char *file_paths[] = { "/vfs0/rmdir/a", "/vfs0/rmdir/b",
				     "/vfs0/rmdir/c", "/vfs0/rmdir/d",
				     "/vfs0/rmdir/e" };

	/* Initialize TC services and daemons */
	context = vinit(get_tc_config_file(tc_config_path, PATH_MAX),
			DEFAULT_LOG_FILE, 77);
	if (context == NULL) {
		NFS4_ERR("Error while initializing tc_client using config "
			 "file: %s; see log at %s",
			 tc_config_path, DEFAULT_LOG_FILE);
		return EIO;
	}

	res = sca_ensure_dir(TC_TEST_NFS_DIR, 0755, NULL);
	if (!vokay(res)) {
		NFS4_ERR("failed to create parent directory %s",
			 TC_TEST_NFS_DIR);
		goto exit;
	}

	/* Setup I/O request */
	for (i = 0; i < N; ++i) {
		files[i] = vfile_from_path(file_paths[i]);
		file_iov[i].file = files[i];
		file_iov[i].is_creation = true;
		file_iov[i].offset = 0;
		/* The file content is its path */
		file_iov[i].length = strlen(file_paths[i]);
		file_iov[i].data = (char *)file_paths[i];
	}

	/* Write the file using NFS compounds; nfs4_writev() will open the file
	 * with CREATION flag, write to it, and then close it. */
	res = vec_write(file_iov, N, false);
	if (vokay(res)) {
		fprintf(stderr, "Successfully created %d test files\n", N);
	} else {
		fprintf(stderr, "Failed to create test files\n");
		goto exit;
	}

	res = vec_remove(files, N, false);
	if (vokay(res)) {
		fprintf(stderr, "Successfully removed %d test files\n", N);
	} else {
		fprintf(stderr,
			"Failed to remove %d-th file with error code %d "
			"(%s). See log file at %s for details.\n",
			res.index, res.err_no, strerror(res.err_no),
			DEFAULT_LOG_FILE);
	}

exit:
	vdeinit(context);

	return res.err_no;
}
