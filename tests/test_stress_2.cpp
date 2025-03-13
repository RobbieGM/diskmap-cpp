#include "../diskmap.h"
#include <cassert>
#include <iostream>

void highVolumeReadWriteTest() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap db("test.dm");
  auto tx = db.begin_rw_transaction();

  constexpr int NUM_ENTRIES = 50000;

  // Insert many entries
  for (int i = 0; i < NUM_ENTRIES; i++) {
    std::string temp = "entry_" + std::to_string(i);
    whl::string key(temp.c_str());
    std::string temp2 = "value_" + std::to_string(i);
    whl::string value(temp2.c_str());
    tx.write(key, value.c_str(), value.size());
  }

  // Random Reads
  for (int i = 0; i < 100000; i++) {
    int randIndex = rand() % NUM_ENTRIES;
    std::string temp = "entry_" + std::to_string(randIndex); // Use randIndex
    whl::string key(temp.c_str());
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
    std::string temp = "delete_key" + std::to_string(i);
    whl::string key(temp.c_str());
    std::string temp2 = "delete_value" + std::to_string(i);
    whl::string value(temp2.c_str());
    tx.write(key, value.c_str(), value.size());
  }

  // Delete half of the keys
  for (int i = 0; i < NUM_ENTRIES / 2; i++) {
    std::string temp = "delete_key" + std::to_string(i);
    whl::string key(temp.c_str());
    bool success = tx.remove(key);
    assert(success);
  }

  // Verify deletion
  bool found{};
  for (int i = 0; i < NUM_ENTRIES / 2; i++) {
    std::string temp = "delete_key" + std::to_string(i);
    whl::string key(temp.c_str());
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