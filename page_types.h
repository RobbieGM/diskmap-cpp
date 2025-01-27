#pragma once
#include <cstdint>
#include <cstring>
#include <wheel.h>

namespace diskmap {

using order_t = unsigned char;

const int PAGE_SIZE = 4096;
static const order_t MAX_ORDER = 38;

static const int FPL_PAGE_CAPACITY = PAGE_SIZE / 8 - 2;

struct MetaPage {
  char magic[8];
  int64_t kv_entry_count;
  uint64_t next_free_page;
  int64_t last_fpl_page[MAX_ORDER + 1];
  int last_fpl_page_entries[MAX_ORDER + 1];
};

struct FPLPage {
  int64_t previous;
  int64_t next;
  int64_t entries[FPL_PAGE_CAPACITY];
  void init() {
    previous = -1;
    next = -1;
    memset(entries, -1, sizeof(entries));
  }
};

struct InternalNodePage {
  // Each entry points to either an internal node, or a leaf node, or -1.
  // The MSB is set if the entry is an internal node
  static const int BRANCHING_FACTOR = PAGE_SIZE / 8;
  int64_t entries[BRANCHING_FACTOR];
  void init() { memset(entries, -1, sizeof(entries)); }
};

struct LeafNodePage {
  // May actually be multiple pages
  static const size_t PAGE_HEADER_SIZE = 11;
  uint64_t usage; // Tracks total bytes used in the data section
  uint16_t entry_count;
  order_t order;
  /*
   * Rest of the page(s) filled with data entries
   * Data format in leaf node data section:
   * Repeated entries of:
   * 1. Key: Null-terminated string
   * 2. Value length: 8 bytes (uint64_t)
   * 3. Value data: <length> bytes
   */
  char data;
  void init(order_t o) {
    order = o;
    usage = 0;
    entry_count = 0;
    memset(&data, 0, capacity(o));
  }
  static size_t capacity(order_t o) {
    // Subtract usage of page header
    return (PAGE_SIZE << o) - PAGE_HEADER_SIZE;
  }
  size_t capacity() { return capacity(order); }
  void *end() {
    return static_cast<char *>(static_cast<void *>(this)) +
           (PAGE_SIZE << order);
  }
};

struct KVEntry {
  whl::string key;
  whl::vector<char> value;
};

}; // namespace diskmap