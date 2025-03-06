#include "../wal.h"
#include <assert.h>
#include <cstdio>
#include <fcntl.h>

struct TestPage {};

int main() {
  remove("test.dm");
  remove("test.dm.wal");

  {
    int fd = open("test.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
    diskmap::BufferPool pool(fd);
    diskmap::WAL wal(&pool, "test.dm.wal");

    // Transaction 1: Committed transaction
    auto tx1 = wal.begin_rw_transaction();
    auto p0 = tx1.get_page<TestPage>(0);
    p0.write(0, "X", 1);
    tx1.commit();
    assert(pool.get_page(0).data()[0] == 'X');

    // Transaction 2: Uncommitted transaction
    auto tx2 = wal.begin_rw_transaction();
    auto p1_2 = tx2.get_page<TestPage>(1);
    p1_2.write(0, "Y", 1);

    // Transaction 3: Committed transaction that steals transaction 2's page
    auto tx3 = wal.begin_rw_transaction();
    auto p1_3 = tx3.get_page<TestPage>(1);
    p1_3.write(1, "Z", 1);
    tx3.commit();

    // Crash - transaction 2 aborts cleanly, but the log is not flushed so we
    // can still test abort completion
    // Flush the buffer pool to test undo (will contain X, Y, and Z)
    pool.flush_all();
  }

  // Recover
  int fd = open("test.dm", O_RDWR);
  diskmap::BufferPool pool(fd);
  diskmap::WAL wal(&pool, "test.dm.wal");

  // Verify that recovery only retains committed transaction
  assert(pool.get_page(0).data()[0] == 'X');
  assert(pool.get_page(1).data()[0] == '\0');
  assert(pool.get_page(1).data()[1] == 'Z');

  return 0;
}
