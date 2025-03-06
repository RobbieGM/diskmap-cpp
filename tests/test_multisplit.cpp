#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  // Page can hold 4096 - 11 = 4085 >= 3000 + 500 + 500
  const char b4000[4000]{};
  const char b500[500]{};
  whl::string k0 = "ad";    // = [446, ...]
  whl::string k1 = "ajos";  // = [446, 219, ...]
  whl::string k2 = "dmUpw"; // = [446, 219, 384, ...]
  whl::string k3 = "a";     // = [446, 219, 384, 400, ...]

  {
    // Incremental multi split
    remove("test.dm");
    remove("test.dm.wal");
    diskmap::DiskMap map("test.dm");
    auto tx = map.begin_transaction();
    tx.write(k3, b4000, 4000);
    tx.write(k0, b500, 500); // Split at root->342
    tx.write(k1, b500, 500); // Split at root->342->413
    tx.write(k2, b500, 500); // Split at root->342->413->269

    tx.debug_dump();
    tx.abort();
  }

  {
    // Sudden multi split
    remove("test.dm");
    remove("test.dm.wal");
    diskmap::DiskMap map("test.dm");
    auto tx = map.begin_transaction();
    tx.write(k0, b500, 500);
    tx.write(k1, b500, 500);
    tx.write(k2, b500, 500);
    tx.write(k3, b4000, 4000); // Cascading split

    tx.debug_dump();
    tx.abort();
  }
  return 0;
}