#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  diskmap::DiskMap map("test.dm");
  // 4 byte key, 8 byte value length
  const char short_value[8]{};
  const char long_value[1024 - 4 - 8]{};
  map.write("111", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("222", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("333", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("444", static_cast<const void *>(long_value), sizeof(long_value));
  // Rewrite all values to short value
  map.write("111", static_cast<const void *>(short_value), sizeof(short_value));
  map.write("222", static_cast<const void *>(short_value), sizeof(short_value));
  map.write("333", static_cast<const void *>(short_value), sizeof(short_value));
  map.write("444", static_cast<const void *>(short_value), sizeof(short_value));
  // Rewrite all values to long value and add 111-
  map.write("111-", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("222", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("333", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("444", static_cast<const void *>(long_value), sizeof(long_value));

  bool found;
  map.read("222", found);
  assert(found);
  map.read("111-", found);
  assert(found);
  map.read("555", found);
  assert(!found);

  map.debug_dump();
  return 0;
}