#include "diskmap.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>



void bulkInsertionTest() {
    diskmap::DiskMap db("diskmap_stress.db");

    constexpr int NUM_ENTRIES = 1000000;
    for (int i = 0; i < NUM_ENTRIES; i++) {
        std::string temp = "alloc_key" + std::to_string(i);
        whl::string key(temp.c_str());
        whl::string value = whl::string("value") + whl::string(std::to_string(i));
        db.write(key, value.c_str(), value.size());
    }

    // Verify some values
    bool found;
    for (int i = 0; i < 1000; i++) {
        whl::string key = whl::string("key") + whl::string(std::to_string(i));
        auto result = db.read(key, found);
        assert(found);
        assert(whl::string(result.data_ptr(), result.size()) == (whl::string("value") + whl::string(std::to_string(i))));
    }

    std::cout << "Bulk Insertion Test Passed" << std::endl;
}

void highVolumeReadWriteTest() {
    diskmap::DiskMap db("diskmap_stress.db");

    constexpr int NUM_ENTRIES = 500000;
    
    // Insert many entries
    for (int i = 0; i < NUM_ENTRIES; i++) {
        whl::string key = whl::string("entry_") + whl::string(std::to_string(i));
        whl::string value = whl::string("val_") + whl::string(std::to_string(i));
        db.write(key, value.c_str(), value.size());
    }

    // Random Reads
    for (int i = 0; i < 100000; i++) {
        int randIndex = rand() % NUM_ENTRIES;
        whl::string key = whl::string("entry_") + whl::string(std::to_string(randIndex));
        bool found;
        auto result = db.read(key, found);
        assert(found);
    }

    std::cout << "High Volume Read/Write Test Passed" << std::endl;
}

void massiveDeletionTest() {
    diskmap::DiskMap db("diskmap_delete.db");

    constexpr int NUM_ENTRIES = 500000;
    
    // Insert values
    for (int i = 0; i < NUM_ENTRIES; i++) {
        whl::string key = whl::string("delete_key") + whl::string(std::to_string(i));
        whl::string value = whl::string("delete_value") + whl::string(std::to_string(i));
        db.write(key, value.c_str(), value.size());
    }

    // Delete half of the keys
    for (int i = 0; i < NUM_ENTRIES / 2; i++) {
        whl::string key = whl::string("delete_key") + whl::string(std::to_string(i));
        bool success = db.remove(key);
        assert(success);
    }

    // Verify deletion
    bool found;
    for (int i = 0; i < NUM_ENTRIES / 2; i++) {
        whl::string key = whl::string("delete_key") + whl::string(std::to_string(i));
        auto result = db.read(key, found);
        assert(!found); // Should be deleted
    }

    std::cout << "Massive Deletion Test Passed" << std::endl;
}

int main() {
    massiveDeletionTest();
    bulkInsertionTest();
    highVolumeReadWriteTest();
    return 0;
}