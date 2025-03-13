#include "../replacer.h"
#include <cassert>
#include <iostream>

void test_LRUReplacer() {
  std::cout << "Running LRUReplacer tests..." << std::endl;

  diskmap::LRUReplacer lru;

  // Insert elements
  lru.insert(1);
  lru.insert(2);
  lru.insert(3);

  // Ensure LRU ordering (should evict 1 first)
  assert(lru.evict() == 1);
  assert(lru.evict() == 2);
  assert(lru.evict() == 3);
  assert(lru.evict() == -1); // Empty case

  // Reinsertion test
  lru.insert(4);
  lru.insert(5);
  assert(lru.evict() == 4);
  assert(lru.evict() == 5);
  lru.insert(4);
  lru.insert(5);
  assert(lru.evict() == 4);
  assert(lru.evict() == 5);

  // Duplication test
  lru.insert(0);
  lru.insert(1);
  lru.insert(2);
  lru.insert(0);
  assert(lru.evict() == 1);
  assert(lru.evict() == 2);
  assert(lru.evict() == 0);

  std::cout << "LRUReplacer tests passed!" << std::endl;
}

void test_DualPriorityReplacer() {
  std::cout << "Running DualPriorityReplacer tests..." << std::endl;

  diskmap::DualPriorityReplacer dpr;

  // Insert elements into both priority queues
  dpr.insert(1, false); // High priority
  dpr.insert(2, false);
  dpr.insert(3, true); // Low priority
  dpr.insert(4, true);

  // Eviction order should remove from unimportant first
  assert(dpr.evict() == 3);
  assert(dpr.evict() == 4);

  // Remaining should be high priority
  assert(dpr.evict() == 1);
  assert(dpr.evict() == 2);
  assert(dpr.evict() == -1); // Empty case

  // Reinsertion test
  dpr.insert(5, true);
  dpr.insert(6, false);
  assert(dpr.evict() == 5);
  assert(dpr.evict() == 6);

  std::cout << "DualPriorityReplacer tests passed!" << std::endl;
}

int main() {
  test_LRUReplacer();
  test_DualPriorityReplacer();
  std::cout << "All replacer tests passed successfully!" << std::endl;
  return 0;
}