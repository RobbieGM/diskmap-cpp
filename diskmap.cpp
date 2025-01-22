#include "diskmap.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>

const char *DiskMap::MAGIC = "DISKMAP";

DiskMap::DiskMap(whl::string path) {
  fd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  bool was_created = fd > 0;
  if (!was_created) {
    fd = open(path.c_str(), O_RDWR);
  }
  if (fd < 0) {
    throw DiskMapException("Failed to open file");
  }
  remap(MAX_ORDER + 2);

  if (was_created) {
    memcpy(mapped, MAGIC, 8);
    *kv_entry_count() = 0;
    *next_free_page() = MAX_ORDER + 2;
    for (order_t order = 0; order <= MAX_ORDER; order++) {
      *last_fpl_page(order) = order + 1;
      *last_fpl_page_entries(order) = 0;
      fpl_init(order + 1);
    }
  } else {
    // Check for magic string
    if (memcmp(mapped, DiskMap::MAGIC, 8) != 0) {
      throw DiskMapException("Invalid diskmap file");
    }
  }
}

DiskMap::~DiskMap() {
  close(fd);
  munmap(mapped, num_mapped_pages * PAGE_SIZE);
}

void DiskMap::remap(int64_t num_pages) {
  // Extend file size if needed
  int64_t required_size = num_pages * PAGE_SIZE;
  if (ftruncate(fd, required_size) == -1) {
    perror("Failed to extend file size");
    exit(1);
  }

  if (mapped && num_mapped_pages > 0) {
#ifdef __linux__
    // Resize existing mapping using mremap on Linux
    void *new_addr = mremap(mapped, num_mapped_pages * PAGE_SIZE, required_size,
                            MREMAP_MAYMOVE);
    if (new_addr == MAP_FAILED) {
      perror("Failed to remap memory");
      exit(1);
    }
    mapped = new_addr;
#else
    // Fall back to munmap + mmap on non-Linux POSIX systems
    if (munmap(mapped, num_mapped_pages * PAGE_SIZE) == -1) {
      perror("Failed to unmap existing mapping");
      exit(1);
    }
    mapped =
        mmap(NULL, required_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
      perror("Failed to map memory");
      exit(1);
    }
#endif
  } else {
    // Create initial mapping
    mapped =
        mmap(NULL, required_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
      perror("Failed to map memory");
      exit(1);
    }
  }

  num_mapped_pages = num_pages;
}

void *DiskMap::get_addr(int64_t page, int offset) {
  return static_cast<char *>(mapped) + PAGE_SIZE * page + offset;
}

int64_t *DiskMap::kv_entry_count() { return (int64_t *)get_addr(0, 8); }

int64_t *DiskMap::next_free_page() { return (int64_t *)get_addr(0, 16); }

int64_t *DiskMap::last_fpl_page(order_t order) {
  return (int64_t *)get_addr(0, 24 + order * 12);
}

int *DiskMap::last_fpl_page_entries(order_t order) {
  return (int32_t *)get_addr(0, 32 + order * 12);
}

void DiskMap::fpl_init(int64_t page) {
  memset(get_addr(page, 0), -1, PAGE_SIZE);
}

int64_t *DiskMap::fpl_previous(int64_t page) {
  return (int64_t *)get_addr(page, 0);
}

int64_t *DiskMap::fpl_next(int64_t page) {
  return (int64_t *)get_addr(page, 8);
}

int64_t *DiskMap::fpl_entry(int64_t page, int64_t index) {
  return (int64_t *)get_addr(page, 16) + index;
}

int64_t DiskMap::allocate_page(order_t order) {
  if (*last_fpl_page_entries(order) == 0) {
    // No freed pages available, allocate a new one
    int64_t page = *next_free_page();
    (*next_free_page()) += 1 << order;
    while (page >= num_mapped_pages) {
      remap(num_mapped_pages * 2);
    }
    return page;
  }

  int64_t fpl_page = *last_fpl_page(order);
  int64_t *freed_page = fpl_entry(fpl_page, *last_fpl_page_entries(order) - 1);
  int64_t result = *freed_page;

  (*last_fpl_page_entries(order))--;

  // If current page becomes empty, move to previous page
  if (*last_fpl_page_entries(order) == 0 && fpl_page != 1) {
    *last_fpl_page(order) = *fpl_previous(fpl_page);
    *last_fpl_page_entries(order) = FPL_PAGE_CAPACITY;
  }

  return result;
}

void DiskMap::free_page(int64_t page, order_t order) {
  if (page <= 1) {
    throw DiskMapException("free_page: cannot free reserved pages");
  }

  // If current FPL page is not full, add page to it
  if (*last_fpl_page_entries(order) < FPL_PAGE_CAPACITY) {
    *fpl_entry(*last_fpl_page(order), *last_fpl_page_entries(order)) = page;
    (*last_fpl_page_entries(order))++;
    return;
  }

  // If there is no next FPL page:
  if (*fpl_next(*last_fpl_page(order)) == -1) {
    if (order == 0) {
      // Use `page` as the next FPL page instead of actually
      // freeing it, since freeing it would require allocating an FPL page
      int64_t current_last = *last_fpl_page(order);
      fpl_init(page);
      *fpl_previous(page) = current_last;
      *fpl_next(current_last) = page;
    } else {
      // Allocate a new FPL page and link it
      int64_t new_fpl_page = allocate_page(0);
      fpl_init(new_fpl_page);
      *fpl_previous(new_fpl_page) = *last_fpl_page(order);
      *fpl_next(*last_fpl_page(order)) = new_fpl_page;
      *fpl_entry(new_fpl_page, 0) = page;
      *last_fpl_page(order) = new_fpl_page;
      *last_fpl_page_entries(order) = 1;
    }
  } else {
    // There is an unused next FPL page, so move to it
    *last_fpl_page(order) = *fpl_next(*last_fpl_page(order));
    *fpl_entry(*last_fpl_page(order), 0) = page;
    *last_fpl_page_entries(order) = 1;
  }
}

// int64_t DiskMap::read(whl::string key, void *&buffer) {}

// void DiskMap::write(whl::string key, void *buffer, int64_t length) {}