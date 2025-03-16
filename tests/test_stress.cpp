#include "../diskmap.h"
#include <assert.h>
#include <cstdio>
#include <string>

void add_600() {
  diskmap::DiskMap map("test.dm");
  const char value[3500]{};
  for (int a = 0; a < 60; a++) {
    auto tx = map.begin_rw_transaction();
    for (int b = 0; b < 100; b++) {
      int i = (a * 100) + b;
      // NOLINTNEXTLINE(readability-redundant-string-cstr)
      whl::string key = std::to_string(i).c_str();
      tx.write(key, value, sizeof(value));
    }
    tx.commit();
  }

  printf("After writing 600 entries:\n");
  {
    auto tx = map.begin_ro_transaction();
    tx.debug_dump();
  }
}

void remove_600() {
  diskmap::DiskMap map("test.dm");
  for (int a = 0; a < 60; a++) {
    auto tx = map.begin_rw_transaction();
    for (int b = 0; b < 100; b++) {
      int i = (a * 100) + b;
      // NOLINTNEXTLINE(readability-redundant-string-cstr)
      whl::string key = std::to_string(i).c_str();
      if (!tx.remove(key)) {
        printf("Warning: %d missing\n", i);
      }
    }
    tx.commit();
  }
  printf("After deleting all entries:\n");
  {
    auto tx = map.begin_ro_transaction();
    tx.debug_dump();
  }
}

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  add_600();
  remove_600();
  add_600();
  return 0;
}