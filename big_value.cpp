#include "big_value.h"
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
  int value_offset = 0;
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

  // Read the value
  while (value_offset < end_offset) {
    size_t distance = end_offset - value_offset;
    size_t page_skip =
        PAGE_SIZE - start_offset(current_page_type) - page_offset;
    size_t length_in_page = whl::min(distance, page_skip);
    // Get page handle (not actually a MetaPage, actual offset calculated
    // later)
    // Read or write
    if (is_write) {
      auto page = dynamic_cast<WAL::RWTransaction *>(txn)->get_page<MetaPage>(
          region_start_page + region_offset);
      page.write(start_offset(current_page_type) + page_offset,
                 buffer + value_offset, length_in_page);
    } else {
      auto page = txn->get_page<MetaPage>(region_start_page + region_offset);
      memcpy(
          buffer + value_offset,
          static_cast<const char *>(static_cast<const void *>(page.ro_data())) +
              start_offset(current_page_type) + page_offset,
          length_in_page);
    }
    // Update state
    value_offset += length_in_page;
    if (distance > page_skip) {
      region_offset++;
      current_page_type = LeafPageType::PURE;
      if (region_offset == 1 << (depth + 1)) {
        depth++;
        region_start_page = next_region_start(region_start_page);
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
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  rw_impl(offset, const_cast<char *>(buffer), length, true);
}

int BigValue::required_continuation_pages(size_t entry_size) {
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

} // namespace diskmap
