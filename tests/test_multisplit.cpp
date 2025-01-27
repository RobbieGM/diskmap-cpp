#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  // Page can hold 4096 - 11 = 4085 >= 3000 + 500 + 500
  const char b4000[4000]{};
  const char b500[500]{};
  whl::string d0 = "dz";   // = [342, ...]
  whl::string d1 = "uJi";  // = [342, 413, ...]
  whl::string d2 = "qvY";  // = [342, 413, 269, ...]
  whl::string d3 = "aAPd"; // = [342, 413, 269, 107, ...]

  {
    // Incremental multi split
    remove("test.dm");
    diskmap::DiskMap map("test.dm");
    map.write(d3, b4000, 4000);
    map.write(d0, b500, 500); // Split at root->342
    map.write(d1, b500, 500); // Split at root->342->413
    map.write(d2, b500, 500); // Split at root->342->413->269

    map.debug_dump();
  }

  {
    // Sudden multi split
    remove("test.dm");
    diskmap::DiskMap map("test.dm");
    map.write(d0, b500, 500);
    map.write(d1, b500, 500);
    map.write(d2, b500, 500);
    map.write(d3, b4000, 4000); // Cascading split

    map.debug_dump();
  }
  return 0;
}