#include "../wal.h"
#include <assert.h>
#include <cstdio>
#include <fcntl.h>

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  int fd = open("test.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
  diskmap::BufferPool pool(fd, 2);
  diskmap::WAL wal(&pool, "test.dm.wal");
  {
    auto tx = wal.begin_transaction();
    auto p0 = tx.get_page(0);
    p0.write(0, "a", 1);
    assert(pool.get_page(0).data()[0] == 'a');
    tx.abort();
    assert(pool.get_page(0).data()[0] == '\0');
  }
  {
    auto tx = wal.begin_transaction();
    auto p0 = tx.get_page(0);
    p0.write(0, "b", 1);
    assert(pool.get_page(0).data()[0] == 'b');
    tx.commit();
    assert(pool.get_page(0).data()[0] == 'b');
  }
  {
    auto unfinished_tx = wal.begin_transaction();
    auto p0 = unfinished_tx.get_page(0);
    p0.write(0, "c", 1);
    // Implicit abort
  }
  assert(pool.get_page(0).data()[0] == 'b');

  return 0;
}
