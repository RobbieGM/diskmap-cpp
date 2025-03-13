#pragma once

#include <cstddef>
#include <cstdint>

namespace diskmap {
class Checksum {
  unsigned long state{1099511628211UL};

public:
  Checksum() = default;
  void digest(const char *data, size_t len) {
    size_t i = 0;
    while (i < (len / 8) * 8) {
      state += *reinterpret_cast<const uint64_t *>(data + i);
      state += (state << 10);
      state ^= (state >> 6);
      i += 8;
    }
    while (i < len) {
      state += data[i];
      state += (state << 10);
      state ^= (state >> 6);
      i += 1;
    }
  }
  unsigned long value() const { return state; }
};
} // namespace diskmap