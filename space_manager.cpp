#include "space_manager.h"
#include "page_types.h"
#include <cstddef>
#include <cstdint>

namespace diskmap {

SpaceManager::SpaceManager() {}

void SpaceManager::init(WAL::RWTransaction &t) {
  WAL::PageHandle<MetaPage> meta = t.get_page<MetaPage>(0);
  meta.write(&MetaPage::next_free_page, static_cast<int64_t>(MAX_ORDER + 3));
  meta.write(&MetaPage::last_fpl_page, 0, static_cast<int64_t>(1));
  meta.write(&MetaPage::last_fpl_page, 1, static_cast<int64_t>(1));
  meta.write(&MetaPage::last_fpl_page_entries, 0, 0);
  meta.write(&MetaPage::last_fpl_page_entries, 1, 0);
}

int64_t SpaceManager::allocate(WAL::RWTransaction &t, order_t order) {
  WAL::PageHandle<MetaPage> meta = t.get_page<MetaPage>(0);
  if (meta.ro_data()->last_fpl_page_entries[order] == 0) {
    // No freed regions are available to reuse
    int64_t page = meta.ro_data()->next_free_page;
    int64_t new_next_free_page = meta.ro_data()->next_free_page + (1 << order);
    meta.write(&MetaPage::next_free_page, new_next_free_page);
    return page;
  }

  // Find the last freed region's index on the free page list (FPL) for the
  // specified order.
  WAL::PageHandle<FPLPage> fpl =
      t.get_page<FPLPage>(meta.ro_data()->last_fpl_page[order]);
  int64_t result =
      fpl.ro_data()->entries[meta.ro_data()->last_fpl_page_entries[order] - 1];

  // Clear the pages in the region
  for (size_t i = 0; i < static_cast<size_t>(1 << order); i++) {
    // <MetaPage> is meaningless here, as we are not reading or writing specific
    // values, but some page type is required
    t.get_page<MetaPage>(result + i).clear();
  }

  // Update last_fpl_page[order] and last_fpl_page_entries[order]
  int new_last_fpl_page_entries =
      meta.ro_data()->last_fpl_page_entries[order] - 1;

  if (new_last_fpl_page_entries == 0) {
    // If current FPL page becomes empty, move to previous page if it exists
    if (fpl.ro_data()->previous != 0) {
      meta.write(&MetaPage::last_fpl_page, order, fpl.ro_data()->previous);
      new_last_fpl_page_entries = FPL_PAGE_CAPACITY;
    }
  }
  meta.write(&MetaPage::last_fpl_page_entries, order,
             new_last_fpl_page_entries);
  return result;
}

void SpaceManager::free(WAL::RWTransaction &t, int64_t page, order_t order) {
  if (page <= 1) {
    throw DiskMapException("free: cannot free reserved pages");
  }

  WAL::PageHandle<MetaPage> meta = t.get_page<MetaPage>(0);

  // If current FPL page is not full, add page to it
  if (meta.ro_data()->last_fpl_page_entries[order] < FPL_PAGE_CAPACITY) {
    WAL::PageHandle<FPLPage> fpl =
        t.get_page<FPLPage>(meta.ro_data()->last_fpl_page[order]);
    fpl.write(&FPLPage::entries, meta.ro_data()->last_fpl_page_entries[order],
              page);
    meta.write(&MetaPage::last_fpl_page_entries, order,
               meta.ro_data()->last_fpl_page_entries[order] + 1);
    return;
  }

  // The current FPL page is full, we may need to allocate a new one and link
  // it, or an empty linked one may already exist
  WAL::PageHandle<FPLPage> fpl =
      t.get_page<FPLPage>(meta.ro_data()->last_fpl_page[order]);
  if (fpl.ro_data()->next == 0) {
    // No empty linked FPL page exists, allocate a new one
    if (order == 0) {
      // Use `page` as the next FPL page instead of actually
      // freeing it, since freeing it would require allocating an FPL page
      int64_t current_last = meta.ro_data()->last_fpl_page[order];
      WAL::PageHandle<FPLPage> new_fpl = t.get_page<FPLPage>(page);
      new_fpl.clear();
      new_fpl.write(&FPLPage::previous, current_last);
      fpl.write(&FPLPage::next, page);
    } else {
      // Allocate a new FPL page and link it
      int64_t new_fpl_page = allocate(t, 0);
      WAL::PageHandle<FPLPage> new_fpl = t.get_page<FPLPage>(new_fpl_page);
      new_fpl.clear();
      new_fpl.write(&FPLPage::previous, meta.ro_data()->last_fpl_page[order]);
      fpl.write(&FPLPage::next, new_fpl_page);
      new_fpl.write(&FPLPage::entries, 0, page);
      meta.write(&MetaPage::last_fpl_page, order, new_fpl_page);
      meta.write(&MetaPage::last_fpl_page_entries, order, 1);
    }
  } else {
    // There is an unused next FPL page, so move to it
    int64_t new_last_fpl_page = fpl.ro_data()->next;
    WAL::PageHandle<FPLPage> new_fpl = t.get_page<FPLPage>(new_last_fpl_page);
    new_fpl.write(&FPLPage::entries, 0, page);
    meta.write(&MetaPage::last_fpl_page, order, new_last_fpl_page);
    meta.write(&MetaPage::last_fpl_page_entries, order, 1);
  }
}

} // namespace diskmap