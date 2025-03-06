#pragma once

#include "wheel.h"

namespace diskmap {

class DiskMapException {
public:
  explicit DiskMapException(const char *message) : message_(message) {}
  const char *what() const noexcept { return message_.c_str(); }

private:
  whl::string message_;
};

} // namespace diskmap