#include "../diskmap.h"
#include <assert.h>
#include <cstdio>
#include <iostream>

void test_basic_operations() {
    remove("test_basic.dm");
    diskmap::DiskMap map("test_basic.dm");
    
    const char data1[100] = { 'A' };
    const char data2[200] = { 'B' };
    const char data3[50]  = { 'C' };
    
    whl::string key1 = "key_one";
    whl::string key2 = "key_two";
    whl::string key3 = "key_three";
    
    // Write and read back
    map.write(key1, data1, 100);
    map.write(key2, data2, 200);
    map.write(key3, data3, 50);
    
    char buffer[200];
    size_t size = 0;
    
    assert(map.read(key1, buffer, size) && size == 100);
    assert(map.read(key2, buffer, size) && size == 200);
    assert(map.read(key3, buffer, size) && size == 50);
    
    std::cout << "test_basic_operations passed!\n";
}

void test_overwrite() {
    remove("test_overwrite.dm");
    diskmap::DiskMap map("test_overwrite.dm");
    
    const char data_old[300] = { 'X' };
    const char data_new[300] = { 'Y' };
    whl::string key = "overwrite_test";
    
    // Initial write
    map.write(key, data_old, 300);
    
    // Overwrite
    map.write(key, data_new, 300);
    
    char buffer[300];
    size_t size = 0;
    assert(map.read(key, buffer, size) && size == 300);
    assert(buffer[0] == 'Y');
    
    std::cout << "test_overwrite passed!\n";
}

void test_large_data() {
    remove("test_large.dm");
    diskmap::DiskMap map("test_large.dm");
    
    const char large_data[4085] = { 'L' };
    whl::string key = "large_key";
    
    map.write(key, large_data, 4085);
    
    char buffer[4085];
    size_t size = 0;
    assert(map.read(key, buffer, size) && size == 4085);
    assert(buffer[0] == 'L');
    
    std::cout << "test_large_data passed!\n";
}

void test_removal() {
    remove("test_remove.dm");
    diskmap::DiskMap map("test_remove.dm");
    
    const char data[150] = { 'D' };
    whl::string key = "delete_key";
    
    map.write(key, data, 150);
    assert(map.remove(key));
    
    char buffer[150];
    size_t size = 0;
    assert(!map.read(key, buffer, size));
    
    std::cout << "test_removal passed!\n";
}

void test_bulk_inserts() {
    remove("test_bulk.dm");
    diskmap::DiskMap map("test_bulk.dm");
    
    for (int i = 0; i < 1000; ++i) {
        whl::string key = "bulk_key_" + std::to_string(i);
        char data[100] = { static_cast<char>('A' + (i % 26)) };
        map.write(key, data, 100);
    }
    
    char buffer[100];
    size_t size = 0;
    whl::string check_key = "bulk_key_500";
    assert(map.read(check_key, buffer, size) && size == 100);
    
    std::cout << "test_bulk_inserts passed!\n";
}

int main() {
    test_basic_operations();
    test_overwrite();
    test_large_data();
    test_removal();
    test_bulk_inserts();
    return 0;
}
