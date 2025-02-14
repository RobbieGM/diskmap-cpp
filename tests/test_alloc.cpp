
#include "diskmap.h"
#include <iostream>
#include <cassert>
#include <vector>

constexpr int NUM_ALLOCS = 100000; // Number of pages to allocate

void allocFreeTest()
{
    diskmap::DiskMap db("diskmap_alloc.db");

    // Allocate many pages
    std::vector<whl::string> keys;
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        std::string temp = "alloc_key" + std::to_string(i);
        whl::string key(temp.c_str());
        std::string temp2 = "alloc_value" + std::to_string(i);
        whl::string value(temp2.c_str());
        // value.append(std::to_string(i).c_str());
        db.write(key, value.c_str(), value.size());
        keys.push_back(key);
    }

    std::cout << "Allocation Test: " << NUM_ALLOCS << " keys written\n";

    // Free half of the allocated pages
    for (int i = 0; i < NUM_ALLOCS / 2; i++)
    {
        bool success = db.remove(keys[i]);
        assert(success);
    }

    std::cout << "Free Test: " << NUM_ALLOCS / 2 << " keys removed\n";

    // Verify deleted pages are gone
    bool found;
    for (int i = 0; i < NUM_ALLOCS / 2; i++)
    {
        auto result = db.read(keys[i], found);
        assert(!found); // Should be deleted
    }

    std::cout << "Verification Passed: Deleted keys are gone\n";

    // Allocate more keys to check if freed pages are reused
    std::vector<whl::string> new_keys;
    for (int i = 0; i < NUM_ALLOCS / 2; i++)
    {
        std::string temp = "realloc_key" + std::to_string(i);
        whl::string key(temp.c_str());
        std::string temp2 = "realloc_value" + std::to_string(i);
        whl::string value(temp2.c_str());

        db.write(key, value.c_str(), value.size());
        new_keys.push_back(key);
    }

    std::cout << "Reallocation Test: " << NUM_ALLOCS / 2 << " new keys written\n";

    // Ensure newly written values exist
    for (const auto &key : new_keys)
    {
        auto result = db.read(key, found);
        assert(found);
    }

    std::cout << "Final Verification: Reallocated keys exist\n";
}

int main()
{
    allocFreeTest();
    return 0;
}