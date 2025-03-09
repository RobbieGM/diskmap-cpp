#pragma once
#include "exception.h"
#include "page_types.h"
#include "space_manager.h"
#include <cstddef>
#include <cstdint>
#include <wheel.h>
#include <whl_unique_ptr.h>

namespace diskmap {

// class LockManager {
//   whl::unordered_map<int64_t, whl::mutex> locks;
//   whl::mutex internal;

// public:
//   whl::mutex_guard get_guard(int64_t page);
// };

class DiskMap {
  static const char *MAGIC;
  static const int64_t ROOT_PAGE = 1;
  SpaceManager space_manager;
  whl::unique_ptr<BufferPool> buffer_pool;
  whl::unique_ptr<WAL> wal_layer;
  whl::rw_mutex global_lock;
  // LockManager lock_manager;

  // Helper method to find the bucket for a given key in an internal node.
  // Explanation of depth:
  //   depth = 0: root page
  //   depth = 1: first level of the tree
  //   depth = 2: second level of the tree
  //   etc.
  // To find the bucket for a key in a leaf node that is a direct child of the
  // root, you would call get_bucket(key, 1) since the child has a depth of 1.
  // Passing a depth of 0 would be invalid.
  static int get_bucket(whl::string &key, int depth);
  static int get_bucket(uint64_t hash, int depth);

  // Helper method to find entry position in leaf node. Makes the assumption
  // that all entries in leaf nodes occur within the first page of the leaf
  // node, which should be true with proper splitting. Returns pointer to entry
  // start, or nullptr if not found. Returns offset from data section start.
  static int find_entry_in_leaf(const LeafNodeStartPage *leaf,
                                const whl::string &key);

  // Update a value in a leaf node, assuming the usage of the leaf node after
  // updating is less than the maximum capacity for one LeafNodeStartPage
  static void update_value_trivially(WAL::PageHandle<LeafNodeStartPage> &leaf,
                                     int entry_offset, const whl::string &key,
                                     const void *buffer, size_t length);

  void free_leaf(WAL::RWTransaction &t, int64_t page);

  static whl::vector<KVEntry>
  get_entries_in_leaf(const LeafNodeStartPage *leaf);

  // Helper method to create a subtree of nodes starting within an internal
  // node. May only create a leaf node, or may create multiple internal nodes.
  void create_subtree(WAL::RWTransaction &t,
                      WAL::PageHandle<InternalNodePage> &parent,
                      int parent_entry, int parent_depth,
                      const whl::vector<KVEntry> &entries);

  whl::vector<char> read(WAL::ROTransaction &t, whl::string &key, bool &found);
  // whl::vector<char> read_part(WAL::Transaction &t, whl::string &key,
  //                             size_t offset, size_t length,
  //                             bool &found); // TODO
  void write(WAL::RWTransaction &t, whl::string &key, const void *buffer,
             size_t length);
  // void append(WAL::Transaction &t, whl::string &key, void *buffer,
  //             size_t length); // TODO
  bool remove(WAL::RWTransaction &t, whl::string &key);

  void debug_dump(WAL::ROTransaction &t);
  void debug_dump_recursive(WAL::ROTransaction &t, int64_t page,
                            int indent_level, int parent_index);

  friend class Transaction;

public:
  // Initialize a DiskMap, loading from the given path or creating a new file
  explicit DiskMap(const whl::string &path);

  class ROTransaction {
  protected:
    DiskMap *dm;
    whl::unique_ptr<WAL::ROTransaction> tx;
    whl::rw_mutex_guard guard;

  public:
    explicit ROTransaction(DiskMap *dm);
    explicit ROTransaction(DiskMap *dm, whl::unique_ptr<WAL::ROTransaction> tx,
                           whl::rw_mutex_guard::Mode mode);

    whl::vector<char> read(whl::string key, bool &found);
    // whl::vector<char> read_part(whl::string key, size_t offset, size_t
    // length,
    //                             bool &found);
    void debug_dump();
  };

  class RWTransaction : public ROTransaction {
    friend class DiskMap;

  public:
    explicit RWTransaction(DiskMap *dm);

    void commit();
    void abort();

    void write(whl::string key, const void *buffer, size_t length);
    // void append(whl::string key, void *buffer, size_t length);
    bool remove(whl::string key);
  };

  RWTransaction begin_rw_transaction();
  ROTransaction begin_ro_transaction();
};

}; // namespace diskmap