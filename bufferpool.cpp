#include "bufferpool.h"
#include <unistd.h>
#include <wheel.h>

namespace diskmap {

BufferPool::BufferPool(int fd, int pool_size) : fd(fd) {
  pages.resize(pool_size);
  metadata.resize(pool_size);
}

BufferPool::~BufferPool() {
  // Do not flush or sync
  close(fd);
}

pool_index_t BufferPool::retain(file_index_t file_index, bool advise_eviction) {
  bool needs_read = false;
  bool evicted_needs_write = false;
  file_index_t evicted_page_file_index;
  size_t evicted_page_lsn;
  pool_index_t pool_index;
  {
    whl::mutex_guard _(&mutex);
    if (!file_to_pool_index.contains(file_index)) {
      // Prepare to load this page from the disk to the buffer pool
      if (next_pool_index < POOL_SIZE) {
        // Use a free page
        pool_index = next_pool_index++;
      } else {
        // Evict
        pool_index = replacer.evict();
        if (pool_index == -1) {
          throw BufferPoolFullException();
        }
        if (metadata[pool_index].dirty) {
          evicted_needs_write = true;
          evicted_page_file_index = metadata[pool_index].file_index;
          evicted_page_lsn = metadata[pool_index].page_lsn;
        }
        file_to_pool_index.erase(metadata[pool_index].file_index);
        metadata[pool_index].reset();
      }

      metadata[pool_index].file_index = file_index;
      metadata[pool_index].advise_eviction = advise_eviction;
      file_to_pool_index[file_index] = pool_index;
      needs_read = true;
    } else {
      // Page is already in the pool
      pool_index = file_to_pool_index[file_index];
    }
    metadata[pool_index].refcount++;
    metadata[pool_index].advise_eviction &= advise_eviction;
  }
  // Defer reads and writes until after the buffer pool mutex has been released
  if (evicted_needs_write) {
    // Ensure the WAL flushes operations up until the last modification of the
    // evicted page. Necessary for safety.
    if (wal) {
      wal->flush_up_to(evicted_page_lsn);
    }
    // Potential future optimization: do this asynchronously
    pwrite(fd, pages[pool_index].data, PAGE_SIZE,
           evicted_page_file_index * PAGE_SIZE);
  }
  if (needs_read) {
    pread(fd, pages[pool_index].data, PAGE_SIZE, file_index * PAGE_SIZE);
  }
  return pool_index;
}

void BufferPool::release(pool_index_t pool_index) {
  if (metadata[pool_index].refcount <= 0) {
    throw BufferPoolException();
  }
  metadata[pool_index].refcount--;
  if (metadata[pool_index].refcount == 0) {
    replacer.insert(pool_index, metadata[pool_index].advise_eviction);
  }
}

BufferPool::PageHandle::PageHandle(BufferPool *pool, pool_index_t index,
                                   bool advise_eviction)
    : pool(pool), index(index) {}

BufferPool::PageHandle::~PageHandle() {
  whl::mutex_guard _(&pool->mutex);
  pool->release(index);
}

char *BufferPool::PageHandle::data() { return pool->pages[index].data; }

void BufferPool::PageHandle::modified_by(size_t lsn) {
  pool->metadata[index].dirty = true;
  if (lsn > pool->metadata[index].page_lsn) {
    pool->metadata[index].page_lsn = lsn;
  }
}

BufferPool::PageHandle BufferPool::get_page(file_index_t file_index,
                                            bool advise_eviction) {
  pool_index_t index = retain(file_index, advise_eviction);
  return PageHandle(this, index, advise_eviction);
}

void BufferPool::flush_all() {
  whl::mutex_guard _(&mutex);
  for (pool_index_t i = 0; i < POOL_SIZE; i++) {
    if (metadata[i].dirty) {
      pwrite(fd, pages[i].data, PAGE_SIZE, metadata[i].file_index * PAGE_SIZE);
      metadata[i].dirty = false;
    }
  }
  fsync(fd);
}

} // namespace diskmap