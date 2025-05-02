#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace diskmap {

using order_t = unsigned char;

const int PAGE_SIZE = 4096;
static const order_t MAX_ORDER = 38;

static const int FPL_PAGE_CAPACITY = (PAGE_SIZE / 8) - 2;

struct MetaPage {
  char magic[8];
  int64_t kv_entry_count;
  int64_t next_free_page;
  int64_t last_fpl_page[MAX_ORDER + 1];
  int last_fpl_page_entries[MAX_ORDER + 1];
} __attribute__((packed));

struct FPLPage {
  int64_t previous;
  int64_t next;
  int64_t entries[FPL_PAGE_CAPACITY];
} __attribute__((packed));

struct InternalNodePage {
  // Each entry points to either an internal node, or a leaf node, or -1.
  // The MSB is set if the entry is an internal node
  static const int BRANCHING_FACTOR = PAGE_SIZE / 8;
  int64_t entries[BRANCHING_FACTOR];
} __attribute__((packed));

// Leaf node format:
// (LeafNodeStartPage) ->
// (LeafNodeContinuationPage + LeafNodePurePage) ->
// (LeafNodeContinuationPage + LeafNodePurePage * 3) -> ... ->
// (LeafNodeContinuationPage + LeafNodePurePage * (2^n - 1))
// Where -> denotes linking to the next page,
// + denotes pages accessed contiguously.
// Only LeafNodeStartPage and LeafNodeContinuationPage contain pointers to the
// start of the next contiguous region of pages.

struct LeafNodeStartPage {
  // The first page of a leaf node. Contains page header and data.
  int64_t next;
  uint64_t usage; // Tracks total bytes used in the data section of this page
                  // and all linked regions
  uint16_t entry_count;
  /*
   * Data format in leaf node data section:
   * Repeated contiguous entries of:
   * 1. Key: Null-terminated string
   * 2. Value length: 8 bytes (uint64_t)
   * 3. Value data: <length> bytes
   */
  char data[];
  constexpr static size_t capacity() {
    // Subtract usage of page header
    return PAGE_SIZE - offsetof(LeafNodeStartPage, data);
  }
} __attribute__((packed));

struct LeafNodeContinuationPage {
  int64_t next;
  char data[];
} __attribute__((packed));

struct LeafNodePurePage {
} __attribute__((packed));

inline const char *end_of(const void *page) {
  return static_cast<const char *>(page) + PAGE_SIZE;
}

struct KVEntry {
  std::string key;
  std::vector<char> value;
  KVEntry() = default;
  KVEntry(const std::string &key, std::vector<char> &&value)
      : key(key), value(std::move(value)) {}
};

}; // namespace diskmap