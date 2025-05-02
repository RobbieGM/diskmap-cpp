#pragma once

#include "page_types.h"
#include "replacer.h"
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace diskmap {

// Index of a page in the data file.
using file_index_t = uint64_t;

// Index of a page in the buffer pool's page array.
using pool_index_t = int;

struct PageData {
  char data[PAGE_SIZE];
};

struct PageMetadata {
  // Index of this page in the file. -1 if unused
  file_index_t file_index;

  // Latest LSN (log sequence number) of any log record that modified this page.
  size_t page_lsn;

  // Number of references to this page. Page may only be evicted when
  // refcount = 0.
  int refcount;

  // True if the this page has been written to and not flushed.
  bool dirty;

  // A hint to the buffer pool to prefer to evict this page, if eviction is
  // necessary. Usually set to true when the page is expected to only be used
  // once.
  bool advise_eviction;

  void reset() {
    file_index = -1;
    page_lsn = 0;
    refcount = 0;
    dirty = false;
    advise_eviction = false;
  }

  PageMetadata() { reset(); }
};

class BufferPoolException {};
class BufferPoolFullException : BufferPoolException {};

// Represents all operations the buffer pool requires from the write-ahead log.
class AbstractWAL {
public:
  // Flush the write-ahead log up until `lsn`.  It's necessary when evicting a
  // dirty page to evict records up to its LSN, so that changes to it are
  // recorded in the log and thus reversible and redoable.
  virtual void flush_up_to(size_t lsn) = 0;
};

// The buffer pool is like a scratch space for the DBMS to cache and modify
// pages. Unlike an approach mmap, it allows the DBMS to have greater control
// over which pages are flushed and when. This is important to guarantee
// durability.
class BufferPool {
  // Number of pages in the buffer pool.
  constexpr static pool_index_t DEFAULT_POOL_SIZE = 8192; // *4kB page = 32MiB
  int fd;
  pool_index_t pool_size;
  AbstractWAL *wal;
  // Index of the next free slot in the buffer pool, limited to POOL_SIZE - 1.
  pool_index_t next_pool_index = 0;
  std::vector<PageData> pages;
  std::vector<PageMetadata> metadata;
  // The replacer controls which not-in-use pages should be evicted.
  DualPriorityReplacer replacer;
  // A lookup table to find a page's index in the buffer pool by its index in
  // the data file.
  std::unordered_map<file_index_t, pool_index_t> file_to_pool_index;
  std::mutex mutex;

  // Put a page from the file into the pool, if needed, and increment its
  // refcount
  pool_index_t retain(file_index_t file_index, bool advise_eviction);
  // Decrement the refcount of a page
  void release(pool_index_t pool_index);

public:
  // A handle to a page in the buffer pool, which automatically handles updating
  // refcount when created or destroyed, ensures the page is made resident in
  // the buffer pool when created, and tracks page writes.
  class PageHandle {
    BufferPool *pool;
    pool_index_t pool_index;

    PageHandle(BufferPool *pool, file_index_t index, bool advise_eviction);
    friend class BufferPool;

  public:
    ~PageHandle();
    PageHandle(const PageHandle &) = delete;
    PageHandle &operator=(const PageHandle &) = delete;
    PageHandle(PageHandle &&) noexcept;
    PageHandle &operator=(PageHandle &&) noexcept;

    char *data();
    // Notify the page handle that the data has been modified by a log record
    // with this lsn.
    void modified_by(size_t lsn);
  };

  BufferPool(const BufferPool &) = delete;
  BufferPool(BufferPool &&) = delete;
  BufferPool &operator=(const BufferPool &) = delete;
  BufferPool &operator=(BufferPool &&) = delete;

  explicit BufferPool(int fd, int pool_size = DEFAULT_POOL_SIZE);
  ~BufferPool();
  void set_wal(AbstractWAL *w) { wal = w; }
  PageHandle get_page(file_index_t file_index, bool advise_eviction = false);
  void flush_all();
};

} // namespace diskmap