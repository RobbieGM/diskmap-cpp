#pragma once

#include <string>

namespace diskmap {

class DiskMapException {
public:
  explicit DiskMapException(const std::string &message) : msg(message) {}
  const char *what() const noexcept { return msg.c_str(); }

private:
  std::string msg;
};

} // namespace diskmap