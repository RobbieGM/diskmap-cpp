
#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  diskmap::DiskMap map("test.dm");
  const char long_value[3000]{};

  // dz = [342, 386, 340]
  // uJi = [342, 413, 272]
  map.write("dz", static_cast<const void *>(long_value), sizeof(long_value));
  map.write("uJi", static_cast<const void *>(long_value), sizeof(long_value));

  assert(map.remove("dz"));

  // Should show leaf node with one entry as direct child of root
  map.debug_dump();

  return 0;
}