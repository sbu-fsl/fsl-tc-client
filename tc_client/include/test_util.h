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

#ifndef __TC_TC_TEST_UTIL_H__
#define __TC_TC_TEST_UTIL_H__

#include <thread>
#include <functional>

constexpr size_t operator"" _KB(unsigned long long a) { return a << 10; }
constexpr size_t operator"" _MB(unsigned long long a) { return a << 20; }
constexpr size_t operator"" _GB(unsigned long long a) { return a << 30; }

#define TCTEST_ERR(fmt, args...) LogCrit(COMPONENT_TC_TEST, fmt, ##args)
#define TCTEST_WARN(fmt, args...) LogWarn(COMPONENT_TC_TEST, fmt, ##args)
#define TCTEST_INFO(fmt, args...) LogInfo(COMPONENT_TC_TEST, fmt, ##args)
#define TCTEST_DEBUG(fmt, args...) LogDebug(COMPONENT_TC_TEST, fmt, ##args)

#define EXPECT_OK(x)                                                           \
	EXPECT_TRUE(vokay(x)) << "Failed at " << x.index << ": "             \
				<< strerror(x.err_no)
#define EXPECT_FAIL(x) \
  EXPECT_FALSE(vokay(x)) << "Expected to fail but succeed"

#define EXPECT_NOTNULL(x) EXPECT_TRUE(x != NULL) << #x << " is NULL"

char *getRandomBytes(int N);

void DoParallel(int nthread, std::function<void(int)> worker);

#endif  // __TC_TC_TEST_UTIL_H__
