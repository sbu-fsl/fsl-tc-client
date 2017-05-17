/**
 * Copyright (C) Stony Brook University 2017
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

#include <unistd.h>
#include <gtest/gtest.h>
#include "tc_cache/TC_DataCache.h"

using std::string;

TEST(TC_DataCacheTest, CacheEntryExpireAfterTimeout)
{
	const int expire_sec = 5;
	char *buf = (char *)malloc(CACHE_BLOCK_SIZE);
	const string PATH = "/foo/bar";
	TC_DataCache cache(10, expire_sec * 1000000);
	cache.put(PATH, 0, CACHE_BLOCK_SIZE, buf);

	EXPECT_TRUE(cache.isCached(PATH));
	char *read_buf = (char *)malloc(CACHE_BLOCK_SIZE);
	EXPECT_EQ(CACHE_BLOCK_SIZE,
		  cache.get(PATH, 0, CACHE_BLOCK_SIZE, read_buf));

	sleep(expire_sec + 1);

	EXPECT_FALSE(cache.isCached(PATH));
	EXPECT_EQ(0, cache.get(PATH, 0, CACHE_BLOCK_SIZE, read_buf));

	free(buf);
	free(read_buf);
}
