#include "../diskmap.h"
#include <cassert>
#include <numeric>

void pseudorandom_fill(char *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    data[i] = (i % 251);
  }
}

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap map("test.dm");
  auto tx = map.begin_rw_transaction();

  // Repeatedly append onto a value (one append will cross a page boundary)
  char repeating_data[251];
  std::iota(repeating_data, repeating_data + 251, 0);
  size_t value_len = 0;
  for (; value_len < 5000; value_len += 251) {
    tx.append("x", repeating_data, 251);
  }
  bool found{};
  auto read_value = tx.read("x", found);
  assert(found);
  assert(read_value.size() == value_len);
  for (size_t i = 0; i < value_len; i++) {
    assert(read_value[i] == repeating_data[i % 251]);
  }

  // Append right after a page boundary
  constexpr size_t start_page_capacity = diskmap::LeafNodeStartPage::capacity();
  constexpr size_t continuation_page_capacity = diskmap::PAGE_SIZE - 8;
  constexpr size_t pure_page_capacity = diskmap::PAGE_SIZE;
  constexpr size_t l0 =
      start_page_capacity - 2 - 8; // 2 bytes for key, 8 bytes for value length
  constexpr size_t l1 = l0 + continuation_page_capacity + pure_page_capacity;
  char data[l1];
  pseudorandom_fill(data, l1);
  tx.append("y", data + 0, l0);
  tx.append("y", data + l0, l1 - l0);
  read_value = tx.read("y", found);
  assert(found);
  assert(read_value.size() == l1);
  for (size_t i = 0; i < l1; i++) {
    assert(read_value[i] == data[i]);
  }

  // Append to values in the same page, eventually causing a split
  for (value_len = 0; value_len < 2500; value_len += 251) {
    tx.append("a", repeating_data, 251);
    tx.append("ad", repeating_data, 251);
  }
  tx.commit();
  read_value = tx.read("a", found);
  assert(found);
  assert(read_value.size() == value_len);
  for (size_t i = 0; i < value_len; i++) {
    assert(read_value[i] == repeating_data[i % 251]);
  }
  auto read_value2 = tx.read("ad", found);
  assert(found);
  assert(read_value2.size() == value_len);
  for (size_t i = 0; i < value_len; i++) {
    assert(read_value2[i] == repeating_data[i % 251]);
  }
  return 0;
}
