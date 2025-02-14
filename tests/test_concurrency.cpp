#include "diskmap.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>



void concurrentInsert(diskmap::DiskMap& db, int thread_id) {
    int start = thread_id * (NUM_ENTRIES / NUM_THREADS);
    int end = start + (NUM_ENTRIES / NUM_THREADS);

    for (int i = start; i < end; i++) {
        std::string temp = "thread_key" + std::to_string(i);
        whl::string key(temp.c_str());
        std::string temp2 = "thread_value" + std::to_string(i);
        whl::string value(temp2.c_str());
        db.write(key, value.c_str(), value.size());
    }
}

void concurrentRead(diskmap::DiskMap& db, int thread_id) {
    int start = thread_id * (NUM_ENTRIES / NUM_THREADS);
    int end = start + (NUM_ENTRIES / NUM_THREADS);
    bool found;

    for (int i = start; i < end; i++) {
        std::string temp = "thread_key" + std::to_string(i);
        whl::string key(temp.c_str());
        auto result = db.read(key, found);
        assert(found);
    }
}

void concurrencyTest() {
    diskmap::DiskMap db("diskmap_concurrent.db");

    std::vector<std::thread> threads;

    // Insert concurrently
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(concurrentInsert, std::ref(db), i);
    }
    for (auto& t : threads) t.join();

    threads.clear();

    // Read concurrently
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(concurrentRead, std::ref(db), i);
    }
    for (auto& t : threads) t.join();

    std::cout << "Concurrency Test Passed\n";
}

