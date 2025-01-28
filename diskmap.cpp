#include "diskmap.h"
#include "page_types.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace diskmap {

whl::mutex_guard LockManager::get_guard(int64_t page) {
  return whl::mutex_guard(&locks[page]);
}

const char *DiskMap::MAGIC = "DISKMAP";

DiskMap::DiskMap(whl::string path) {
  fd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  bool was_created = fd > 0;
  if (was_created) {
    extend_file(MAX_ORDER + 3);
  } else {
    fd = open(path.c_str(), O_RDWR);
  }
  if (fd < 0) {
    throw DiskMapException("Failed to open file");
  }
  file_pages = lseek(fd, 0, SEEK_END) / PAGE_SIZE;
  mapped = mmap(NULL, MMAPPED_PAGES * PAGE_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    perror("Failed to map memory");
    exit(1);
  }

  if (was_created) {
    // Initialize page 0 (metadata)
    memcpy(meta()->magic, MAGIC, 8);
    meta()->kv_entry_count = 0;
    // meta page + root page + (MAX_ORDER + 1) FPL pages
    meta()->next_free_page = MAX_ORDER + 3;

    // Initialize root page
    internal_node(1)->init();

    // Initialize all FPL pages
    for (order_t order = 0; order <= MAX_ORDER; order++) {
      meta()->last_fpl_page[order] = order + 2;
      meta()->last_fpl_page_entries[order] = 0;
      fpl(order + 2)->init();
    }
  } else {
    // Check for magic string
    if (memcmp(meta()->magic, DiskMap::MAGIC, 8) != 0) {
      throw DiskMapException("Invalid diskmap file");
    }
  }
}

DiskMap::~DiskMap() {
  if (msync(mapped, MMAPPED_PAGES * PAGE_SIZE, MS_SYNC) == -1) {
    perror("Failed to sync memory");
    exit(1);
  }
  close(fd);
  munmap(mapped, MMAPPED_PAGES * PAGE_SIZE);
}

void DiskMap::extend_file(int64_t num_pages) {
  // Extend file size if needed
  int64_t required_size = num_pages * PAGE_SIZE;
  if (ftruncate(fd, required_size) == -1) {
    perror("Failed to extend file size");
    exit(1);
  }
  file_pages = num_pages;
}

void *DiskMap::get_addr(int64_t page, int offset) {
  return static_cast<char *>(mapped) + PAGE_SIZE * page + offset;
}

int64_t DiskMap::allocate_page(order_t order) {
  if (meta()->last_fpl_page_entries[order] == 0) {
    // No freed pages available, allocate a new one
    int64_t page = meta()->next_free_page;
    meta()->next_free_page += 1 << order;
    while (meta()->next_free_page > file_pages) {
      extend_file(file_pages * 2);
    }
    return page;
  }

  int64_t fpl_page = meta()->last_fpl_page[order];
  int64_t result =
      fpl(fpl_page)->entries[meta()->last_fpl_page_entries[order] - 1];

  (meta()->last_fpl_page_entries[order])--;

  // If current page becomes empty, move to previous page if it exists
  if (meta()->last_fpl_page_entries[order] == 0 &&
      fpl(fpl_page)->previous != -1) {
    meta()->last_fpl_page[order] = fpl(fpl_page)->previous;
    meta()->last_fpl_page_entries[order] = FPL_PAGE_CAPACITY;
  }

  return result;
}

void DiskMap::free_page(int64_t page, order_t order) {
  if (page <= 1) {
    throw DiskMapException("free_page: cannot free reserved pages");
  }

  // If current FPL page is not full, add page to it
  if (meta()->last_fpl_page_entries[order] < FPL_PAGE_CAPACITY) {
    fpl(meta()->last_fpl_page[order])
        ->entries[meta()->last_fpl_page_entries[order]] = page;
    meta()->last_fpl_page_entries[order]++;
    return;
  }

  // If there is no next FPL page:
  if (fpl(meta()->last_fpl_page[order])->next == -1) {
    if (order == 0) {
      // Use `page` as the next FPL page instead of actually
      // freeing it, since freeing it would require allocating an FPL page
      int64_t current_last = meta()->last_fpl_page[order];
      fpl(page)->init();
      fpl(page)->previous = current_last;
      fpl(current_last)->next = page;
    } else {
      // Allocate a new FPL page and link it
      int64_t new_fpl_page = allocate_page(0);
      fpl(new_fpl_page)->init();
      fpl(new_fpl_page)->previous = meta()->last_fpl_page[order];
      fpl(meta()->last_fpl_page[order])->next = new_fpl_page;
      fpl(new_fpl_page)->entries[0] = page;
      meta()->last_fpl_page[order] = new_fpl_page;
      meta()->last_fpl_page_entries[order] = 1;
    }
  } else {
    // There is an unused next FPL page, so move to it
    meta()->last_fpl_page[order] = fpl(meta()->last_fpl_page[order])->next;
    fpl(meta()->last_fpl_page[order])->entries[0] = page;
    meta()->last_fpl_page_entries[order] = 1;
  }
}

static int64_t set_msb(int64_t val, int64_t bit) {
  return (val & ~(1LL << 63)) | (bit << 63);
}

static int64_t get_msb(int64_t val) { return val >> 63; }

static int64_t clear_msb(int64_t val) { return val & ~(1LL << 63); }

int64_t DiskMap::get_bucket(uint64_t hash, int64_t depth) {
  // INNER_NODE_BRANCHING_FACTOR = 512 (for example)
  // Depth = 0 means root node
  // Suppose hash = x + y * 512 + z * 512 * 512 + ...
  // If depth = 0, return x, if depth = 1, return y
  // get_bucket(hash, 0) determines the index within the root node, and so on

  while (depth > 0) {
    hash /= InternalNodePage::BRANCHING_FACTOR;
    depth--;
  }
  return hash % InternalNodePage::BRANCHING_FACTOR;
}

char *DiskMap::find_entry_in_leaf(LeafNodePage *leaf, const whl::string &key) {
  char *ptr = static_cast<char *>(&leaf->data);

  while (ptr < leaf->end()) {
    if (*ptr == '\0') { // Empty key marks end of entries
      return nullptr;
    }

    if (strncmp(ptr, key.c_str(), key.size()) == 0) {
      return ptr;
    }

    // Skip past current entry
    size_t key_size = strlen(ptr) + 1;
    ptr += key_size;

    uint64_t value_length = *reinterpret_cast<uint64_t *>(ptr);
    ptr += sizeof(uint64_t) + value_length;
  }
  return nullptr;
}

whl::vector<KVEntry> DiskMap::get_entries_in_leaf(LeafNodePage *leaf) {
  whl::vector<KVEntry> result;
  char *ptr = static_cast<char *>(&leaf->data);

  while (ptr < leaf->end()) {
    if (*ptr == '\0') { // Empty key marks end of entries
      break;
    }

    KVEntry entry;
    entry.key = ptr;
    ptr += strlen(ptr) + 1;
    entry.value.resize(*reinterpret_cast<uint64_t *>(ptr));
    uint64_t value_length = *reinterpret_cast<uint64_t *>(ptr);
    ptr += sizeof(uint64_t) + value_length;
    memcpy(entry.value.data_ptr(), ptr, value_length);
    result.push_back(entry);
  }
  return result;
}

void DiskMap::create_subtree(int64_t *parent_entry, int depth,
                             const whl::vector<KVEntry> &entries) {
  size_t total_entries_size = 0;
  for (size_t i = 0; i < entries.size(); i++) {
    total_entries_size +=
        entries[i].key.size() + 1 + sizeof(uint64_t) + entries[i].value.size();
  }
  int64_t new_parent_entry = -1;
  if (total_entries_size < LeafNodePage::capacity(0) || entries.size() == 1) {
    // If all entries can fit in a leaf, or there is only one entry, use one
    // leaf node
    order_t order = 0;
    while (order < MAX_ORDER &&
           total_entries_size > LeafNodePage::capacity(order)) {
      order++;
    }
    int64_t new_leaf_page_number = allocate_page(order);
    LeafNodePage *new_leaf = leaf_node(new_leaf_page_number);
    new_leaf->init(order);
    new_leaf->usage = total_entries_size;
    new_leaf->entry_count = entries.size();

    auto *ptr = static_cast<char *>(&new_leaf->data);
    for (size_t i = 0; i < entries.size(); i++) {
      // Write key
      memcpy(ptr, entries[i].key.c_str(), entries[i].key.size() + 1);
      ptr += entries[i].key.size() + 1;

      // Write value length
      *reinterpret_cast<uint64_t *>(ptr) = entries[i].value.size();
      ptr += sizeof(uint64_t);

      // Write value
      memcpy(ptr, entries[i].value.data_ptr(), entries[i].value.size());
      ptr += entries[i].value.size();
    }

    meta()->kv_entry_count += entries.size();

    // Replace parent entry with pointer to new leaf
    new_parent_entry = set_msb(new_leaf_page_number, 0);
  } else {
    // If entries cannot fit in a leaf and there is more than one, create
    // internal node
    whl::vector<KVEntry> entries_by_hash[InternalNodePage::BRANCHING_FACTOR];
    for (size_t i = 0; i < entries.size(); i++) {
      entries_by_hash[get_bucket(entries[i].key.hash(), depth)].push_back(
          entries[i]);
    }

    int64_t new_internal_node_page_number = allocate_page(0);
    InternalNodePage *new_internal_node =
        internal_node(new_internal_node_page_number);
    new_internal_node->init();
    for (size_t i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
      if (entries_by_hash[i].size() > 0) {
        create_subtree(&new_internal_node->entries[i], depth + 1,
                       entries_by_hash[i]);
      }
    }

    new_parent_entry = set_msb(new_internal_node_page_number, 1);
  }

  // Free old leaf node
  if (*parent_entry != -1) {
    int64_t parent_entry_page = clear_msb(*parent_entry);
    bool parent_entry_points_to_internal_node = get_msb(*parent_entry);
    if (parent_entry_points_to_internal_node) {
      throw DiskMapException(
          "create_subtree: parent entry points to internal node");
    }

    LeafNodePage *original_leaf = leaf_node(parent_entry_page);
    free_page(parent_entry_page, original_leaf->order);
  }

  *parent_entry = new_parent_entry;
}

void DiskMap::write(whl::string key, const void *buffer, int64_t length) {
  if (key.size() == 0 || buffer == nullptr || length < 0) {
    throw DiskMapException("write: invalid input parameters");
  }

  int64_t page = ROOT_PAGE;
  int depth = 0;
  int64_t *parent_entry = nullptr;
  // Traverse tree to find leaf node
  while (true) {
    InternalNodePage *internal = internal_node(page);
    int64_t idx = get_bucket(key.hash(), depth);
    if (internal->entries[idx] == -1) {
      // Need to create subtree
      whl::vector<KVEntry> entries;
      KVEntry entry;
      entry.key = key;
      entry.value.resize(length);
      memcpy(entry.value.data_ptr(), buffer, length);
      entries.push_back(entry);
      create_subtree(&internal->entries[idx], depth + 1, entries);
      return;
    } else if (get_msb(internal->entries[idx]) == 0) {
      // Found leaf node
      page = clear_msb(internal->entries[idx]);
      parent_entry = &internal->entries[idx];
      break;
    }
    page = clear_msb(internal->entries[idx]);
    depth++;
  }
  LeafNodePage *leaf = leaf_node(page);

  char *entry_ptr = find_entry_in_leaf(leaf, key);

  if (entry_ptr) {
    // Update existing entry
    char *value_length_ptr = entry_ptr + key.size() + 1;
    int64_t current_value_length =
        static_cast<int64_t>(*reinterpret_cast<uint64_t *>(value_length_ptr));
    char *value_ptr = value_length_ptr + sizeof(uint64_t);

    if (length <= current_value_length) {
      // Can update in place
      memcpy(value_ptr, buffer, length);
      *reinterpret_cast<uint64_t *>(value_length_ptr) = length;

      if (length < current_value_length) {
        // Compact the space if new value is smaller
        char *next_entry = value_ptr + current_value_length;
        size_t bytes_to_move = static_cast<char *>(leaf->end()) - next_entry;
        char *new_next = value_ptr + length;
        memmove(new_next, next_entry, bytes_to_move);
        leaf->usage -= (current_value_length - length);
      }
    } else {
      // Need more space for larger value
      char *next_entry = value_ptr + current_value_length;
      size_t additional_space = length - current_value_length;

      if (leaf->usage + additional_space > leaf->capacity()) {
        // Copy entries into memory and update
        // TODO: may not fit in memory
        auto entries = get_entries_in_leaf(leaf);
        for (size_t i = 0; i < entries.size(); i++) {
          if (entries[i].key == key) {
            entries[i].value.resize(length);
            memcpy(entries[i].value.data_ptr(), buffer, length);
            break;
          }
        }

        // Rebuild tree
        meta()->kv_entry_count -= entries.size();
        create_subtree(parent_entry, depth, entries);
        return;
      }

      // Move existing data to make room
      size_t bytes_to_move =
          static_cast<char *>(leaf->end()) - next_entry - additional_space;
      memmove(next_entry + additional_space, next_entry, bytes_to_move);
      memcpy(value_ptr, buffer, length);
      *reinterpret_cast<uint64_t *>(value_length_ptr) = length;
      leaf->usage += additional_space;
    }
  } else {
    // Append new entry
    size_t required_space = key.size() + 1 + sizeof(uint64_t) + length;
    if (leaf->usage + required_space > leaf->capacity()) {
      // Leaf needs to expand or be split
      // TODO: may not fit in memory
      auto entries = get_entries_in_leaf(leaf);
      entries.push_back({key, whl::vector<char>(length)});
      entries.back().value.resize(length);
      memcpy(entries.back().value.data_ptr(), buffer, length);

      // Rebuild tree
      meta()->kv_entry_count -= entries.size() - 1; // Previous entry count
      create_subtree(parent_entry, depth, entries);
      return;
    }

    char *append_ptr = static_cast<char *>(&leaf->data) + leaf->usage;

    // Write key
    memcpy(append_ptr, key.c_str(), key.size() + 1);
    append_ptr += key.size() + 1;

    // Write length
    *reinterpret_cast<uint64_t *>(append_ptr) = length;
    append_ptr += sizeof(uint64_t);

    // Write value
    memcpy(append_ptr, buffer, length);
    leaf->usage += required_space;
    leaf->entry_count++;
    meta()->kv_entry_count++;
  }
}

whl::vector<char> DiskMap::read(whl::string key, bool &found) {
  // ROOT_PAGE marked as internal with MSB set
  int64_t page = set_msb(ROOT_PAGE, 1);
  int depth = 0;

  while (true) {
    // Check if we've reached a leaf node by checking MSB
    if (!get_msb(page)) {
      LeafNodePage *leaf = leaf_node(page);
      char *entry = find_entry_in_leaf(leaf, key);

      if (!entry) {
        found = false;
        return whl::vector<char>();
      }

      // Skip past the key string
      entry += key.size() + 1;

      // Get the value length
      uint64_t length = *reinterpret_cast<uint64_t *>(entry);
      entry += sizeof(uint64_t);

      // Create vector and copy the value
      whl::vector<char> result(length);
      memcpy(result.data_ptr(), entry, length);
      found = true;
      return result;
    }

    // We're in an internal node
    page = clear_msb(page);
    InternalNodePage *node = internal_node(page);
    uint64_t hash = whl::hash<whl::string>{}(key);
    int64_t bucket = get_bucket(hash, depth);

    if (node->entries[bucket] == -1) {
      found = false;
      return whl::vector<char>();
    }

    page = node->entries[bucket];
    depth++;
  }
}

bool DiskMap::remove(whl::string key) {
  // ROOT_PAGE marked as internal with MSB set
  int64_t page = set_msb(ROOT_PAGE, 1);
  int depth = 0;

  // Stack to keep track of path from root to leaf
  struct PathEntry {
    int64_t page;
    int64_t bucket;
    int64_t *parent_entry_ptr;
  };
  whl::vector<PathEntry> path;
  int64_t *parent_entry_ptr = nullptr;

  while (true) {
    if (!get_msb(page)) {
      // We've reached a leaf node
      LeafNodePage *leaf = leaf_node(page);
      char *entry = find_entry_in_leaf(leaf, key);

      if (!entry) {
        return false;
      }

      // Found the key, now remove it
      char *next_entry = entry + key.size() + 1; // Skip key and null terminator
      uint64_t value_length = *reinterpret_cast<uint64_t *>(next_entry);
      next_entry += sizeof(uint64_t) + value_length;

      // Calculate size of entry being removed
      size_t entry_size = key.size() + 1 + sizeof(uint64_t) + value_length;

      // Shift remaining entries left
      size_t bytes_to_move =
          leaf->usage - (next_entry - static_cast<char *>(&leaf->data));
      if (bytes_to_move > 0) {
        memmove(entry, next_entry, bytes_to_move);
      }
      // Clear the last entry_size bytes starting from the new location of the
      // final entry
      memset(&leaf->data + leaf->usage - entry_size, 0, entry_size);

      leaf->usage -= entry_size;
      leaf->entry_count--;
      meta()->kv_entry_count--;

      // If leaf is now empty, we need to handle unlinking and potential
      // collapse
      //            root                                parent
      // path = [(1, bucket(hash, 0), null), ..., (41, bucket(hash, 1),
      // grandparent_entry_ptr)]
      // path.size() = 1 -> child of root path.size() must be 2 or greater to
      // collapse parent with sibling
      if (leaf->entry_count == 0 && grandparent_entry_ptr != nullptr) {
        // Free the leaf node
        free_page(page, leaf->order);
        *parent_entry_ptr = -1;

          // Check parent's children
        InternalNodePage *parent = internal_node(clear_msb(parent_page));
        int parent_child_count = 0;
        int64_t sibling = -1;

          for (int i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
            if (parent->entries[i] != -1) {
            sibling = parent->entries[i];
            parent_child_count++;
            if (parent_child_count > 1) {
              break;
            }
          }
        }

        if (parent_child_count == 1) {
            // Update grandparent to point to the sibling
          *grandparent_entry_ptr = sibling;

            // Free the parent node
          free_page(clear_msb(parent_page), 0);
        }
      }
      return true;
    }

    // We're in an internal node
    page = clear_msb(page);
    InternalNodePage *node = internal_node(page);
    uint64_t hash = whl::hash<whl::string>{}(key);
    int64_t bucket = get_bucket(hash, depth);

    if (node->entries[bucket] == -1) {
      return false;
    }

    // Save path information
    grandparent_entry_ptr = parent_entry_ptr;
    parent_entry_ptr = &node->entries[bucket];
    parent_page = page;
    page = node->entries[bucket];
    depth++;
  }
}

void DiskMap::debug_dump_recursive(int64_t page, int indent_level,
                                   int parent_index) {
  // Print indentation
  for (int i = 0; i < indent_level; i++) {
    printf("  ");
  }

  // Check if page is an internal node by checking MSB
  if (get_msb(page)) {
    int64_t actual_page = clear_msb(page);
    printf("Page %ld (internal, parent_index=%d)\n", actual_page, parent_index);

    // Recursively process all non-empty entries
    InternalNodePage *node = internal_node(actual_page);
    for (int i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
      if (node->entries[i] != -1) {
        debug_dump_recursive(node->entries[i], indent_level + 1, i);
      }
    }
  } else {
    // Leaf node
    LeafNodePage *leaf = leaf_node(page);
    printf("Page %ld (leaf, parent_index=%d, entry_count=%d, order=%d, "
           "usage=%zu)\n",
           page, parent_index, leaf->entry_count, leaf->order, leaf->usage);

    char *ptr = static_cast<char *>(&leaf->data);
    int entry_count = 0;

    while (ptr < leaf->end()) {
      if (*ptr == '\0') { // Empty key marks end of entries
        break;
      }

      for (int i = 0; i < indent_level + 1; i++) {
        printf("  ");
      }
      printf("Key: %s, ", ptr);
      ptr += strlen(ptr) + 1;
      printf("Value length: %zu\n", *reinterpret_cast<uint64_t *>(ptr));
      ptr += sizeof(uint64_t) + *reinterpret_cast<uint64_t *>(ptr);
      entry_count++;
    }

    // Ensure unused space is zeroed
    while (ptr < leaf->end()) {
      if (*ptr != 0) {
        printf("Leaf corrupted at %lx\n", ptr - static_cast<char *>(mapped));
        break;
      }
      ptr++;
    }
    if (leaf->entry_count != entry_count) {
      printf("Leaf entry count mismatch: %d (recorded) != %d (actual)\n",
             leaf->entry_count, entry_count);
    }
  }
}

void DiskMap::debug_dump() {
  MetaPage *meta = this->meta();
  printf("Page 0 (metadata):\n");
  printf("  kv_entry_count: %ld\n", meta->kv_entry_count);
  printf("  next_free_page: %ld\n", meta->next_free_page);
  debug_dump_recursive(set_msb(ROOT_PAGE, 1), 0, -1);
}

}; // namespace diskmap