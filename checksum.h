#pragma once

#include <cstddef>
namespace diskmap {
class Checksum {
  unsigned long state{1099511628211UL};

public:
  Checksum() = default;
  void digest(const char *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      state += data[i];
      state += (state << 10);
      state ^= (state >> 6);
    }
  }
  unsigned long value() const { return state; }
};
} // namespace diskmap