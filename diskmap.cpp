#include "diskmap.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>

const char *DiskMap::magic = "DISKMAP";

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
  } else {
    // Check for magic string
    if (memcmp(mapped, DiskMap::magic, 8) != 0) {
      throw DiskMapException("Invalid diskmap file");
    }
  }
}

DiskMap::~DiskMap() {
  close(fd);
  munmap(mapped, num_mapped_pages * page_size);
}

void DiskMap::remap(size_t num_pages) {
  page_size = getpagesize();

  // Extend file size if needed
  size_t required_size = num_pages * page_size;
  if (ftruncate(fd, required_size) == -1) {
    perror("Failed to extend file size");
    exit(1);
  }

  if (mapped && num_mapped_pages > 0) {
#ifdef __linux__
    // Resize existing mapping using mremap on Linux
    void *new_addr = mremap(mapped, num_mapped_pages * page_size, required_size,
                            MREMAP_MAYMOVE);
    if (new_addr == MAP_FAILED) {
      perror("Failed to remap memory");
      exit(1);
    }
    mapped = new_addr;
#else
    // Fall back to munmap + mmap on non-Linux POSIX systems
    if (munmap(mapped, num_mapped_pages * page_size) == -1) {
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