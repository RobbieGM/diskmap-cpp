#include "diskmap.h"
#include <iostream>
#include <cassert>

void test_diskmap() {
    try {
        // Create a temporary DiskMap file for testing
        diskmap::DiskMap map("test_diskmap.dat");

        // Test 1: Write entries
        std::string key1 = "key1";
        std::string value1 = "value1";
        map.write(key1, value1.data(), value1.size());
        std::cout << "Test 1 Passed: Write entry" << std::endl;

        // Test 2: Read existing entry
        bool found = false;
        auto read_value1 = map.read(key1, found);
        assert(found);
        assert(std::string(read_value1.begin(), read_value1.end()) == value1);
        std::cout << "Test 2 Passed: Read entry" << std::endl;

        // Test 3: Update existing entry
        std::string value1_updated = "value1_updated";
        map.write(key1, value1_updated.data(), value1_updated.size());
        auto read_value1_updated = map.read(key1, found);
        assert(found);
        assert(std::string(read_value1_updated.begin(), read_value1_updated.end()) == value1_updated);
        std::cout << "Test 3 Passed: Update entry" << std::endl;

        // Test 4: Write and read multiple entries
        std::string key2 = "key2";
        std::string value2 = "value2";
        map.write(key2, value2.data(), value2.size());

        auto read_value2 = map.read(key2, found);
        assert(found);
        assert(std::string(read_value2.begin(), read_value2.end()) == value2);

        std::string key3 = "key3";
        std::string value3 = "value3";
        map.write(key3, value3.data(), value3.size());

        auto read_value3 = map.read(key3, found);
        assert(found);
        assert(std::string(read_value3.begin(), read_value3.end()) == value3);

        std::cout << "Test 4 Passed: Write and read multiple entries" << std::endl;

        // Test 5: Delete entry
        bool removed = map.remove(key2);
        assert(removed);
        auto read_deleted_value = map.read(key2, found);
        assert(!found);
        std::cout << "Test 5 Passed: Delete entry" << std::endl;

        // Test 6: Handle missing keys
        auto read_missing_value = map.read("nonexistent_key", found);
        assert(!found);
        std::cout << "Test 6 Passed: Handle missing keys" << std::endl;

        // Test 7: Resize DiskMap (implicit with allocate_page)
        for (int i = 0; i < 10000; i++) {
            std::string key = "key" + std::to_string(i);
            std::string value = "value" + std::to_string(i);
            map.write(key, value.data(), value.size());
        }

        std::string key9999 = "key9999";
        auto read_value9999 = map.read(key9999, found);
        assert(found);
        assert(std::string(read_value9999.begin(), read_value9999.end()) == "value9999");
        std::cout << "Test 7 Passed: Handle large number of entries" << std::endl;

        // Test 8: Test empty key rejection
        try {
            map.write("", "value", 5);
            assert(false && "Empty key should throw an exception");
        } catch (const diskmap::DiskMapException &e) {
            std::cout << "Test 8 Passed: Handle empty key" << std::endl;
        }

        // Test 9: Debug dump
        map.debug_dump();
        std::cout << "Test 9 Passed: Debug dump" << std::endl;

        // Cleanup test file
        remove("test_diskmap.dat");

    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
    }
}

int main() {
    test_diskmap();
    return 0;
}