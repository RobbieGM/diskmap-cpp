#include "buffer_pool.h"
#include <mutex>
#include <unistd.h>

namespace diskmap {

BufferPool::BufferPool(int fd, int pool_size)
    : fd(fd), pool_size(pool_size), wal(nullptr) {
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
  file_index_t evicted_page_file_index = -1;
  size_t evicted_page_lsn = -1;
  pool_index_t pool_index = -1;
  {
    std::lock_guard _(mutex);
    if (file_to_pool_index.find(file_index) == file_to_pool_index.end()) {
      // Prepare to load this page from the disk to the buffer pool
      if (next_pool_index < pool_size) {
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
      replacer.remove(pool_index);
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
           static_cast<long>(evicted_page_file_index * PAGE_SIZE));
  }
  if (needs_read) {
    size_t bytes_read = pread(fd, pages[pool_index].data, PAGE_SIZE,
                              static_cast<long>(file_index * PAGE_SIZE));
    if (bytes_read == 0) {
      memset(pages[pool_index].data, 0, PAGE_SIZE);
    }
  }
  return pool_index;
}

void BufferPool::release(pool_index_t pool_index) {
  std::lock_guard _(mutex);
  if (metadata[pool_index].refcount <= 0) {
    throw BufferPoolException();
  }
  metadata[pool_index].refcount--;
  if (metadata[pool_index].refcount == 0) {
    replacer.insert(pool_index, metadata[pool_index].advise_eviction);
  }
}

BufferPool::PageHandle::PageHandle(BufferPool *pool, file_index_t file_index,
                                   bool advise_eviction)
    : pool(pool), pool_index(pool->retain(file_index, advise_eviction)) {}

BufferPool::PageHandle::~PageHandle() {
  if (pool_index != -1) {
    pool->release(pool_index);
  }
  pool_index = -1;
}

BufferPool::PageHandle::PageHandle(PageHandle &&other) noexcept
    : pool(other.pool), pool_index(other.pool_index) {
  other.pool_index = -1;
}

BufferPool::PageHandle &
BufferPool::PageHandle::operator=(PageHandle &&other) noexcept {
  if (this != &other) {
    pool = other.pool;
    pool_index = other.pool_index;
    other.pool_index = -1;
  }
  return *this;
}

char *BufferPool::PageHandle::data() { return pool->pages[pool_index].data; }

void BufferPool::PageHandle::modified_by(size_t lsn) {
  pool->metadata[pool_index].dirty = true;
  pool->metadata[pool_index].page_lsn =
      std::max(lsn, pool->metadata[pool_index].page_lsn);
}

BufferPool::PageHandle BufferPool::get_page(file_index_t file_index,
                                            bool advise_eviction) {
  return PageHandle(this, file_index, advise_eviction);
}

void BufferPool::flush_all() {
  std::lock_guard _(mutex);
  for (pool_index_t i = 0; i < pool_size; i++) {
    if (metadata[i].dirty) {
      pwrite(fd, pages[i].data, PAGE_SIZE,
             static_cast<long>(metadata[i].file_index * PAGE_SIZE));
      metadata[i].dirty = false;
    }
  }
  fsync(fd);
}

} // namespace diskmap