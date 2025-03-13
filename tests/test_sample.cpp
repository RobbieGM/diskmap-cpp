#include "../diskmap.h"
#include <assert.h>
#include <cstdio>
#include <string>

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap map("test.dm");
  char value[3500];
  auto tx = map.begin_rw_transaction();
  for (int i = 0; i < 600; i++) {
    // NOLINTNEXTLINE(readability-redundant-string-cstr)
    whl::string key = std::to_string(i).c_str();
    tx.write(key, value, sizeof(value));
  }
  tx.commit();

  {
    auto tx = map.begin_ro_transaction();
    printf("Debug dump:\n");
    tx.debug_dump();
    printf("Random sample:\n");
    srand(123456789);
    auto sample = tx.sample(10);
    for (size_t i = 0; i < sample.size(); i++) {
      printf("%s\n", sample[i].key.c_str());
    }
  }
  return 0;
}