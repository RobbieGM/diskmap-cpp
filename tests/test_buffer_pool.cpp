#include "../bufferpool.h"

#include <assert.h>
#include <cstdio>
#include <fcntl.h>

class MockWAL : public diskmap::AbstractWAL {
public:
  size_t last_flushed_lsn = -1UL;
  void flush_up_to(size_t lsn) override { last_flushed_lsn = lsn; }
};

int main() {
  MockWAL wal;
  remove("test.dm");
  int fd = open("test.dm", O_RDWR | O_CREAT | O_EXCL, 0666);
  diskmap::BufferPool pool(fd, 2); // 2 page pool
  pool.set_wal(&wal);

  auto p0 = pool.get_page(0);
  p0.data()[0] = 'a';
  p0.modified_by(0);
  auto p1 = pool.get_page(1);
  p1.data()[0] = 'b';
  p1.modified_by(1);
  assert(wal.last_flushed_lsn == -1UL);
  p0.diskmap::BufferPool::PageHandle::~PageHandle();
  {
    auto p2 = pool.get_page(2);
    p2.data()[0] = 'c';
    p2.modified_by(2);
    // p2 replaces p0, which has pagelsn of 0, so log flushes up to lsn 0
    assert(wal.last_flushed_lsn == 0);
  }
  auto p0_b = pool.get_page(0);
  assert(p0_b.data()[0] == 'a');
  bool threw = false;
  try {
    // Retain a page when all 2 slots have pinned pages already
    pool.get_page(5);
  } catch (const diskmap::BufferPoolFullException &e) {
    threw = true;
  }
  assert(threw);
  return 0;
}