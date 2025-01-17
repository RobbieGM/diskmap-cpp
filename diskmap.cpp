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

void DiskMap::remap(size_t num_pages) {
  // Extend file size if needed
  size_t required_size = num_pages * PAGE_SIZE;
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

void *DiskMap::get_addr(size_t page, int offset) {
  return static_cast<char *>(mapped) + PAGE_SIZE * page + offset;
}

size_t *DiskMap::kv_entry_count() { return (size_t *)get_addr(0, 8); }

size_t *DiskMap::next_free_page() { return (size_t *)get_addr(0, 16); }

size_t *DiskMap::dir_entry_count() { return (size_t *)get_addr(0, 24); }

size_t DiskMap::get_split_index() {
  // Return dir_entry_count with MSB set to zero, because split index starts
  // over at 0 after reaching a power of 2
  size_t i = 1;
  while (i <= *dir_entry_count())
    i *= 2;
  return *dir_entry_count() ^ (i / 2);
}

size_t DiskMap::allocate_page() {
  // Find a freed page if it exists
  int64_t *first_entry = (int64_t *)get_addr(1, 8);

  if (*first_entry != -1) {
    // Find newest freed list page
    int64_t prev_fpl_page = -1;
    int64_t fpl_page = 1;
    while (*(int64_t *)get_addr(fpl_page, 0) != -1) {
      prev_fpl_page = fpl_page;
      fpl_page = *(int64_t *)get_addr(fpl_page, 0);
    }

    // Find first non-(-1) entry from the end of the page
    int64_t *page_end = (int64_t *)get_addr(fpl_page + 1, 0);
    int64_t *freed_page = page_end - 1;
    while (freed_page >= (int64_t *)get_addr(fpl_page, 8) &&
           *freed_page == -1) {
      freed_page--;
    }

    if (freed_page >= (int64_t *)get_addr(fpl_page, 8)) {
      // Found a valid freed page
      size_t result = *freed_page;
      *freed_page = -1;

      // If this was the last non-(-1) entry in a non-first FPL page,
      // unlink this page from the list
      if (freed_page == (int64_t *)get_addr(fpl_page, 8) && fpl_page != 1) {
        *(int64_t *)get_addr(prev_fpl_page, 0) = -1;
      }

      // Update the first entry pointer if we just used the last freed page
      if (fpl_page == 1 && freed_page == (int64_t *)get_addr(1, 8)) {
        *first_entry = -1;
      }

      return result;
    }
  }

  // No freed pages available, allocate a new one
  (*next_free_page())++;
  return *next_free_page() - 1;
}