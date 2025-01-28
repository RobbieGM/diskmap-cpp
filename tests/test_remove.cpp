#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  diskmap::DiskMap map("test.dm");
  const char short_value[8]{};

  map.write("111", static_cast<const void *>(short_value), sizeof(short_value));
  map.write("dz", static_cast<const void *>(short_value), sizeof(short_value));
  map.write("uJi", static_cast<const void *>(short_value), sizeof(short_value));
  map.write("444", static_cast<const void *>(short_value), sizeof(short_value));

  assert(map.remove("dz"));
  assert(map.remove("444"));
  assert(!map.remove("dz"));

  bool found;
  map.read("111", found);
  assert(found);
  map.read("dz", found);
  assert(!found);
  map.read("uJi", found);
  assert(found);
  map.read("444", found);
  assert(!found);

  map.debug_dump();
  return 0;
}