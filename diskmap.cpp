#include "diskmap.h"
#include "big_value.h"
#include "exception.h"
#include "page_types.h"
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace diskmap {

const char *DiskMap::MAGIC = "DISKMAP";

DiskMap::DiskMap(const whl::string &path) {
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  bool was_created = fd > 0;
  if (!was_created) {
    fd = open(path.c_str(), O_RDWR);
  }
  if (fd < 0) {
    throw DiskMapException("Failed to open file");
  }
  buffer_pool = new BufferPool(fd);
  wal_layer = new WAL(buffer_pool.get(), path + ".wal");

  if (was_created) {
    WAL::RWTransaction tx = wal_layer->begin_rw_transaction();
    // Initialize page 0 (metadata)
    WAL::PageHandle<MetaPage> meta = tx.get_page<MetaPage>(0);
    meta.write(&MetaPage::kv_entry_count, static_cast<int64_t>(0));
    // meta page + root page + (MAX_ORDER + 1) FPL pages
    meta.write(&MetaPage::next_free_page, static_cast<int64_t>(MAX_ORDER + 3));

    // Root page (page 1) is initialized as zeros

    // Initialize all FPL pages
    for (order_t order = 0; order <= MAX_ORDER; order++) {
      meta.write(&MetaPage::last_fpl_page, order,
                 static_cast<int64_t>(order + 2));
      meta.write(&MetaPage::last_fpl_page_entries, order, 0);
      // FPL page at (order + 2) is initialized as zeros
    }

    // Write magic string last in case of incomplete initialization
    meta.write(offsetof(MetaPage, magic), MAGIC, 8);
    tx.commit();
  } else {
    // Check for magic string
    WAL::ROTransaction tx = wal_layer->begin_ro_transaction();
    WAL::ROPageHandle<MetaPage> meta = tx.get_page<MetaPage>(0);
    if (memcmp(meta.ro_data()->magic, MAGIC, 8) != 0) {
      throw DiskMapException("Invalid diskmap file");
    }
  }
}

static int64_t set_msb(int64_t val, int64_t bit) {
  return (val & ~(1LL << 63)) | (bit << 63);
}

static int64_t get_msb(int64_t val) { return val >> 63; }

static int64_t clear_msb(int64_t val) { return val & ~(1LL << 63); }

int DiskMap::get_bucket(whl::string &key, int depth) {
  depth--; // For consistency with fnv_collide.py assuming depth starts at 0
  static int SPLITS_PER_HASH = 8; // = ceil(64 / 9) where 9 = log2(512)
  int hash_function = depth / SPLITS_PER_HASH;
  whl::string *hash_key = &key;
  whl::string alt_key;
  if (hash_function > 0) {
    // Must prepend from the beginning since hashes that collide already cannot
    // be made different by appending characters to the end
    alt_key += static_cast<char>('0' + hash_function);
    alt_key += key;
    hash_key = &alt_key;
  }
  uint64_t hash = hash_key->hash();
  return get_bucket(hash, depth);
}

template <typename Transaction>
auto DiskMap::find_parent(Transaction &t, whl::string &key, int &parent_entry,
                          int &parent_depth) {
  auto parent = t.template get_page<InternalNodePage>(ROOT_PAGE);
  parent_depth = 0;
  parent_entry = 0;
  int64_t parent_entry_value = 0;
  while (true) {
    parent_entry = get_bucket(key, parent_depth + 1);
    parent_entry_value = parent.ro_data()->entries[parent_entry];
    bool points_to_leaf = get_msb(parent_entry_value) == 0;
    if (parent_entry_value == 0 || points_to_leaf) {
      break;
    }
    parent =
        t.template get_page<InternalNodePage>(clear_msb(parent_entry_value));
    parent_depth++;
  }
  return parent;
}

// Explicit template instantiations for find_parent
template auto DiskMap::find_parent<WAL::RWTransaction>(WAL::RWTransaction &t,
                                                       whl::string &key,
                                                       int &parent_entry,
                                                       int &parent_depth);
template auto DiskMap::find_parent<WAL::ROTransaction>(WAL::ROTransaction &t,
                                                       whl::string &key,
                                                       int &parent_entry,
                                                       int &parent_depth);

int DiskMap::get_bucket(uint64_t hash, int depth) {
  // Depth argument for get bucket refers to depth of internal node that is the
  // parent of the node to be found
  // INNER_NODE_BRANCHING_FACTOR = 512 (for example)
  // Suppose hash = x + y * 512 + z * 512 * 512 + ...
  // If depth = 0, return x, if depth = 1, return y, etc.

  while (depth > 0) {
    hash /= InternalNodePage::BRANCHING_FACTOR;
    depth--;
  }
  return hash % InternalNodePage::BRANCHING_FACTOR;
}

int DiskMap::find_entry_in_leaf(const LeafNodeStartPage *leaf,
                                const whl::string &key) {
  const char *start = static_cast<const char *>(leaf->data);
  const char *ptr = start;

  while (ptr < end_of(leaf)) {
    if (*ptr == '\0') { // Empty key marks end of entries
      return -1;
    }

    if (strncmp(ptr, key.c_str(), key.size()) == 0 && ptr[key.size()] == '\0') {
      return ptr - start;
    }

    // Skip past current entry
    size_t key_size = strlen(ptr) + 1;
    ptr += key_size;

    uint64_t value_length = *reinterpret_cast<const uint64_t *>(ptr);
    ptr += sizeof(uint64_t) + value_length;
  }
  return -1;
}

whl::vector<KVEntry>
DiskMap::get_entries_in_leaf(const LeafNodeStartPage *leaf) {
  whl::vector<KVEntry> result;
  const char *ptr = static_cast<const char *>(leaf->data);

  while (ptr < end_of(leaf)) {
    if (*ptr == '\0') { // Empty key marks end of entries
      break;
    }

    KVEntry entry;
    entry.key = ptr;
    ptr += entry.key.size() + 1;
    uint64_t value_length = *reinterpret_cast<const uint64_t *>(ptr);
    entry.value.resize(value_length);
    ptr += sizeof(uint64_t);
    memcpy(entry.value.data_ptr(), ptr, value_length);
    ptr += value_length;
    result.push_back(whl::move(entry));
  }
  return result;
}

whl::vector<KVEntry> DiskMap::sample(WAL::ROTransaction &t, size_t count) {
  whl::vector<int64_t> pages;
  whl::vector<KVEntry> result;
  pages.push_back(set_msb(ROOT_PAGE, 1));
  while (pages.size() > 0 && result.size() < count) {
    size_t idx = rand() % pages.size();
    int64_t page = pages[idx];
    pages[idx] = pages.back();
    pages.pop_back();
    if (get_msb(page)) {
      // Internal node
      WAL::ROPageHandle<InternalNodePage> internal =
          t.get_page<InternalNodePage>(clear_msb(page));
      for (int i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
        int64_t child = internal.ro_data()->entries[i];
        if (child != 0) {
          pages.push_back(child);
        }
      }
    } else {
      // Leaf node
      WAL::ROPageHandle<LeafNodeStartPage> leaf =
          t.get_page<LeafNodeStartPage>(clear_msb(page));
      if (leaf.ro_data()->next == 0) {
        whl::vector<KVEntry> entries = get_entries_in_leaf(leaf.ro_data());
        for (size_t i = 0; i < entries.size() && result.size() < count; i++) {
          result.push_back(whl::move(entries[i]));
        }
      }
      // Sampling from big leaf nodes is not supported
    }
  }
  return result;
}

void DiskMap::create_subtree(WAL::RWTransaction &t,
                             WAL::PageHandle<InternalNodePage> &parent,
                             int parent_entry, int parent_depth,
                             const whl::vector<KVEntry> &entries) {
  size_t total_entries_size = 0;
  for (size_t i = 0; i < entries.size(); i++) {
    total_entries_size +=
        entries[i].key.size() + 1 + sizeof(uint64_t) + entries[i].value.size();
  }
  int64_t new_parent_entry = 0;
  if (total_entries_size <= LeafNodeStartPage::capacity()) {
    // Use one leaf node
    int64_t new_leaf_page_number = SpaceManager::allocate(t, 0);
    WAL::PageHandle<LeafNodeStartPage> new_leaf =
        t.get_page<LeafNodeStartPage>(new_leaf_page_number);
    new_leaf.write(&LeafNodeStartPage::usage, total_entries_size);
    new_leaf.write(&LeafNodeStartPage::entry_count,
                   static_cast<uint16_t>(entries.size()));

    int offset = 0;
    char entries_buffer[total_entries_size];
    for (size_t i = 0; i < entries.size(); i++) {
      // Write key into entries_buffer
      memcpy(entries_buffer + offset, entries[i].key.c_str(),
             entries[i].key.size() + 1);
      offset += entries[i].key.size() + 1;

      // Write value length into entries_buffer
      uint64_t value_length = entries[i].value.size();
      memcpy(entries_buffer + offset, &value_length, sizeof(uint64_t));
      offset += sizeof(uint64_t);

      // Write value into entries_buffer
      memcpy(entries_buffer + offset, entries[i].value.data_ptr(),
             entries[i].value.size());
      offset += entries[i].value.size();
    }

    // Write entries_buffer all at once
    new_leaf.write(&LeafNodeStartPage::data, 0, total_entries_size,
                   entries_buffer);

    // Replace parent entry with pointer to new leaf
    new_parent_entry = set_msb(new_leaf_page_number, 0);
  } else if (entries.size() == 1) {
    // Use one leaf node with linked list of continuation pages
    int64_t new_leaf_start_page_number = SpaceManager::allocate(t, 0);
    WAL::PageHandle<LeafNodeStartPage> new_leaf_start =
        t.get_page<LeafNodeStartPage>(new_leaf_start_page_number);
    new_leaf_start.write(&LeafNodeStartPage::usage, total_entries_size);
    new_leaf_start.write(&LeafNodeStartPage::entry_count,
                         static_cast<uint16_t>(1));

    // Write key and value length
    char buf[entries[0].key.size() + 1 + sizeof(uint64_t)];
    memcpy(buf, entries[0].key.c_str(), entries[0].key.size() + 1);
    uint64_t value_length = entries[0].value.size();
    memcpy(buf + entries[0].key.size() + 1, &value_length, sizeof(uint64_t));
    new_leaf_start.write(&LeafNodeStartPage::data, 0, sizeof(buf), buf);

    // Write value
    BigValue bv(&t, new_leaf_start_page_number,
                entries[0].key.size() + 1 + sizeof(uint64_t));
    bv.write(0, entries[0].value.data_ptr(), entries[0].value.size());

    new_parent_entry = set_msb(new_leaf_start_page_number, 0);
  } else {
    // If entries cannot fit in a leaf and there is more than one, create
    // internal node
    whl::vector<KVEntry> entries_by_hash[InternalNodePage::BRANCHING_FACTOR];
    for (size_t i = 0; i < entries.size(); i++) {
      entries_by_hash[get_bucket(entries[i].key.hash(), parent_depth + 1)]
          .push_back(entries[i]);
    }

    int64_t new_internal_node_page_number = SpaceManager::allocate(t, 0);
    WAL::PageHandle<InternalNodePage> new_internal_node =
        t.get_page<InternalNodePage>(new_internal_node_page_number);
    for (size_t i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
      if (entries_by_hash[i].size() > 0) {
        create_subtree(t, new_internal_node, i, parent_depth + 1,
                       entries_by_hash[i]);
      }
    }

    new_parent_entry = set_msb(new_internal_node_page_number, 1);
  }

  // Free old leaf node
  int64_t parent_entry_value = parent.ro_data()->entries[parent_entry];
  if (parent_entry_value != 0) {
    int64_t parent_entry_page = clear_msb(parent_entry_value);
    bool parent_entry_points_to_internal_node = get_msb(parent_entry_value);
    if (parent_entry_points_to_internal_node) {
      throw DiskMapException(
          "create_subtree: parent entry points to internal node");
    }

    free_leaf(t, parent_entry_page);
  }

  // Update parent entry
  parent.write(&InternalNodePage::entries, parent_entry, new_parent_entry);
}

void DiskMap::update_value_trivially(WAL::PageHandle<LeafNodeStartPage> &leaf,
                                     int entry_offset, const whl::string &key,
                                     const void *buffer, size_t length) {
  int value_length_offset = entry_offset + key.size() + 1;
  uint64_t current_value_length = *reinterpret_cast<const uint64_t *>(
      leaf.ro_data()->data + value_length_offset);
  int value_offset = value_length_offset + sizeof(uint64_t);

  if (leaf.ro_data()->usage + length - current_value_length >
      LeafNodeStartPage::capacity()) {
    throw DiskMapException("update_value_trivially: updated leaf node "
                           "exceeds maximum capacity, update is not trivial");
  }

  if (length == current_value_length) {
    // Update only the value
    leaf.write(&LeafNodeStartPage::data, value_offset, length,
               static_cast<const char *>(buffer));
    return;
  }

  bool growing = length > current_value_length;
  // Update value length, value, and the rest of the page (shift right if
  // growing, else left)
  // Update range: value_length_offset to last byte on page + (growing ? length
  // - current_value_length : 0)
  int update_start = value_length_offset;
  int update_end =
      leaf.ro_data()->usage + (growing ? length - current_value_length : 0);
  char update_buffer[update_end - update_start];
  memcpy(update_buffer, leaf.ro_data()->data + update_start,
         update_end - update_start);

  int next_entry_offset = value_offset + current_value_length;
  int new_next_entry_offset = value_offset + length;
  int bytes_to_move = leaf.ro_data()->usage - next_entry_offset;
  // Move subsequent entries left or right to be adjacent to reduced or expanded
  // size KV pair
  memmove(update_buffer - update_start + new_next_entry_offset,
          update_buffer - update_start + next_entry_offset, bytes_to_move);

  // Set new value length
  *reinterpret_cast<uint64_t *>(update_buffer - update_start +
                                value_length_offset) = length;

  // Set new value
  memcpy(update_buffer - update_start + value_offset, buffer, length);

  leaf.write(&LeafNodeStartPage::usage,
             leaf.ro_data()->usage + length - current_value_length);
  leaf.write(&LeafNodeStartPage::data, update_start, update_end - update_start,
             update_buffer);
}

void DiskMap::free_leaf(WAL::RWTransaction &t, int64_t page_number) {
  // Relies on LeafNodeContinuationPage and LeafNodeStartPage sharing layout
  int order = 0;
  while (true) {
    auto page = t.get_page<LeafNodeContinuationPage>(page_number);
    SpaceManager::free(t, page_number, order++);
    if (page.ro_data()->next == 0)
      break;
    page_number = page.ro_data()->next;
  }
}

void DiskMap::write(WAL::RWTransaction &t, whl::string &key, const void *buffer,
                    size_t length) {
  if (key.size() == 0 || buffer == nullptr) {
    throw DiskMapException("write: invalid input parameters");
  }

  int parent_entry = 0;
  int parent_depth = 0;
  WAL::PageHandle<InternalNodePage> parent =
      find_parent(t, key, parent_entry, parent_depth);

  write_at_node(t, whl::move(parent), parent_entry, parent_depth, key, buffer,
                length);
}

void DiskMap::write_at_node(WAL::RWTransaction &t,
                            WAL::PageHandle<InternalNodePage> parent,
                            int parent_entry, int parent_depth,
                            whl::string &key, const void *buffer,
                            size_t length) {
  int64_t parent_entry_value = parent.ro_data()->entries[parent_entry];
  if (parent_entry_value == 0) {
    // Need to create subtree
    whl::vector<KVEntry> entries;
    entries.push_back(KVEntry(
        key, whl::vector<char>(static_cast<const char *>(buffer), length)));
    create_subtree(t, parent, parent_entry, parent_depth, entries);
    auto meta = t.get_page<MetaPage>(0);
    meta.write(&MetaPage::kv_entry_count, meta.ro_data()->kv_entry_count + 1);
    return;
  }
  int64_t leaf_page_number = clear_msb(parent_entry_value);
  WAL::PageHandle<LeafNodeStartPage> leaf =
      t.get_page<LeafNodeStartPage>(leaf_page_number);

  int entry_offset = find_entry_in_leaf(leaf.ro_data(), key);

  if (entry_offset != -1) {
    // Update existing entry
    if (leaf.ro_data()->next != 0) {
      // Remove old multi-region value
      free_leaf(t, leaf.get_page());
      parent.write(&InternalNodePage::entries, parent_entry,
                   static_cast<int64_t>(0));
    }
    int value_length_offset = entry_offset + key.size() + 1;
    size_t current_value_length = *reinterpret_cast<const uint64_t *>(
        leaf.ro_data()->data + value_length_offset);
    if (leaf.ro_data()->usage + length - current_value_length >
        LeafNodeStartPage::capacity()) {
      // Rebuild tree
      auto entries = get_entries_in_leaf(leaf.ro_data());
      for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].key == key) {
          entries[i].value.resize(length);
          memcpy(entries[i].value.data_ptr(), buffer, length);
          break;
        }
      }
      create_subtree(t, parent, parent_entry, parent_depth, entries);
    } else {
      update_value_trivially(leaf, entry_offset, key, buffer, length);
    }
  } else {
    // Add a new entry

    auto meta = t.get_page<MetaPage>(0);
    meta.write(&MetaPage::kv_entry_count, meta.ro_data()->kv_entry_count + 1);

    size_t entry_size = key.size() + 1 + sizeof(uint64_t) + length;

    if (leaf.ro_data()->usage + entry_size <= LeafNodeStartPage::capacity()) {
      // Just append the new key-value pair to the leaf
      // Update range: append_ptr to append_ptr + key size + null term + 8 +
      // value size

      int update_start = leaf.ro_data()->usage;
      char update_buffer[entry_size];
      char *append_ptr = update_buffer;

      // Write key
      memcpy(update_buffer, key.c_str(), key.size() + 1);
      append_ptr += key.size() + 1;

      // Write length
      *reinterpret_cast<uint64_t *>(append_ptr) = length;
      append_ptr += sizeof(uint64_t);

      // Write value
      memcpy(append_ptr, buffer, length);

      leaf.write(&LeafNodeStartPage::data, update_start, entry_size,
                 update_buffer);
      leaf.write(&LeafNodeStartPage::usage, leaf.ro_data()->usage + entry_size);
      leaf.write(&LeafNodeStartPage::entry_count,
                 static_cast<uint16_t>(leaf.ro_data()->entry_count + 1));
      return;
    }

    // Leaf needs to be split
    if (leaf.ro_data()->entry_count == 1) {
      // Create new internal node(s) pointing to original leaf and a new leaf
      // containing the new key-value pair.
      whl::string existing_key = get_entries_in_leaf(leaf.ro_data())[0].key;
      // Keep creating internal nodes until the buckets don't collide anymore
      while (true) {
        int new_bucket = get_bucket(key, parent_depth + 1);
        int existing_key_bucket = get_bucket(existing_key, parent_depth + 1);
        if (existing_key_bucket == new_bucket) {
          // Create another internal node
          int64_t new_internal_page_number = SpaceManager::allocate(t, 0);
          WAL::PageHandle<InternalNodePage> new_internal =
              t.get_page<InternalNodePage>(new_internal_page_number);
          parent.write(&InternalNodePage::entries, new_bucket,
                       set_msb(new_internal_page_number, 1));
          parent = whl::move(new_internal);
          parent_depth++;
        } else {
          // Link parent to existing leaf
          parent.write(&InternalNodePage::entries, existing_key_bucket,
                       set_msb(leaf_page_number, 0));

          // Create new leaf
          whl::vector<KVEntry> new_entries;
          new_entries.push_back(
              KVEntry(key, whl::vector<char>(static_cast<const char *>(buffer),
                                             length)));
          create_subtree(t, parent, new_bucket, parent_depth, new_entries);
          break;
        }
      }
    } else {
      // Rebuild tree
      auto entries = get_entries_in_leaf(leaf.ro_data());
      entries.push_back({key, whl::vector<char>(length)});
      entries.back().value.resize(length);
      memcpy(entries.back().value.data_ptr(), buffer, length);

      create_subtree(t, parent, parent_entry, parent_depth, entries);
    }
  }
}

void DiskMap::append(WAL::RWTransaction &t, whl::string &key, void *buffer,
                     size_t append_length) {
  write_part(t, key, -1UL, buffer, append_length);
}

void DiskMap::write_part(WAL::RWTransaction &t, whl::string &key,
                         size_t write_offset, const void *buffer,
                         size_t write_length) {
  int parent_entry = 0;
  int parent_depth = 0;
  WAL::PageHandle<InternalNodePage> parent =
      find_parent(t, key, parent_entry, parent_depth);
  int64_t parent_entry_value = parent.ro_data()->entries[parent_entry];

  auto key_not_found = [&]() {
    const void *true_buffer = buffer;
    if (write_offset == -1UL) {
      write_offset = 0;
    }
    if (write_offset != 0) {
      // Prepend 0s before offset
      void *buf = alloca(write_offset + write_length);
      memset(buf, 0, write_offset);
      memcpy(reinterpret_cast<char *>(buf) + write_offset, buffer,
             write_length);
      true_buffer = buf;
    }
    write_at_node(t, whl::move(parent), parent_entry, parent_depth, key,
                  true_buffer, write_length);
  };
  if (parent_entry_value == 0) {
    key_not_found();
    return;
  }
  int64_t leaf_page_number = clear_msb(parent_entry_value);
  WAL::PageHandle<LeafNodeStartPage> leaf =
      t.get_page<LeafNodeStartPage>(leaf_page_number);

  int entry_offset = find_entry_in_leaf(leaf.ro_data(), key);
  if (entry_offset == -1) {
    key_not_found();
    return;
  }

  // Key exists
  int value_length_offset = entry_offset + key.size() + 1;
  uint64_t existing_length = *reinterpret_cast<const uint64_t *>(
      leaf.ro_data()->data + value_length_offset);
  if (write_offset == -1UL) {
    write_offset = existing_length; // Append
  }
  int data_section_offset = value_length_offset + sizeof(uint64_t);
  size_t new_value_length =
      whl::max(existing_length, write_offset + write_length);

  if (leaf.ro_data()->entry_count == 1) {
    // Use big value approach
    BigValue bv(&t, leaf.get_page(), data_section_offset);
    bv.write(write_offset, static_cast<const char *>(buffer), write_length);

    if (new_value_length != existing_length) {
      char buffer[8];
      memcpy(buffer, &new_value_length, 8);
      leaf.write(&LeafNodeStartPage::data, value_length_offset, 8, buffer);
      leaf.write(&LeafNodeStartPage::usage,
                 leaf.ro_data()->usage + new_value_length -
                     existing_length); // Length cannot decrease
    }
  } else {
    // Read existing value, append, and rewrite
    whl::vector<char> new_value(new_value_length);
    memcpy(new_value.data_ptr(), leaf.ro_data()->data + data_section_offset,
           existing_length);
    memcpy(new_value.data_ptr() + write_offset, buffer, write_length);

    write_at_node(t, whl::move(parent), parent_entry, parent_depth, key,
                  new_value.data_ptr(), new_value.size());
  }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
whl::vector<char> DiskMap::read(WAL::ROTransaction &t, whl::string &key,
                                bool &found) {
  return read_part(t, key, 0, -1UL, found);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
whl::vector<char> DiskMap::read_part(WAL::ROTransaction &t, whl::string &key,
                                     size_t read_offset, size_t read_length,
                                     bool &found) {
  int parent_entry{};
  int parent_depth{};
  auto parent = find_parent(t, key, parent_entry, parent_depth);
  int64_t parent_entry_value = parent.ro_data()->entries[parent_entry];

  if (get_msb(parent_entry_value)) {
    found = false;
    return whl::vector<char>();
  }

  WAL::ROPageHandle<LeafNodeStartPage> leaf =
      t.get_page<LeafNodeStartPage>(clear_msb(parent_entry_value));
  int entry_offset = find_entry_in_leaf(leaf.ro_data(), key);

  if (entry_offset == -1) {
    found = false;
    return whl::vector<char>();
  }

  int offset = entry_offset + key.size() + 1;
  uint64_t length =
      *reinterpret_cast<const uint64_t *>(leaf.ro_data()->data + offset);
  read_length = whl::min(read_length, length - read_offset);
  offset += sizeof(uint64_t);

  whl::vector<char> buffer(read_length);
  if (leaf.ro_data()->next != 0) {
    BigValue bv(&t, leaf.get_page(), offset);
    bv.read(read_offset, buffer.data_ptr(), read_length);
  } else {
    memcpy(buffer.data_ptr(), leaf.ro_data()->data + offset + read_offset,
           read_length);
  }

  found = true;
  return buffer;
}

bool DiskMap::remove(WAL::RWTransaction &t, whl::string &key) {
  // ROOT_PAGE marked as internal with MSB set
  WAL::PageHandle<InternalNodePage> parent =
      t.get_page<InternalNodePage>(ROOT_PAGE);
  WAL::PageHandle<InternalNodePage> grandparent =
      t.get_page<InternalNodePage>(ROOT_PAGE);
  int parent_entry = -1;
  int64_t parent_entry_value = 0;
  int grandparent_entry = -1;
  int depth = 1;
  while (true) {
    grandparent_entry = parent_entry;
    parent_entry = get_bucket(key, depth);
    parent_entry_value = parent.ro_data()->entries[parent_entry];

    if (parent_entry_value == 0) {
      return false;
    }

    depth++;
    if (get_msb(parent_entry_value)) {
      // The next node is an internal node
      grandparent = whl::move(parent);
      parent = t.get_page<InternalNodePage>(clear_msb(parent_entry_value));
    } else {
      // The next node is a leaf node
      break;
    }
  }

  // Found candidate leaf node
  WAL::PageHandle<LeafNodeStartPage> leaf =
      t.get_page<LeafNodeStartPage>(parent_entry_value);
  int entry_offset = find_entry_in_leaf(leaf.ro_data(), key);

  if (entry_offset == -1) {
    return false;
  }

  if (leaf.ro_data()->entry_count == 1) {
    // Free the leaf node - it will be cleared when reallocated
    free_leaf(t, leaf.get_page());

    // Update parent
    parent.write(&InternalNodePage::entries, parent_entry,
                 static_cast<int64_t>(0));

    // If the leaf node has exactly one sibling, free its parent
    if (grandparent_entry != -1) {
      // Check parent's children
      int sibling_count = 0;
      int64_t sibling_entry_value = 0;

      for (int i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
        if (parent.ro_data()->entries[i] != 0) {
          sibling_entry_value = parent.ro_data()->entries[i];
          sibling_count++;
          if (sibling_count > 1) {
            break;
          }
        }
      }

      if (sibling_count == 1) {
        // Update grandparent to point to the sibling
        grandparent.write(&InternalNodePage::entries, grandparent_entry,
                          sibling_entry_value);

        // Free the parent node
        SpaceManager::free(t, parent.get_page(), 0);
      }
    }
  } else {
    // Shift remaining entries left
    uint64_t value_length = *reinterpret_cast<const uint64_t *>(
        leaf.ro_data()->data + entry_offset + key.size() + 1);
    size_t entry_size = key.size() + 1 + sizeof(uint64_t) + value_length;
    int next_entry_offset = entry_offset + entry_size;
    size_t bytes_to_move = leaf.ro_data()->usage - next_entry_offset;

    // Move bytes_to_move bytes from next_entry_offset left by entry_size
    char write_buffer[bytes_to_move + entry_size];
    memcpy(write_buffer, leaf.ro_data()->data + next_entry_offset,
           bytes_to_move); // Leave last entry_size bytes at 0
    leaf.write(&LeafNodeStartPage::data, entry_offset,
               bytes_to_move + entry_size, write_buffer);

    // Update usage and entry count
    leaf.write(&LeafNodeStartPage::usage, leaf.ro_data()->usage - entry_size);
    leaf.write(&LeafNodeStartPage::entry_count,
               static_cast<uint16_t>(leaf.ro_data()->entry_count - 1));
  }

  // Decrement kv_entry_count
  WAL::PageHandle<MetaPage> meta = t.get_page<MetaPage>(0);
  meta.write(&MetaPage::kv_entry_count, meta.ro_data()->kv_entry_count - 1);

  return true;
}

void DiskMap::debug_dump(WAL::ROTransaction &t) {
  WAL::ROPageHandle<MetaPage> meta = t.get_page<MetaPage>(0);
  printf("Page 0 (metadata):\n");
  printf("  kv_entry_count: %ld\n", meta.ro_data()->kv_entry_count);
  printf("  next_free_page: %ld\n", meta.ro_data()->next_free_page);
  debug_dump_recursive(t, set_msb(ROOT_PAGE, 1), 0, -1);
}

void DiskMap::debug_dump_recursive(WAL::ROTransaction &t, int64_t page,
                                   int indent_level, int parent_index) {
  // Print indentation
  for (int i = 0; i < indent_level; i++) {
    printf("  ");
  }

  // Check if page is an internal node by checking MSB
  if (get_msb(page)) {
    int64_t actual_page = clear_msb(page);
    printf("Page 0x%lx (internal, parent_index=%d)\n", actual_page,
           parent_index);

    // Recursively process all non-empty entries
    WAL::ROPageHandle<InternalNodePage> node =
        t.get_page<InternalNodePage>(actual_page);
    for (int i = 0; i < InternalNodePage::BRANCHING_FACTOR; i++) {
      if (node.ro_data()->entries[i] != 0) {
        debug_dump_recursive(t, node.ro_data()->entries[i], indent_level + 1,
                             i);
      }
    }
  } else {
    // Leaf node
    WAL::ROPageHandle<LeafNodeStartPage> leaf =
        t.get_page<LeafNodeStartPage>(page);
    printf("Page 0x%lx (leaf, parent_index=%d, entry_count=%d, "
           "usage=%zu)\n",
           page, parent_index, leaf.ro_data()->entry_count,
           leaf.ro_data()->usage);

    const char *ptr = static_cast<const char *>(leaf.ro_data()->data);
    int entry_count = 0;

    while (ptr < end_of(leaf.ro_data())) {
      if (*ptr == '\0') { // Empty key marks end of entries
        break;
      }

      for (int i = 0; i < indent_level + 1; i++) {
        printf("  ");
      }
      printf("Key: %s, ", ptr);
      ptr += strlen(ptr) + 1;
      printf("Value length: %zu\n", *reinterpret_cast<const uint64_t *>(ptr));
      ptr += sizeof(uint64_t) + *reinterpret_cast<const uint64_t *>(ptr);
      entry_count++;
    }

    // Ensure unused space is zeroed
    while (ptr < end_of(leaf.ro_data())) {
      if (*ptr != 0) {
        printf("Leaf corrupted at %lx\n",
               ptr - static_cast<const char *>(leaf.ro_data()->data));
        break;
      }
      ptr++;
    }
    if (leaf.ro_data()->entry_count != entry_count) {
      printf("Leaf entry count mismatch: %d (recorded) != %d (actual)\n",
             leaf.ro_data()->entry_count, entry_count);
    }
  }
}

DiskMap::RWTransaction DiskMap::begin_rw_transaction() {
  return RWTransaction(this);
}

DiskMap::ROTransaction DiskMap::begin_ro_transaction() {
  return ROTransaction(this);
}

// DiskMap::ROTransaction

DiskMap::ROTransaction::ROTransaction(DiskMap *dm,
                                      whl::unique_ptr<WAL::ROTransaction> tx,
                                      whl::rw_mutex_guard::Mode mode)
    : dm(dm), tx(whl::move(tx)), guard(&dm->global_lock, mode) {}

DiskMap::ROTransaction::ROTransaction(DiskMap *dm)
    : ROTransaction(dm,
                    whl::unique_ptr<WAL::ROTransaction>(
                        whl::move(dm->wal_layer->begin_ro_transaction())),
                    whl::rw_mutex_guard::Mode::READ) {}

static whl::unique_ptr<WAL::ROTransaction>
ro_cast(whl::unique_ptr<WAL::RWTransaction> tx) {
  return whl::unique_ptr<WAL::ROTransaction>(tx.release());
}

whl::vector<char> DiskMap::ROTransaction::read_part(whl::string key,
                                                    size_t offset,
                                                    size_t length,
                                                    bool &found) {
  return dm->read_part(*tx, key, offset, length, found);
}

whl::vector<char> DiskMap::ROTransaction::read(whl::string key, bool &found) {
  return dm->read(*tx, key, found);
}

whl::vector<KVEntry> DiskMap::ROTransaction::sample(size_t count) {
  return dm->sample(*tx, count);
}

void DiskMap::ROTransaction::debug_dump() { dm->debug_dump(*tx); }

// DiskMap::Transaction

DiskMap::RWTransaction::RWTransaction(DiskMap *dm)
    : ROTransaction(dm,
                    ro_cast(whl::move(dm->wal_layer->begin_rw_transaction())),
                    whl::rw_mutex_guard::Mode::WRITE) {}

void DiskMap::RWTransaction::commit() {
  dynamic_cast<WAL::RWTransaction *>(tx.get())->commit();
}

void DiskMap::RWTransaction::abort() {
  dynamic_cast<WAL::RWTransaction *>(tx.get())->abort();
}

void DiskMap::RWTransaction::write(whl::string key, const void *buffer,
                                   size_t length) {
  dm->write(*dynamic_cast<WAL::RWTransaction *>(tx.get()), key, buffer, length);
}

void DiskMap::RWTransaction::write_part(whl::string key, size_t offset,
                                        const void *buffer, size_t length) {
  dm->write_part(*dynamic_cast<WAL::RWTransaction *>(tx.get()), key, offset,
                 buffer, length);
}

void DiskMap::RWTransaction::append(whl::string key, void *buffer,
                                    size_t length) {
  dm->append(*dynamic_cast<WAL::RWTransaction *>(tx.get()), key, buffer,
             length);
}

bool DiskMap::RWTransaction::remove(whl::string key) {
  return dm->remove(*dynamic_cast<WAL::RWTransaction *>(tx.get()), key);
}

}; // namespace diskmap
