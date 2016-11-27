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
 * This is an example creating and writing to a file with one RPC.
 * It has the same effect as the bash command:
 *
 *  $ echo "hello world" > /vfs0/hello.txt
 *
 * @file tc_test_append.c
 * @brief Test create and append to a small file from NFS using TC.
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
#include "../nfs4/nfs4_util.h"

static char exe_path[PATH_MAX];
static char tc_config_path[PATH_MAX];

#define DEFAULT_LOG_FILE "/tmp/tc_test_append.log"

#define TC_TEST_NFS_FILE "/vfs0/abcd.txt"

int main(int argc, char *argv[])
{
	void *context = NULL;
	tc_res res;
	char data[32];
	struct tc_attrs attrs1;
	struct tc_attrs attrs2;
	int i;

	/* Locate and use the default config file in the repo.  Before running
	 * this example, please update the config file to a correct NFS server.
	 */
	readlink("/proc/self/exe", exe_path, PATH_MAX);
	snprintf(tc_config_path, PATH_MAX,
		 "%s/../../../config/tc.ganesha.conf", dirname(exe_path));
	fprintf(stderr, "using config file: %s\n", tc_config_path);

	/* Initialize TC services and daemons */
	context = tc_init(tc_config_path, DEFAULT_LOG_FILE, 77);
	if (context == NULL) {
		NFS4_ERR("Error while initializing tc_client using config "
			 "file: %s; see log at %s",
			 tc_config_path, DEFAULT_LOG_FILE);
		return EIO;
	}

/*
	attrs1.file = tc_file_from_path(TC_TEST_NFS_FILE);
	attrs1.masks = TC_ATTRS_MASK_NONE;
	attrs1.masks.has_size = true;
	attrs1.size = 2742;

	res = tc_setattrsv(&attrs1, 1, false);

	if (tc_okay(res)) {
		fprintf(stderr,
			"Successfully sent setattr for %s "
			"size: %u\n", TC_TEST_NFS_FILE, attrs1.size);
	} else {
		fprintf(stderr,
			"Failed to getattr file \"%s\" at the %d-th operation "
			"with error code %d (%s). See log file for details: "
			"%s\n",
			TC_TEST_NFS_FILE, res.index, res.err_no,
			strerror(res.err_no), DEFAULT_LOG_FILE);
	}
*/

	attrs1.file = tc_file_from_path(TC_TEST_NFS_FILE);
	attrs1.masks = TC_ATTRS_MASK_NONE;
	attrs1.masks.has_size = true;

	res = tc_getattrsv(&attrs1, 1, false);

	/* Check results. */
	if (tc_okay(res)) {
		fprintf(stderr,
			"Successfully sent getattr for %s "
			"size: %u\n", TC_TEST_NFS_FILE, attrs1.size);
	} else {
		fprintf(stderr,
			"Failed to getattr file \"%s\" at the %d-th operation "
			"with error code %d (%s). See log file for details: "
			"%s\n",
			TC_TEST_NFS_FILE, res.index, res.err_no,
			strerror(res.err_no), DEFAULT_LOG_FILE);
	}

	attrs1.file = tc_file_from_path(TC_TEST_NFS_FILE);
	attrs1.masks = TC_ATTRS_MASK_NONE;
	attrs1.masks.has_size = true;
	attrs1.size = 2742;

	res = tc_setattrsv(&attrs1, 1, false);

	if (tc_okay(res)) {
		fprintf(stderr,
			"Successfully sent setattr for %s "
			"size: %u\n", TC_TEST_NFS_FILE, attrs1.size);
	} else {
		fprintf(stderr,
			"Failed to getattr file \"%s\" at the %d-th operation "
			"with error code %d (%s). See log file for details: "
			"%s\n",
			TC_TEST_NFS_FILE, res.index, res.err_no,
			strerror(res.err_no), DEFAULT_LOG_FILE);
	}

	//sleep(1); // This would remove the element from the cache

	attrs2.file = attrs1.file;
	attrs2.masks = TC_ATTRS_MASK_NONE;
	attrs2.masks.has_size = true;

	res = tc_getattrsv(&attrs2, 1, false);

	/* Check results. */
	if (tc_okay(res)) {
		fprintf(stderr,
			"Successfully sent getattr for %s "
			"size: %u\n", TC_TEST_NFS_FILE, attrs2.size);
	} else {
		fprintf(stderr,
			"Failed to getattr file \"%s\" at the %d-th operation "
			"with error code %d (%s). See log file for details: "
			"%s\n",
			TC_TEST_NFS_FILE, res.index, res.err_no,
			strerror(res.err_no), DEFAULT_LOG_FILE);
	}

	tc_deinit(context);

	return res.err_no;
}
