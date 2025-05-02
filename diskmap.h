#pragma once
#include "exception.h"
#include "page_types.h"
#include "space_manager.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>

namespace diskmap {

class DiskMap {
  static const char *MAGIC;
  static const int64_t ROOT_PAGE = 1;
  static const size_t MAX_KEY_LENGTH = 2000;
  std::unique_ptr<BufferPool> buffer_pool;
  std::unique_ptr<WAL> wal_layer;
  std::shared_mutex global_lock;
  size_t op_count = 0;

  // Helper method to find the bucket for a given key in an internal node.
  // Explanation of depth:
  //   depth = 0: root page
  //   depth = 1: first level of the tree
  //   depth = 2: second level of the tree
  //   etc.
  // To find the bucket for a key in a leaf node that is a direct child of the
  // root, you would call get_bucket(key, 1) since the child has a depth of 1.
  // Passing a depth of 0 would be invalid.
  static int get_bucket(const std::string &key, int depth);
  static int get_bucket(uint64_t hash, int depth);

  // Helper method to find the correct parent node for a key, and the index of
  // the entry that should point to the leaf node containing the key.
  template <typename Transaction>
  static auto find_parent(Transaction &t, const std::string &key,
                          int &parent_entry, int &parent_depth);

  // Helper method to find entry position in leaf node. Makes the assumption
  // that all entries in leaf nodes occur within the first page of the leaf
  // node, which should be true with proper splitting. Returns pointer to entry
  // start, or nullptr if not found. Returns offset from data section start.
  static int find_entry_in_leaf(const LeafNodeStartPage *leaf,
                                const std::string &key);

  // Update a value in a leaf node, assuming the usage of the leaf node after
  // updating is less than the maximum capacity for one LeafNodeStartPage
  static void update_value_trivially(WAL::PageHandle<LeafNodeStartPage> &leaf,
                                     int entry_offset, const std::string &key,
                                     const void *buffer, size_t length);

  static void free_leaf(WAL::RWTransaction &t, int64_t page);

  static std::vector<KVEntry>
  get_entries_in_leaf(const LeafNodeStartPage *leaf);

  // Helper method to create a subtree of nodes starting within an internal
  // node. May only create a leaf node, or may create multiple internal nodes.
  void create_subtree(WAL::RWTransaction &t,
                      WAL::PageHandle<InternalNodePage> &parent,
                      int parent_entry, int parent_depth,
                      const std::vector<KVEntry> &entries);

  std::vector<char> read(WAL::ROTransaction &t, const std::string &key,
                         bool &found);
  std::vector<char> read_part(WAL::ROTransaction &t, const std::string &key,
                              size_t offset, size_t length, bool &found);
  static size_t read_value_length(WAL::ROTransaction &t, const std::string &key,
                                  bool &found);
  static std::vector<KVEntry> sample(WAL::ROTransaction &t, size_t count);
  void write_at_node(WAL::RWTransaction &t,
                     WAL::PageHandle<InternalNodePage> parent, int parent_entry,
                     int parent_depth, const std::string &key,
                     const void *buffer, size_t length);
  void write(WAL::RWTransaction &t, const std::string &key, const void *buffer,
             size_t length);
  void write_part(WAL::RWTransaction &t, const std::string &key, size_t offset,
                  const void *buffer, size_t length);
  void append(WAL::RWTransaction &t, const std::string &key, void *buffer,
              size_t length);
  bool remove(WAL::RWTransaction &t, const std::string &key);

  void debug_dump(WAL::ROTransaction &t);
  void debug_dump_recursive(WAL::ROTransaction &t, int64_t page,
                            int indent_level, int parent_index);

  // Throws DiskMapException if the entire DiskMap is corrupt in any way
  static void verify_integrity(WAL::ROTransaction &t);
  static void verify_integrity_recursive(WAL::ROTransaction &t, int64_t page,
                                         int parent_index);

  friend class Transaction;

public:
  // Initialize a DiskMap, loading from the given path or creating a new file
  explicit DiskMap(const std::string &path);

  class ROTransaction {
  protected:
    DiskMap *dm;
    std::unique_ptr<WAL::ROTransaction> tx;
    std::shared_lock<std::shared_mutex> guard;

    ROTransaction(DiskMap *dm, bool read_lock);

  public:
    explicit ROTransaction(DiskMap *dm);

    std::vector<char> read(const std::string &key, bool &found);
    std::vector<char> read_part(const std::string &key, size_t offset,
                                size_t length, bool &found);
    size_t read_value_length(const std::string &key, bool &found);
    std::vector<KVEntry> sample(size_t count);
    void debug_dump();
  };

  class RWTransaction : public ROTransaction {
    friend class DiskMap;
    std::unique_lock<std::shared_mutex> guard;

  public:
    explicit RWTransaction(DiskMap *dm);

    void commit();
    void abort();

    void write(const std::string &key, const void *buffer, size_t length);
    void write_part(const std::string &key, size_t offset, const void *buffer,
                    size_t length);
    void append(const std::string &key, void *buffer, size_t length);
    bool remove(const std::string &key);
  };

  RWTransaction begin_rw_transaction();
  ROTransaction begin_ro_transaction();
};

}; // namespace diskmap