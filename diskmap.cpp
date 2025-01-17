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
    // copy magic string to first 8 bytes of page 0
    // rest of page 0 is already initialized to 0 by default
    memcpy(mapped, MAGIC, 8);

    // set all of page 1 to -1
    memset(get_addr(1, 0), -1, PAGE_SIZE);

    // set all of page 2 to -1
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
  return static_cast<char*>(mapped) + PAGE_SIZE * page + offset;
}

size_t *DiskMap::kv_entry_count() {
  return (size_t*) get_addr(0, 8);
}

size_t *DiskMap::next_free_page() {
  return (size_t*) get_addr(0, 16);
}

size_t *DiskMap::dir_entry_count() {
  return (size_t*) get_addr(0, 24);
}

size_t DiskMap::get_split_index() {
  
}

// void DiskMap::write_integer(void *target, int64_t num) {
//   *(int64_t*)target = num;
// }