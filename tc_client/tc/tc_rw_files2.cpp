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
#include <random>
#include <string>
#include <thread>
#include <vector>

DEFINE_bool(tc, true, "Use TC implementation");

DEFINE_bool(read, true, "Read or write");

DEFINE_bool(verbose, false, "Nag more about progress");

DEFINE_int32(nfiles, 1000, "Number of files");

DEFINE_int32(nthreads, 4, "Number of threads to r/w concurrently");

DEFINE_int32(overlap, 0, "Percentage of access overlap (0-100)");

DEFINE_string(overlap_style, "rear",
              "How to place overlap?"
              "(front, rear, random)");

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

void arrangeTask(int indivStart, int indivEnd, int commonStart, int commonEnd,
                 std::vector<int> &tasks, std::string style) {
  if (style == "front") {
    for (int i = commonStart; i < commonEnd; ++i) tasks.push_back(i);
    for (int i = indivStart; i < indivEnd; ++i) tasks.push_back(i);
  } else if (style == "rear") {
    for (int i = indivStart; i < indivEnd; ++i) tasks.push_back(i);
    for (int i = commonStart; i < commonEnd; ++i) tasks.push_back(i);
  } else if (style == "random") {
    int p1 = indivStart, p2 = commonStart;
    int total = (indivEnd - indivStart) + (commonEnd - commonStart);
    float commonProb = 1.0 * (commonEnd - commonStart) / total;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> uniform_rand(0.0, 1.0);
    while (p1 < indivEnd && p2 < commonEnd) {
      float rn = uniform_rand(gen);
      if (rn <= commonProb) {
        /* Add a common task */
        tasks.push_back(p2);
        ++p2;
      } else {
        /* Add a individual task */
        tasks.push_back(p1);
        ++p1;
      }
    }
    /* Add the rest if there are remainings */
    if (p1 < indivEnd) {
      for (int i = p1; i < indivEnd; ++i) tasks.push_back(i);
    }
    if (p2 < commonEnd) {
      for (int i = p2; i < commonEnd; ++i) tasks.push_back(i);
    }
  }
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
  int files_per_thread = total_files / nthreads;
  int overlap_rate = FLAGS_overlap;
  int commons = files_per_thread * overlap_rate / 100;
  int independants = files_per_thread - commons;
  for (int i = 0; i < nthreads; ++i) {
    std::vector<int> tasks;
    arrangeTask(filenum, filenum + independants, total_files - commons,
                total_files, tasks, FLAGS_overlap_style);
    worklist.push_back(tasks);
    filenum += independants;

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

bool checkParameter(void) {
  bool res = true;
  if (FLAGS_overlap < 0 || FLAGS_overlap > 100) {
    fprintf(stderr, "Overlap rate should be in range of 0-100.\n");
    res = false;
  }
  if (FLAGS_overlap_style != "rear" && FLAGS_overlap_style != "front" &&
      FLAGS_overlap_style != "random") {
    fprintf(stderr, "Overlap style can only be front, rear or random.\n");
    res = false;
  }
  return res;
}

int main(int argc, char *argv[]) {
  std::string usage("This program issues read requests to files.\nUsage: ");
  usage += argv[0];
  usage += "  <dir-path>";
  gflags::SetUsageMessage(usage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (!checkParameter()) {
    fprintf(stderr, "There is one or more error(s) in the CMD parameter.\n");
    return 1;
  }
  Run(argv[1]);
  return 0;
}
