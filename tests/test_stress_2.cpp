#include "../diskmap.h"
#include <cassert>

void highVolumeReadWriteTest() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap db("test.dm");
  auto tx = db.begin_rw_transaction();

  constexpr int NUM_ENTRIES = 50000;

  // Insert many entries
  for (int i = 0; i < NUM_ENTRIES; i++) {
    std::string key = "entry_" + std::to_string(i);
    std::string value = "value_" + std::to_string(i);
    tx.write(key, value.c_str(), value.size());
  }

  // Random Reads
  for (int i = 0; i < 100000; i++) {
    int randIndex = rand() % NUM_ENTRIES;
    std::string key = "entry_" + std::to_string(randIndex);
    bool found{};
    auto result = tx.read(key, found);
    assert(found);
  }

  tx.commit();

  printf("High Volume Read/Write Test Passed\n");
}

void massiveDeletionTest() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap db("test.dm");
  auto tx = db.begin_rw_transaction();

  constexpr int NUM_ENTRIES = 50000;

  // Insert values
  for (int i = 0; i < NUM_ENTRIES; i++) {
    std::string key = "delete_key" + std::to_string(i);
    std::string value = "delete_value" + std::to_string(i);
    tx.write(key, value.c_str(), value.size());
  }

  // Delete half of the keys
  for (int i = 0; i < NUM_ENTRIES / 2; i++) {
    std::string key = "delete_key" + std::to_string(i);
    bool success = tx.remove(key);
    assert(success);
  }

  // Verify deletion
  bool found{};
  for (int i = 0; i < NUM_ENTRIES / 2; i++) {
    std::string key = "delete_key" + std::to_string(i);
    auto result = tx.read(key, found);
    assert(!found); // Should be deleted
  }

  tx.commit();

  printf("Massive Deletion Test Passed\n");
}

int main() {
  massiveDeletionTest();
  highVolumeReadWriteTest();
  return 0;
}