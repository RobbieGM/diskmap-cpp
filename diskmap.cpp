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
  remap(3);

  if (was_created) {
    memcpy(mapped, MAGIC, 8);
    *next_free_page() = 3;

    memset(get_addr(1, 0), -1, PAGE_SIZE);

    memset(get_addr(2, 0), -1, PAGE_SIZE);
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

int64_t *DiskMap::dir_entry_count() { return (int64_t *)get_addr(0, 24); }

int64_t DiskMap::get_split_index() {
  // Return dir_entry_count with MSB set to zero, because split index starts
  // over at 0 after reaching a power of 2
  int64_t i = 1;
  while (i <= *dir_entry_count())
    i *= 2;
  return *dir_entry_count() ^ (i / 2);
}

int64_t DiskMap::allocate_page() {
  // Check if there are any freed pages
  if (*(int64_t *)get_addr(1, 8) != -1) {
    // Find newest freed list page that is not composed of all -1s
    // FPL pages are never freed to avoid circular freeing/allocation issue
    int64_t fpl_page = 1;
    int64_t next_fpl_page;
    while ((next_fpl_page = *(int64_t *)get_addr(fpl_page, 0)) != -1 &&
           *(int64_t *)get_addr(next_fpl_page, 8) != -1) {
      fpl_page = next_fpl_page;
    }

    // Find first non-(-1) entry from the end of the page
    int64_t *page_end = (int64_t *)get_addr(fpl_page + 1, 0);
    int64_t *freed_page = page_end - 1;
    while (freed_page >= (int64_t *)get_addr(fpl_page, 8) &&
           *freed_page == -1) {
      freed_page--;
    }

    if (freed_page < (int64_t *)get_addr(fpl_page, 8)) {
      throw DiskMapException(
          "allocate_page: mistakenly advanced to empty FPL page");
    }

    int64_t result = *freed_page;
    *freed_page = -1;
    return result;
  }

  // No freed pages available, allocate a new one
  (*next_free_page())++;
  return *next_free_page() - 1;
}

void DiskMap::free_page(int64_t page) {
  if (page <= 2) {
    throw DiskMapException("free_page: cannot free reserved pages");
  }

  int64_t fpl_page = 1;
  // Advance until finding a non-full FPL page, or the end
  while (true) {
    bool is_full = *(int64_t *)get_addr(fpl_page, PAGE_SIZE - 8) != -1;
    if (!is_full) {
      // Add page to this FPL page
      // Start by finding the first -1 entry from the start of the page
      int64_t *freed_page = (int64_t *)get_addr(fpl_page, 8);
      while (*freed_page != -1) {
        freed_page++;
      }
      // Write page to the FPL page
      *freed_page = page;
      return;
    }

    // Page is full, check/create next page
    int64_t next_fpl_page = *(int64_t *)get_addr(fpl_page, 0);
    if (next_fpl_page == -1) {
      // Allocate a next FPL page
      next_fpl_page = allocate_page();
      *(int64_t *)get_addr(fpl_page, 0) = next_fpl_page;
      // Initialize next FPL page
      memset(get_addr(next_fpl_page, 0), -1, PAGE_SIZE);
    }
    fpl_page = next_fpl_page;
  }
}