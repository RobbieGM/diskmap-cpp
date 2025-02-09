#include "replacer.h"

namespace diskmap {

LRUReplacer::LRUReplacer() {
  head = nullptr;
  tail = nullptr;
  size = 0;
}

LRUReplacer::~LRUReplacer() {
  while (head != nullptr) {
    Node *temp = head;
    head = head->next;
    delete temp;
  }
}

void LRUReplacer::insert(int val) {
  if (!map.contains(val)) {
    Node *node = new Node(val);
    map[val] = node;
    if (head == nullptr) {
      head = node;
      tail = node;
    } else {
      node->next = head;
      head->prev = node;
      head = node;
    }
    size++;
    return;
  }
  // val already exists, move to head
  Node *node = map[val];
  if (node == head) {
    return;
  }
  if (node == tail) {
    tail = node->prev;
    tail->next = nullptr;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
  }
  node->next = head;
  head->prev = node;
  head = node;
}

bool LRUReplacer::remove(int val) {
  Node **result = map.find(val);
  if (result == nullptr) {
    return false;
  }
  Node *node = *result;
  if (node == head && node == tail) {
    head = nullptr;
    tail = nullptr;
  } else if (node == head) {
    head = node->next;
    head->prev = nullptr;
  } else if (node == tail) {
    tail = node->prev;
    tail->next = nullptr;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
  }
  map.erase(val);
  delete node;
  size--;
  return true;
}

int LRUReplacer::evict() {
  if (head == nullptr) {
    return -1;
  }
  int val = tail->val;
  remove(val);
  return val;
}

void DualPriorityReplacer::insert(int val, bool advise_eviction) {
  // Insert into the appropriate list, preferring the important list if both
  // would otherwise contain val after insertion
  if (advise_eviction) {
    if (!important.contains(val)) {
      unimportant.insert(val);
    }
  } else {
    important.insert(val);
    unimportant.remove(val);
  }
}

bool DualPriorityReplacer::remove(int val) {
  // Short-circuit || should work, and val should be found in at most one list
  // Order doesn't matter here
  return unimportant.remove(val) || important.remove(val);
}

int DualPriorityReplacer::evict() {
  int result = unimportant.evict();
  if (result != -1) {
    return result;
  }
  return important.evict();
}

} // namespace diskmap