#pragma once

#include <cstddef>
#include <string>

namespace diskmap {

size_t fnv_hash(const std::string &key) {
  size_t hash = 14695981039346656037UL;
  size_t prime = 1099511628211UL;
  for (size_t i = 0; i < key.size(); i++) {
    hash *= prime;
    hash ^= static_cast<size_t>(key[i]);
  }
  return hash;
}

} // namespace diskmap