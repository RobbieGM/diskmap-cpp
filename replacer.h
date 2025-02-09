#pragma once

#include <cstddef>
#include <wheel.h>

namespace diskmap {

// A least-recently-used replacement policy implementation.
class LRUReplacer {
  struct Node {
    Node() {}
    Node(int val) : val(val) {}
    int val;
    Node *next = nullptr;
    Node *prev = nullptr;
  };

  Node *head;
  Node *tail;
  size_t size;
  whl::unordered_map<int, Node *> map;

  bool contains(int val) { return map.contains(val); }
  friend class DualPriorityReplacer;

public:
  LRUReplacer();
  ~LRUReplacer();

  void insert(int val);
  bool remove(int val);
  int evict();
};

// A LRU replacer with two priorities: high-eviction-priority (unimportant) and
// low-eviction-priority (important). Unimportant entries are chosen for
// eviction first.
class DualPriorityReplacer {
  LRUReplacer important;
  LRUReplacer unimportant;

public:
  void insert(int val, bool advise_eviction);
  bool remove(int val);
  int evict();
};

} // namespace diskmap