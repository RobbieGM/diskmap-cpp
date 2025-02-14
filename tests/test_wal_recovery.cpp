#include "../wal.h"
#include <assert.h>
#include <cstdio>
#include <fcntl.h>
#include <iostream>

void run_wal_tests() {
  remove("test.dm");
  remove("test.dm.wal");

  {
    int fd = open("test.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
    diskmap::BufferPool pool(fd, 10);
    diskmap::WAL wal(&pool, "test.dm.wal");

    // Transaction 1: Committed transaction
    auto tx1 = wal.begin_transaction();
    auto p0 = tx1.get_page(0);
    p0.write(0, "X", 1);
    tx1.commit();
    assert(pool.get_page(0).data()[0] == 'X');

    // Transaction 2: Uncommitted transaction
    auto tx2 = wal.begin_transaction();
    auto p1_2 = tx2.get_page(1);
    p1_2.write(0, "Y", 1);

    // Transaction 3: Committed transaction that steals transaction 2's page
    auto tx3 = wal.begin_transaction();
    auto p1_3 = tx3.get_page(1);
    p1_3.write(1, "Z", 1);
    tx3.commit();

    // Simulate crash: transaction 2 remains uncommitted
    pool.flush_all();
  }

  // Recover WAL
  int fd = open("test.dm", O_RDWR);
  diskmap::BufferPool pool(fd, 10);
  diskmap::WAL wal(&pool, "test.dm.wal");

  // Verify that only committed transactions persist after recovery
  assert(pool.get_page(0).data()[0] == 'X'); // From committed tx1
  assert(pool.get_page(1).data()[0] == '\0'); // Uncommitted tx2 should be rolled back
  assert(pool.get_page(1).data()[1] == 'Z'); // Committed tx3 should persist

  std::cout << "Basic WAL Recovery Test Passed!" << std::endl;
}

void test_multiple_page_commit() {
  std::cout << "Running Multiple Page Commit Test..." << std::endl;

  remove("test_multiple.dm");
  remove("test_multiple.dm.wal");

  {
    int fd = open("test_multiple.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
    diskmap::BufferPool pool(fd, 10);
    diskmap::WAL wal(&pool, "test_multiple.dm.wal");

    // Start a transaction modifying multiple pages
    auto txn = wal.begin_transaction();
    auto p0 = txn.get_page(0);
    auto p1 = txn.get_page(1);
    auto p2 = txn.get_page(2);

    p0.write(0, "A", 1);
    p1.write(0, "B", 1);
    p2.write(0, "C", 1);

    txn.commit();
    pool.flush_all();
  }

  // Recover WAL
  int fd = open("test_multiple.dm", O_RDWR);
  diskmap::BufferPool pool(fd, 10);
  diskmap::WAL wal(&pool, "test_multiple.dm.wal");

  // Verify that committed data is retained
  assert(pool.get_page(0).data()[0] == 'A');
  assert(pool.get_page(1).data()[0] == 'B');
  assert(pool.get_page(2).data()[0] == 'C');

  std::cout << "Multiple Page Commit Test Passed!" << std::endl;
}

void test_checkpointing() {
  std::cout << "Running Checkpointing Test..." << std::endl;

  remove("test_checkpoint.dm");
  remove("test_checkpoint.dm.wal");

  {
    int fd = open("test_checkpoint.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
    diskmap::BufferPool pool(fd, 10);
    diskmap::WAL wal(&pool, "test_checkpoint.dm.wal");

    // Start a transaction modifying multiple pages
    auto txn = wal.begin_transaction();
    auto p0 = txn.get_page(0);
    auto p1 = txn.get_page(1);

    p0.write(0, "Q", 1);
    p1.write(0, "R", 1);

    txn.commit();
    pool.flush_all();

    // Run a checkpoint
    wal.checkpoint();
  }

  // Recover WAL
  int fd = open("test_checkpoint.dm", O_RDWR);
  diskmap::BufferPool pool(fd, 10);
  diskmap::WAL wal(&pool, "test_checkpoint.dm.wal");

  // Verify that committed data is retained
  assert(pool.get_page(0).data()[0] == 'Q');
  assert(pool.get_page(1).data()[0] == 'R');

  std::cout << "Checkpointing Test Passed!" << std::endl;
}

void test_crash_during_transaction() {
  std::cout << "Running Crash During Transaction Test..." << std::endl;

  remove("test_crash.dm");
  remove("test_crash.dm.wal");

  {
    int fd = open("test_crash.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
    diskmap::BufferPool pool(fd, 10);
    diskmap::WAL wal(&pool, "test_crash.dm.wal");

    // Start a transaction but don't commit
    auto txn = wal.begin_transaction();
    auto p0 = txn.get_page(0);
    auto p1 = txn.get_page(1);

    p0.write(0, "U", 1);
    p1.write(0, "V", 1);

    // Simulated crash (no commit)
    pool.flush_all();
  }

  // Recover WAL
  int fd = open("test_crash.dm", O_RDWR);
  diskmap::BufferPool pool(fd, 10);
  diskmap::WAL wal(&pool, "test_crash.dm.wal");

  // Verify that uncommitted transactions are rolled back
  assert(pool.get_page(0).data()[0] == '\0'); // Should not retain 'U'
  assert(pool.get_page(1).data()[0] == '\0'); // Should not retain 'V'

  std::cout << "Crash During Transaction Test Passed!" << std::endl;
}

void test_wal_performance() {
  std::cout << "Running WAL Performance Test..." << std::endl;

  remove("test_perf.dm");
  remove("test_perf.dm.wal");

  int fd = open("test_perf.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
  diskmap::BufferPool pool(fd, 100);
  diskmap::WAL wal(&pool, "test_perf.dm.wal");

  constexpr int NUM_TRANSACTIONS = 1000;

  for (int i = 0; i < NUM_TRANSACTIONS; i++) {
    auto txn = wal.begin_transaction();
    auto page = txn.get_page(i % 100, false);
    char val = static_cast<char>('A' + (i % 26));
    page.write(0, &val, 1);
    txn.commit();
  }

  std::cout << "WAL Performance Test Passed!" << std::endl;
}

int main() {
  run_wal_tests();
  test_multiple_page_commit();
  test_checkpointing();
  test_crash_during_transaction();
  test_wal_performance();
  
  std::cout << "All WAL tests passed successfully!" << std::endl;
  return 0;
}