#pragma once

#include "page_types.h"
#include "wal.h"
#include <functional>
#include <wheel.h>

namespace diskmap {

class BigValue {
  WAL::ROTransaction *txn;
  // Page numbers of start page of each region, starting with a
  // LeafNodeStartPage, and the rest are LeafNodeContinuationPages
  size_t start_page;
  size_t value_offset_in_start_page;

  enum class LeafPageType : uint8_t { START, CONTINUATION, PURE };

  size_t start_offset(LeafPageType type) const {
    switch (type) {
    case LeafPageType::START:
      return offsetof(LeafNodeStartPage, data) + value_offset_in_start_page;
    case LeafPageType::CONTINUATION:
      return offsetof(LeafNodeContinuationPage, data);
    case LeafPageType::PURE:
      return 0;
    }
  }

  size_t next_region_start(size_t region_start_page) {
    // This relies on the fact that LeafNodeStartPage and
    // LeafNodeContinuationPage both have the next pointer at the same offset
    return txn->get_page<LeafNodeContinuationPage>(region_start_page)
        .ro_data()
        ->next;
  }

  void rw_impl(size_t offset, char *buffer, size_t length, bool is_write);

public:
  BigValue(WAL::ROTransaction *txn, uint64_t page, size_t offset)
      : txn(txn), start_page(page), value_offset_in_start_page(offset) {}
  void read(size_t offset, char *buffer, size_t length);
  void write(size_t offset, const char *buffer, size_t length);
  // Calculate the number of continuation regions needed to store an entry with
  // a given size (including key and value).
  static int required_continuation_pages(size_t entry_size);
};

} // namespace diskmap
