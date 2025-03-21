#pragma once

#include "exception.h"
#include "wal.h"

namespace diskmap {

// A utility to manage used space in the data file.
class SpaceManager {

public:
  // Set up any information in the meta page for the SpaceManager.
  static void init(WAL::RWTransaction &t);
  // Allocate a new contiguous region of pages with the specified order. The
  // order of an allocated region is the log base 2 of the number of pages in
  // it, i.e. order 0 = 1 page, order 1 = 2 pages, order 2 = 4 pages, etc.
  // Returns the index of the first page in the region.
  static int64_t allocate(WAL::RWTransaction &t, order_t order);
  // Free a region by its starting page index. Its order must also be specified
  // to properly deallocate it.
  static void free(WAL::RWTransaction &t, int64_t page, order_t order);
};

} // namespace diskmap