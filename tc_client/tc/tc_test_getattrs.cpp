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
 *
 * This is for benchmarking of tc metadata cache using getattr ops
 */

#include <error.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tc_api.h"
#include "tc_helper.h"

#include <string>
#include <vector>
#include <iostream>
#include <ctime>
#include <sys/time.h>
#include <algorithm>
#include <chrono>
#include <random>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <functional>

#define DEBUG 0

using namespace std;
using namespace  boost::accumulators;
using namespace std::placeholders;

const size_t BUFSIZE = 4096;

/* need to deinit cache before every repetition */
extern void deinit_page_cache();
extern int get_miss_count();
extern void reset_miss_count();

/* Generates path name in using gamma-distributed
 * n is number of required path names
 * l is the number of loops
 */
static vector<const char *> NewPaths(const char *format, int n, int l)
{
	unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
	std::default_random_engine generator(seed);
	std::gamma_distribution<double> distribution (7.0,13.0);
	int m = n * l;	// to define the range of file to be selected for benchmarking

	vector<const char *> paths(n);
	for (int i = 0; i < n; ++i) {
		char *p = (char *)malloc(PATH_MAX);
		int d = int(distribution(generator)) % m;
		assert(p);
		snprintf(p, PATH_MAX, format, d);
		paths[i] = p;
	}
	return paths;
}

static vector<tc_attrs> NewTcAttrs(size_t nfiles, int nloop, tc_attrs *values = nullptr)
{
	vector<const char *> paths = NewPaths("tc_cache/file-%d", nfiles, nloop);
	vector<tc_attrs> attrs(nfiles);

#if DEBUG
	vector<const char *>::iterator it;
	for (it = paths.begin(); it != paths.end(); ++it) {
		std::cout << *it << std::endl;
	}
#endif

	for (size_t i = 0; i < nfiles; ++i) {
		if (values) {
			attrs[i] = *values;
		} else {
			attrs[i].masks = TC_ATTRS_MASK_ALL;
		}
		attrs[i].file = tc_file_from_path(paths[i]);
	}

	return attrs;
}

static void FreeTcAttrs(vector<tc_attrs> *attrs)
{
	for (const auto& at : *attrs) {
		free((char *)at.file.path);
	}
}

static inline struct timespec totimespec(long sec, long nsec)
{
	struct timespec tm = {
		.tv_sec = sec,
		.tv_nsec = nsec,
	};
	return tm;
}

static void* SetUp()
{
	void *context;
	char buf[PATH_MAX];
	context = tc_init(get_tc_config_file(buf, PATH_MAX),
				  "/tmp/tc-test-getattrs.log", 77);
#if DEBUG
	fprintf(stderr, "Using config file at %s\n", buf);
#endif
	return context;
}

static void TearDown(void *context)
{
	tc_deinit(context);
}

double test_getattrs(size_t n, int l) {
	int i;
	long seconds, useconds;
	double duration = 0.0;
	double agg_duration = 0.0;
	timeval start, end;

	for (i = 0; i < l; ++i) {
		/* generate a vector of n tc_attrs, having random file paths
		 * ranges from 0 to n * l, using gamma distribution
		 */
		vector<tc_attrs> attrs = NewTcAttrs(n, l);

		gettimeofday(&start, 0);

		tc_res tcres = tc_getattrsv(attrs.data(), n, false);
		assert(tc_okay(tcres));

		gettimeofday(&end, 0);

		FreeTcAttrs(&attrs);

		seconds  = end.tv_sec  - start.tv_sec;
		useconds = end.tv_usec - start.tv_usec;
		duration = (seconds + useconds/1000000.0) * 1000;	// in ms

		agg_duration += duration;
#if DEBUG
		cout << "duration: " << duration << " agg_duration: " << agg_duration << endl;
#endif

	}

	return agg_duration;
}

void tc_getattrs_bench(size_t n, int l, int r) {
	int i;
	vector<double> time_taken(r);
	accumulator_set<double, stats<tag::variance> > acc;
	vector<int> miss_count(r);
	accumulator_set<int, stats<tag::variance> > acc_m;

	for (i = 0; i < r; ++i) {
		deinit_page_cache();	// clear cache before each repetitions
		reset_miss_count();	// reset miss counter
		time_taken[i] = test_getattrs(n, l);	// time taken during this iteration
		miss_count[i] = get_miss_count();		// counter for misses in the cache
	}

	for_each(time_taken.begin(), time_taken.end(), bind<void>(ref(acc), _1));
	cout << "Time taken: mean: " << mean(acc) << " SD: " << sqrt(variance(acc)) << endl;
	for_each(miss_count.begin(), miss_count.end(), bind<void>(ref(acc_m), _1));
	cout << "Miss counts: mean: " << mean(acc_m) << " SD: " << sqrt(variance(acc_m)) << endl;

#if DEBUG
	vector<double>::iterator it;
	for (it = time_taken.begin(); it != time_taken.end(); ++it) {
		cout << *it << endl;
	}
#endif
}

int main(int argc, char **argv)
{
	extern char *optarg;
	int c;
	size_t nfiles = 1;	// no of ops in the vector of tc_attrs
	int repetitions = 1;	// no of repetitions for the experiment
	int nloops = 1;		// no of loops per repetition
	void *context = SetUp();

	while ((c = getopt(argc, argv, "l:n:r:")) != -1) {
		switch (c) {
			case 'l':
				nloops = atoi(optarg);
				break;
			case 'n':
				nfiles = atoi(optarg);
				break;
			case 'r':
				repetitions = atoi(optarg);
				break;
		}
	}
#if DEBUG
	cout << "nloops = " << nloops << endl;
	cout << "nfiles = " << nfiles << endl;
	cout << "repetitions = " << repetitions << endl;
#endif

	tc_getattrs_bench(nfiles, nloops, repetitions);
	TearDown(context);

	return 0;
}
