#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap map("test.dm");
  // 4 byte key, 8 byte value length
  const char short_value[] = "short";
  const char long_value[] = "long value";
  // a, ad, hK hash to [446, ...]
  // b hashes to something else
  auto tx = map.begin_transaction();
  tx.write("a", static_cast<const void *>(long_value), sizeof(long_value));
  tx.write("ad", static_cast<const void *>(long_value), sizeof(long_value));
  tx.write("b", static_cast<const void *>(long_value), sizeof(long_value));
  // Rewrite all values to short value
  tx.write("a", static_cast<const void *>(short_value), sizeof(short_value));
  tx.write("ad", static_cast<const void *>(short_value), sizeof(short_value));
  tx.write("b", static_cast<const void *>(short_value), sizeof(short_value));
  // Add hK, rewrite all values to long value
  tx.write("hK", static_cast<const void *>(long_value), sizeof(long_value));
  tx.write("a", static_cast<const void *>(long_value), sizeof(long_value));
  tx.write("ad", static_cast<const void *>(long_value), sizeof(long_value));
  tx.write("b", static_cast<const void *>(long_value), sizeof(long_value));

  bool found;
  tx.read("a", found);
  assert(found);
  tx.read("ad", found);
  assert(found);
  tx.read("b", found);
  assert(found);

  tx.debug_dump();
  tx.commit();
  return 0;
}