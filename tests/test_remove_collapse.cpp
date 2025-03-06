
#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap map("test.dm");
  auto tx = map.begin_rw_transaction();
  const char long_value[3000]{};

  whl::string k0 = "a";
  whl::string k1 = "ad";
  tx.write(k0, static_cast<const void *>(long_value), sizeof(long_value));
  tx.write(k1, static_cast<const void *>(long_value), sizeof(long_value));

  // Should show internal node with two leaves
  printf("Before remove:\n");
  tx.debug_dump();

  assert(tx.remove(k0));

  // Should show leaf node with one entry as direct child of root
  printf("\nAfter remove:\n");
  tx.debug_dump();

  return 0;
}