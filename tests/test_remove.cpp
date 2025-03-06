#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap map("test.dm");
  auto tx = map.begin_rw_transaction();
  const char short_value[8]{};

  tx.write("111", static_cast<const void *>(short_value), sizeof(short_value));
  tx.write("a", static_cast<const void *>(short_value), sizeof(short_value));
  tx.write("aj", static_cast<const void *>(short_value), sizeof(short_value));
  tx.write("444", static_cast<const void *>(short_value), sizeof(short_value));

  assert(tx.remove("a"));
  assert(tx.remove("444"));
  assert(!tx.remove("a"));

  bool found = false;
  tx.read("111", found);
  assert(found);
  tx.read("a", found);
  assert(!found);
  tx.read("aj", found);
  assert(found);
  tx.read("444", found);
  assert(!found);

  tx.debug_dump();
  return 0;
}