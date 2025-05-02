#include "big_value.h"
#include "exception.h"
#include "page_types.h"

namespace diskmap {

void BigValue::rw_impl(size_t offset, char *buffer, size_t length,
                       bool is_write) {
  size_t end_offset = offset + length;
  int depth = 0;
  size_t region_start_page = start_page;
  int region_offset = 0; // Offset in pages into current region
  int page_offset = 0;   // Offset in bytes into current page, relative
                         // to the first byte of the value in the page
  // (value_offset_in_start_page for the first page, 8 for
  // continuation pages, 0 for pure pages)
  size_t value_offset = 0;
  LeafPageType current_page_type = LeafPageType::START;

  // Continue skipping through 1) page regions, 2) pages, or 3) bytes, until the
  // offset is reached
  while (value_offset < offset) {
    size_t distance = offset - value_offset;

    // Skip to next region if needed
    size_t region_skip = (PAGE_SIZE << depth) -
                         start_offset(depth == 0 ? LeafPageType::START
                                                 : LeafPageType::CONTINUATION);
    if (distance >= region_skip) {
      value_offset += region_skip;
      depth++;
      region_start_page = next_region_start(region_start_page);
      current_page_type = LeafPageType::CONTINUATION;
      continue;
    }

    // Skip to next page if needed
    size_t page_skip = PAGE_SIZE - start_offset(current_page_type);
    if (distance >= page_skip) {
      value_offset += page_skip;
      region_offset++;
      current_page_type = LeafPageType::PURE;
      continue;
    }

    // Skip bytes if needed
    page_offset += distance;
    value_offset += distance;
  }

  // Read or write the value
  while (value_offset < end_offset) {
    size_t distance = end_offset - value_offset;
    size_t page_skip =
        PAGE_SIZE - start_offset(current_page_type) - page_offset;
    size_t length_in_page = std::min(distance, page_skip);
    // Get page handle (not actually a MetaPage, actual offset calculated
    // later)
    // Read or write
    if (is_write) {
      auto page = dynamic_cast<WAL::RWTransaction *>(txn)->get_page<MetaPage>(
          region_start_page + region_offset);
      page.write(start_offset(current_page_type) + page_offset,
                 buffer + value_offset - offset, length_in_page);
    } else {
      auto page = txn->get_page<MetaPage>(region_start_page + region_offset);
      memcpy(
          buffer + value_offset - offset,
          static_cast<const char *>(static_cast<const void *>(page.ro_data())) +
              start_offset(current_page_type) + page_offset,
          length_in_page);
    }
    // Update state
    value_offset += length_in_page;
    if (distance > page_skip) {
      region_offset++;
      current_page_type = LeafPageType::PURE;
      page_offset = 0;
      if (region_offset == 1 << depth) {
        depth++;
        region_start_page = next_region_start(region_start_page);
        if (region_start_page == 0) {
          throw DiskMapException("Invalid next pointer in big value");
        }
        current_page_type = LeafPageType::CONTINUATION;
        region_offset = 0;
      }
    }
  }
}

void BigValue::read(size_t offset, char *buffer, size_t length) {
  rw_impl(offset, buffer, length, false);
}

void BigValue::write(size_t offset, const char *buffer, size_t length) {
  // Ensure there are enough pages to fit the value
  size_t max_value_size = offset + length;
  create_continuation_regions(value_offset_in_start_page + max_value_size);
  // Write the value
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  rw_impl(offset, const_cast<char *>(buffer), length, true);
}

int BigValue::required_continuation_regions(size_t entry_size) {
  int result = 0;
  size_t capacity = LeafNodeStartPage::capacity();
  while (capacity < entry_size) {
    size_t pages_in_next_region = 1 << (result + 1);
    capacity += (pages_in_next_region * PAGE_SIZE) -
                offsetof(LeafNodeContinuationPage, data);
    result++;
  }
  return result;
}

void BigValue::create_continuation_regions(
    size_t entry_size) { // TODO: only call when needed
  int continuation_regions =
      BigValue::required_continuation_regions(entry_size);
  auto &rw_txn = *dynamic_cast<WAL::RWTransaction *>(txn);
  WAL::PageHandle<LeafNodeStartPage> leaf_start =
      rw_txn.get_page<LeafNodeStartPage>(start_page);
  // Create linked list of continuation pages
  if (continuation_regions > 0) {
    int order = 1;
    int64_t continuation_page_number = leaf_start.ro_data()->next;
    if (continuation_page_number == 0) {
      continuation_page_number = SpaceManager::allocate(rw_txn, order);
      leaf_start.write(&LeafNodeStartPage::next, continuation_page_number);
    }
    order++;
    WAL::PageHandle<LeafNodeContinuationPage> continuation_page =
        rw_txn.get_page<LeafNodeContinuationPage>(continuation_page_number);
    WAL::PageHandle<LeafNodeContinuationPage> prev =
        std::move(continuation_page);
    continuation_regions--;
    while (continuation_regions > 0) {
      continuation_page_number = prev.ro_data()->next;
      if (continuation_page_number == 0) {
        continuation_page_number = SpaceManager::allocate(rw_txn, order);
        prev.write(&LeafNodeContinuationPage::next, continuation_page_number);
      }
      order++;
      continuation_page =
          rw_txn.get_page<LeafNodeContinuationPage>(continuation_page_number);
      prev = std::move(continuation_page);
      continuation_regions--;
    }
  }
}

} // namespace diskmap
