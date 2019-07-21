#include "tc_api_wrapper.hpp"


std::vector<const char *> tc::to_char_array(const std::vector<std::string> &strlist)
{
    std::vector<const char *> res;
    res.reserve(strlist.size() + 1);
    std::transform(begin(strlist), end(strlist), std::back_inserter(res),
        [](const std::string &s) { return s.c_str(); });
    res.push_back(nullptr);
    return res;
}

bool tc::vec_mkdir_simple(const std::vector<std::string> &paths, const int mode) {
  const int n = paths.size();
  struct vattrs *attrs = new struct vattrs[n];
  if (!attrs) return false;

  for (int i = 0; i < n; ++i) {
    vset_up_creation(&attrs[i], paths[i].c_str(), mode);
  }

  return ::tx_vec_mkdir(attrs, n, true);
}

bool tc::sca_mkdir(const std::string &path, const int mode) {
  struct vattrs attr;
  vset_up_creation(&attr, path.c_str(), mode);

  return ::tx_vec_mkdir(&attr, 1, true);
}

vres tc::sca_unlink_recursive(const std::string &path) {
  const char *cpath = path.c_str();
  return ::vec_unlink_recursive(&cpath, 1);
}

vres tc::sca_unlink(const std::string &path) {
  const char *cpath = path.c_str();
  return ::vec_unlink(&cpath, 1);
}

vres tc::vec_unlink(const std::vector<std::string> &paths, int first_n) {
  auto cpath_list = tc::to_char_array(paths);
  const char **cpaths = cpath_list.data();
  if (first_n < 1) {
    first_n = paths.size();
  }
  return ::vec_unlink(cpaths, first_n);
}

bool tc::sca_exists(const std::string &path) {
  const char *cpath = path.c_str();
  return ::sca_exists(cpath);
}

vfile *tc::vec_open_simple(const std::vector<std::string> &paths, int flag, mode_t mode) {
  auto cpath_list = tc::to_char_array(paths);
  const char **cpaths = cpath_list.data();
  return ::vec_open_simple(cpaths, paths.size(), flag, mode);
}

vfile *tc::vec_open(const std::vector<std::string> &paths, std::vector<int> &flags, std::vector<mode_t> &modes) {
  auto cpath_list = tc::to_char_array(paths);
  const char **cpaths = cpath_list.data();
  return ::vec_open(cpaths, paths.size(), flags.data(), modes.data());
}

vres tc::vec_hardlink(const std::vector<std::string> &src, const std::vector<std::string> &dest, bool is_trans) {
  if (src.size() != dest.size()) {
    vres bad;
    bad.index = 0;
    bad.err_no = EINVAL;
    return bad;
  }

  auto src_list = tc::to_char_array(src);
  const char **c_src = src_list.data();
  auto dst_list = tc::to_char_array(dest);
  const char **c_dst = dst_list.data();

  int n = src.size();
  return ::vec_hardlink(c_src, c_dst, n, is_trans);
}

vres tc::vec_symlink(const std::vector<std::string> &src, const std::vector<std::string> &dest, bool is_trans) {
  if (src.size() != dest.size()) {
    vres bad;
    bad.index = 0;
    bad.err_no = EINVAL;
    return bad;
  }

  auto src_list = tc::to_char_array(src);
  const char **c_src = src_list.data();
  auto dst_list = tc::to_char_array(dest);
  const char **c_dst = dst_list.data();
  int n = src.size();
  return ::vec_symlink(c_src, c_dst, n, is_trans);
}