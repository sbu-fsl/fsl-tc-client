/**
 * Copyright (C) Stony Brook University 2019
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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <experimental/filesystem>
#include <experimental/random>
#include <list>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tc_api_wrapper.hpp"
#include "tc_test.hpp"

namespace stdexp = std::experimental;
namespace fs = stdexp::filesystem;

/* Flags for Serializability tests */
/* number of writer threads */
DEFINE_int32(nwriter_threads, 2, "Number of writer threads");

/* number of reader threads */
DEFINE_int32(nreader_threads, 5, "Number of reader threads");

/* number of operations in writer thread */
DEFINE_int32(nwriter_iterations, 50, "Number of iterations in writer thread");

DEFINE_bool(skip_test_setup, false,
            "Skip directory and file creation or removal. Needed for "
            "running tests across multiple process.");

TYPED_TEST_CASE_P(TcLockTest);

void execute_posix_path_exists(const std::vector<std::string> &paths,
                               const std::string &base,
                               const bool &writers_finished) {
  EXPECT_GT(paths.size(), 0);
  /* path to test directory */
  auto base_dir = fs::path(paths[0]).parent_path();
  /* Get absolute path to NFS directory on server */
  auto abs_path = fs::absolute(base) / base_dir;
  while (!writers_finished) {
    size_t count = 0;
    DIR *dir = opendir(abs_path.c_str());
    EXPECT_NOTNULL(dir);
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
      // ignore special directories
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      count++;
    }
    closedir(dir);
    EXPECT_TRUE(count == 0 || count == paths.size()) << "count: " << count;
  }
}
void execute_create_remove_dir(const std::vector<std::string> &paths,
                               const size_t iteration_count) {
  for (size_t t = 0; t < iteration_count; t++) {
    EXPECT_TRUE(tc::vec_mkdir_simple(paths, 0777));

    EXPECT_OK(tc::vec_unlink(paths));
  }
}

template <size_t item_count, size_t max_item_size>
void execute_create_remove_file(
    const std::vector<std::string> &paths,
    const std::array<char[max_item_size], item_count> &write_data,
    const size_t iteration_count) {
  for (size_t t = 0; t < iteration_count; t++) {
    vfile *files = tc::vec_open_simple(paths, O_CREAT | O_RDWR, 0666);
    EXPECT_NOTNULL(files);
    std::array<struct viovec, item_count> v;
    for (int i = 0; i < item_count; ++i) {
      viov2file(&v[i], &files[i], 0, sizeof(write_data[i]),
                (char *)write_data[i]);
    }
    EXPECT_OK(vec_write(v.data(), item_count, true));
    EXPECT_OK(vec_close(files, item_count));

    EXPECT_OK(vec_remove(files, item_count, true));
  }
}

template <size_t item_count, size_t max_item_size>
void execute_create(
    const std::vector<std::string> &paths,
    const std::array<char[max_item_size], item_count> &write_data) {
  vfile *files = tc::vec_open_simple(paths, O_CREAT | O_RDWR, 0666);
  EXPECT_NOTNULL(files);
  std::array<struct viovec, item_count> v;
  for (int i = 0; i < item_count; ++i) {
    viov2file(&v[i], &files[i], 0, sizeof(write_data[i]),
              (char *)write_data[i]);
  }
  EXPECT_OK(vec_write(v.data(), item_count, true));
  EXPECT_OK(vec_close(files, item_count));
}

template <size_t item_count, size_t max_item_size>
void execute_write(
    const std::vector<std::string> &paths,
    const std::array<char[max_item_size], item_count> &write_data,
    const size_t iteration_count) {
  std::array<struct viovec, item_count> v;
  for (size_t t = 0; t < iteration_count; t++) {
    for (size_t i = 0; i < item_count; ++i) {
      viov2path(&v[i], paths[i].c_str(), 0, sizeof(write_data[i]),
                (char *)write_data[i]);
    }
    EXPECT_OK(vec_write(v.data(), item_count, true));
  }
}

template <size_t item_count, size_t max_item_size>
void execute_read(
    const std::vector<std::string> &paths,
    const std::vector<std::array<char[max_item_size], item_count> > &write_sets,
    const bool &writers_finished) {
  std::array<struct viovec, item_count> v;
  std::array<char[max_item_size], item_count> buffer{0};

  const size_t buflen = item_count * max_item_size;

  while (!writers_finished) {
    std::vector<std::string> read_set;
    for (size_t i = 0; i < item_count; ++i) {
      viov2path(&v[i], paths[i].c_str(), 0, max_item_size, buffer[i]);
    }
    EXPECT_OK(vec_read(v.data(), item_count, true));

    bool is_expected = false;
    for (const auto &write_set : write_sets) {
      is_expected |= (memcmp(buffer.data(), write_set.data(), buflen) == 0);
    }

    if (!is_expected) {
      for (size_t i = 0; i < item_count; i++) {
        std::cout << buffer[i] << " ";
      }
      std::cout << "\n";
      FAIL();
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}
TYPED_TEST_P(TcLockTest, SerializabilityRW) {
  const std::string dir("serializable-rw");
  std::srand(std::time(0));

  /* max size of each write */
  constexpr size_t max_write_size = 40;

  /* number of files to be written, should be same as number of element in
   * each write set */
  constexpr size_t num_writes = 3;

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_writes; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  EXPECT_EQ(paths.size(), num_writes);

  /* each thread performs a write with values in the set */
  const std::vector<std::array<char[max_write_size], num_writes> > write_sets{
      std::array<char[max_write_size], num_writes>{"apple", "banana", "mango"},
      std::array<char[max_write_size], num_writes>{"new york", "california",
                                                   "texas"}};
  /* reader and writer threads */
  std::vector<std::thread> writer_threads, reader_threads;

  /* create base dir */
  if (!FLAGS_skip_test_setup) {
    EXPECT_TRUE(tc::sca_mkdir(dir, 0777));
  }
  /* create files in paths[] and write from set1 */
  execute_create(paths, write_sets[0]);
  bool writers_finished = false;
  /* start a thread performing VWrite with values in each write set */
  for (size_t i = 0; i < FLAGS_nwriter_threads; i++) {
    const auto &write_set = write_sets[std::rand() % write_sets.size()];
    writer_threads.emplace_back(execute_write<num_writes, max_write_size>,
                                std::ref(paths), std::ref(write_set),
                                FLAGS_nwriter_iterations);
  }

  /* read from files */
  for (size_t i = 0; i < FLAGS_nreader_threads; i++) {
    reader_threads.emplace_back(execute_read<num_writes, max_write_size>,
                                std::ref(paths), std::ref(write_sets),
                                std::ref(writers_finished));
  }

  /* wait for threads */
  for (auto &thread : writer_threads) {
    thread.join();
  }
  writers_finished = true;
  for (auto &thread : reader_threads) {
    thread.join();
  }

  if (!FLAGS_skip_test_setup) {
    EXPECT_OK(tc::sca_unlink_recursive(dir));
  }
}

TYPED_TEST_P(TcLockTest, SerializabilityFileCR) {
  const std::string dir("serializable-file-cr");

  /* max size of each write */
  constexpr size_t max_write_size = 40;

  /* number of files to be written, should be same as number of element in
   * each write set */
  constexpr size_t num_writes = 3;

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_writes; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  EXPECT_EQ(paths.size(), num_writes);

  /* each thread performs a write with values in the set */
  std::array<char[max_write_size], num_writes> write_set{"apple", "banana",
                                                         "mango"};
  /* creator + remover and open threads */
  std::thread create_remove_thread;
  std::vector<std::thread> open_threads;

  /* create base dir */
  (tc::sca_mkdir(dir, 0777) || fs::exists(fs::path(dir)));
  bool writers_finished = false;

  /* create files in paths[] and write from set1 */
  create_remove_thread = std::thread(
      execute_create_remove_file<num_writes, max_write_size>, std::ref(paths),
      std::ref(write_set), FLAGS_nwriter_iterations);

  /* use posix api's to verify either all files exist or none */
  for (size_t i = 0; i < FLAGS_nreader_threads; i++) {
    open_threads.emplace_back(execute_posix_path_exists, std::ref(paths),
                              std::ref(this->nfs_base),
                              std::ref(writers_finished));
  }

  /* wait for threads */
  create_remove_thread.join();
  writers_finished = true;
  for (auto &thread : open_threads) {
    thread.join();
  }

  EXPECT_OK(tc::sca_unlink_recursive(dir));
}
TYPED_TEST_P(TcLockTest, SerializabilityDirCR) {
  const std::string dir("serializable-dir-cr");

  /* number of directories */
  constexpr size_t num_directories = 3;

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_directories; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  /* creator + remover and open threads */
  std::thread create_remove_thread;
  std::vector<std::thread> open_threads;

  /* create base dir */
  (tc::sca_mkdir(dir, 0777) || fs::exists(fs::path(dir)));
  bool writers_finished = false;

  /* create files in paths[] and write from set1 */
  create_remove_thread = std::thread(execute_create_remove_dir, std::ref(paths),
                                     FLAGS_nwriter_iterations);

  /* use posix api's to verify either all files exist or none */
  for (size_t i = 0; i < FLAGS_nreader_threads; i++) {
    open_threads.emplace_back(execute_posix_path_exists, std::ref(paths),
                              std::ref(this->nfs_base),
                              std::ref(writers_finished));
  }

  /* wait for threads */
  create_remove_thread.join();
  writers_finished = true;
  for (auto &thread : open_threads) {
    thread.join();
  }

  EXPECT_OK(tc::sca_unlink_recursive(dir));
}

TYPED_TEST_P(TcLockTest, MultipleWritesOnSameFile) {
  const std::string dir("multiple-writes-on-same-file");
  std::srand(std::time(0));
  (tc::sca_mkdir(dir, 0777) || fs::exists(fs::path(dir)));

  /* max size of each write */
  constexpr size_t max_write_size = 256;

  /* number of files to be written, should be same as number of element in
   * each write set */
  constexpr size_t num_writes = 5;
  constexpr size_t total_data_size = 1024;
  char *data = getRandomBytes(total_data_size);

  std::vector<std::string> paths;
  for (size_t i = 0; i < num_writes; i++) {
    fs::path p = dir;
    p = p / std::to_string(i);
    paths.emplace_back(p.string());
  }

  std::array<size_t, num_writes> offsets{0};
  std::vector<struct viovec> v;
  size_t completed_files = 0;
  /* create the files */
  {
    vfile *files = tc::vec_open_simple(paths, O_CREAT | O_RDWR, 0666);
    EXPECT_NOTNULL(files);
    vec_close(files, num_writes);
  }

  /* build vwrite compound */
  while (completed_files < num_writes) {
    for (size_t i = 0; i < num_writes; ++i) {
      if (offsets[i] < total_data_size) {
        /* random write size */
        size_t write_size = std::min((std::rand() % max_write_size) + 1,
                                     total_data_size - offsets[i]);
        struct viovec iovec;
        viov2path(&iovec, paths[i].c_str(), offsets[i], write_size,
                  &data[offsets[i]]);
        offsets[i] += write_size;

        if (offsets[i] == total_data_size) {
          completed_files++;
        }
        v.emplace_back(iovec);
      }
    }
  }

  EXPECT_OK(vec_write(v.data(), v.size(), true));

  /* all the files should have the same data */
  std::array<char[total_data_size], num_writes> buffer{0};

  {
    std::array<struct viovec, num_writes> v;
    for (size_t i = 0; i < num_writes; ++i) {
      viov2path(&v[i], paths[i].c_str(), 0, total_data_size, buffer[i]);
    }
    EXPECT_OK(vec_read(v.data(), num_writes, true));
  }
  for (size_t i = 0; i < num_writes; ++i) {
    EXPECT_EQ(memcmp(buffer[i], data, total_data_size), 0);
  }
  EXPECT_OK(tc::sca_unlink_recursive(dir));
}

REGISTER_TYPED_TEST_CASE_P(TcLockTest, SerializabilityRW, SerializabilityFileCR,
                           SerializabilityDirCR, MultipleWritesOnSameFile);

typedef ::testing::Types<TcNFS4Impl> TcLockImpls;
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  gflags::SetUsageMessage(
      "Note: Flags are only used for multi-threaded "
      "Serializability tests");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  INSTANTIATE_TYPED_TEST_CASE_P(TC, TcLockTest, TcLockImpls);

  return RUN_ALL_TESTS();
}
