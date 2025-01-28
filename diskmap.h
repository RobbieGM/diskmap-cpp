#pragma once
#include "page_types.h"
#include <cstddef>
#include <cstdint>
#include <wheel.h>

namespace diskmap {

class DiskMapException {
public:
  DiskMapException(const char *message) : message_(message) {}
  const char *what() const noexcept { return message_.c_str(); }

private:
  whl::string message_;
};

class LockManager {
  whl::unordered_map<int64_t, whl::mutex> locks;
  whl::mutex internal;

public:
  whl::mutex_guard get_guard(int64_t page);
};

class DiskMap {
  static const char *MAGIC;
  static const int64_t ROOT_PAGE = 1;
  static const int64_t MMAPPED_PAGES =
      1ULL << 32; // 128 TiB, half of virtual address space
  size_t file_pages;
  int fd;
  void *mapped;
  LockManager lock_manager;

  struct HeldNode {
    int64_t page;
    enum { INTERNAL_NODE, LEAF_NODE } type;
  };

  void extend_file(int64_t num_pages);
  void *get_addr(int64_t page, int offset);
  MetaPage *meta() { return static_cast<MetaPage *>(get_addr(0, 0)); }
  FPLPage *fpl(int64_t page) {
    return static_cast<FPLPage *>(get_addr(page, 0));
  }
  InternalNodePage *internal_node(int64_t page) {
    return static_cast<InternalNodePage *>(get_addr(page, 0));
  }
  LeafNodePage *leaf_node(int64_t page) {
    return static_cast<LeafNodePage *>(get_addr(page, 0));
  }

  // Allocate a new page. May contain undefined data
  int64_t allocate_page(order_t order);
  void free_page(int64_t page, order_t order);

  int64_t get_bucket(uint64_t hash, int64_t depth);

  // Helper method to find entry position in leaf node
  // Returns pointer to entry start, or nullptr if not found
  char *find_entry_in_leaf(LeafNodePage *leaf, const whl::string &key);

  whl::vector<KVEntry> get_entries_in_leaf(LeafNodePage *leaf);

  void create_subtree(int64_t *parent_entry, int depth,
                      const whl::vector<KVEntry> &entries);

  template <typename T> friend class PageRef;

public:
  // Initialize a DiskMap, loading from the given path or creating a new file
  // to back the map at that path
  DiskMap(whl::string path);
  ~DiskMap();

  whl::vector<char> read(whl::string key, bool &found);
  void write(whl::string key, const void *buffer, int64_t length);
  void append(whl::string key, void *buffer, int64_t length);
  bool remove(whl::string key);

  void debug_dump();
  void debug_dump_recursive(int64_t page, int indent_level, int parent_index);
};

}; // namespace diskmap