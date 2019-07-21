/**
 * tc_api_wrapper.hpp - The C++ wrapper of TC C APIs
 */

#include "tc_api.h"
#include <algorithm>
#include <vector>
#include <string>

namespace tc {

std::vector<const char *> to_char_array(const std::vector<std::string> &strlist);
bool vec_mkdir_simple(const std::vector<std::string> &paths, const int mode);
bool sca_mkdir(const std::string &path, const int mode);
vres sca_unlink_recursive(const std::string &path);
vres sca_unlink(const std::string &path);
vres vec_unlink(const std::vector<std::string> &paths, int first_n = 0);
bool sca_exists(const std::string &path);
vfile *vec_open_simple(const std::vector<std::string> &paths, int flag, mode_t mode);
vfile *vec_open(const std::vector<std::string> &paths, std::vector<int> &flags, std::vector<mode_t> &modes);
vres vec_hardlink(const std::vector<std::string> &src, const std::vector<std::string> &dest, bool is_trans);
vres vec_symlink(const std::vector<std::string> &src, const std::vector<std::string> &dest, bool is_trans);

}