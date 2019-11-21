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

#include <error.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tc_api.h"
#include "tc_helper.h"
#include "path_utils.h"
#include "tc_bench_util.h"

#include <gflags/gflags.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

DEFINE_bool(tc, true, "Use TC implementation");

DEFINE_bool(read, true, "Read or write");

DEFINE_bool(verbose, false, "Nag more about progress");

DEFINE_int32(nfiles, 1000, "Number of files");

DEFINE_int32(nthreads, 4, "Number of threads to r/w concurrently");

using std::vector;

const size_t kSizeLimit = (16 << 20);

static char *GetFilePath(const char *dir, int i) {
  char *p = (char *)malloc(PATH_MAX);

  if (p) snprintf(p, PATH_MAX, "%s/%04d", dir, i);

  return p;
}

void worker(const char *dir, std::vector<int> &filenums, size_t file_size) {
  int files_finished = 0;
  int nfiles = filenums.size();
  /* bytes_finished: Amount of data read/written */
  vector<size_t> bytes_finished(nfiles, 0);  // per file
  /* bytes_reading: Amount of data pending in iovec but not committed */
  vector<size_t> bytes_reading(nfiles, 0);  // per file
  const size_t kIoSizeThreshold = 1 << 20;

  char *data = (char *)malloc(kSizeLimit);
  size_t bytes = 0;

  while (files_finished < nfiles) {
    vector<viovec> iovs;

    /* Assemble IO vectors */
    for (int i = files_finished; i < nfiles;) {
      /* Let's say, each iovec r/w up to 16M of data. */
      if (bytes >= kSizeLimit) {
        break;
      }
      if (bytes_finished[i] == file_size) {
        ++i;
        continue;
      }
      struct viovec iov;
      size_t iosize = std::min<size_t>(
          kIoSizeThreshold, file_size - (bytes_finished[i] + bytes_reading[i]));
      iosize = std::min(iosize, kSizeLimit - bytes);

      viov2path(&iov, GetFilePath(dir, filenums[i]),
                bytes_finished[i] + bytes_reading[i], iosize, data + bytes);
      iovs.push_back(std::move(iov));

      bytes += iosize;
      bytes_reading[i] += iosize;

      if (bytes_finished[i] + bytes_reading[i] == file_size) {
        ++i;
      }
    }

    vres tcres;

    /* Execute r/w operations */
    if (FLAGS_read) {
      tcres = vec_read(iovs.data(), iovs.size(), false);
    } else {
      tcres = vec_write(iovs.data(), iovs.size(), false);
    }

    if (!vokay(tcres)) {
      error(1, tcres.err_no, "failed to read %s", iovs[tcres.index].file.path);
    }

    /* Summary and cleanup */
    int new_files_finished = files_finished;
    int i = files_finished;
    for (int j = 0; j < iovs.size(); ++j) {
      while (bytes_finished[i] == file_size) {
        ++i;
      }
      bytes_finished[i] += iovs[j].length;
      if (bytes_finished[i] == file_size && new_files_finished == i) {
        if (++new_files_finished % 100 == 0 && FLAGS_verbose) {
          fprintf(stderr, "Finished %d files\n", new_files_finished);
        }
      }
      free((char *)iovs[j].file.path);
      bytes_reading[i] = 0;
    }

    files_finished = new_files_finished;
    bytes = 0;
  }

  free(data);
}

void Run(const char *dir) {
  int total_files = FLAGS_nfiles;
  int nthreads = FLAGS_nthreads;
  std::vector<std::thread> threads;
  void *tcdata = SetUp(FLAGS_tc);

  /* Assume all files are equally sized */
  char *sample_path = GetFilePath(dir, 0);
  size_t file_size = GetFileSize(sample_path);
  size_t total_size = file_size * total_files;
  free(sample_path);

  /* distribute tasks */
  std::vector<std::vector<int> > worklist;
  int filenum = 0;
  bool has_leftover = (total_files % nthreads > 0);
  for (int i = 0; i < nthreads; ++i) {
    std::vector<int> tasks;
    int ntasks = total_files / nthreads;
    if (has_leftover && i == nthreads - 1) {
      ntasks += total_files % nthreads;
    }
    for (int fn = 0; fn < ntasks; ++fn) {
      tasks.push_back(fn + filenum);
    }
    worklist.push_back(tasks);
    filenum += ntasks;

    if (FLAGS_verbose) {
      printf("thread %d: works: ", i);
      for (int fn : tasks) {
        printf("%d ", fn);
      }
      printf("\n");
    }
  }

  /* start timer */
  auto start = std::chrono::high_resolution_clock::now();

  /* apply threads */
  for (int i = 0; i < nthreads; ++i) {
    threads.emplace_back(worker, dir, std::ref(worklist[i]), file_size);
  }

  /* wait for worker threads to finish */
  for (auto &th : threads) {
    th.join();
  }

  /* end timer - calculate elapsed time and average throughput*/
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  double throughput = total_size / elapsed.count();

  fprintf(stdout, "%.6f secs, %.2f MB/s\n", elapsed.count(),
          throughput / 1048576);
  TearDown(tcdata);
}

int main(int argc, char *argv[]) {
  std::string usage("This program issues read requests to files.\nUsage: ");
  usage += argv[0];
  usage += "  <dir-path>";
  gflags::SetUsageMessage(usage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  Run(argv[1]);
  return 0;
}
