#include "diskmap.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>

DiskMap::DiskMap(whl::string path) {
  fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
  if (fd < 0) {
    printf("Failed to open diskmap file\n");
    exit(1);
  }
}
