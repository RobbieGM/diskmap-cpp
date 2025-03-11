#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

void pseudorandom_fill(char *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    data[i] = (i % 251);
  }
}

void compare(const char *expected, const char *actual, size_t length,
             const char *name) {
  for (size_t i = 0; i < length; i++) {
    if (expected[i] != actual[i]) {
      printf("Mismatch at 0x%lx (%zu) in %s\n", i, i, name);
      printf("%02x (expected) != %02x (actual)\n",
             static_cast<unsigned char>(expected[i]),
             static_cast<unsigned char>(actual[i]));
      printf("Context:\n");
      printf("Actual:   ");
      for (int j = -10; j < 10; j++) {
        if (i + j < 0 || i + j >= length) {
          continue;
        }
        printf("%02x ", static_cast<unsigned char>(actual[i + j]));
      }
      printf("\n");
      printf("Expected: ");
      for (int j = -10; j < 10; j++) {
        if (i + j < 0 || i + j >= length) {
          continue;
        }
        printf("%02x ", static_cast<unsigned char>(expected[i + j]));
      }
      printf("\n");
      return;
    }
  }
}

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  diskmap::DiskMap map("test.dm");
  constexpr size_t start_page_capacity = diskmap::LeafNodeStartPage::capacity();
  constexpr size_t continuation_page_capacity = diskmap::PAGE_SIZE - 8;
  constexpr size_t pure_page_capacity = diskmap::PAGE_SIZE;
  constexpr size_t l0 =
      start_page_capacity - 3 - 8; // 3 bytes for key, 8 bytes for value length
  constexpr size_t l1 = l0 + continuation_page_capacity + pure_page_capacity;
  constexpr size_t l2 =
      l1 + continuation_page_capacity + (pure_page_capacity * 3);
  char data[l2];
  pseudorandom_fill(data, l2);
  auto tx = map.begin_rw_transaction();
  tx.write("l0", data, l0);
  tx.write("l1", data, l1);
  tx.write("lx", data, l1 + 1);
  tx.write("l2", data, l2);
  tx.debug_dump();
  bool found{};

  // Test full reads

  auto read_l0 = tx.read("l0", found);
  assert(found);
  compare(data, read_l0.data_ptr(), l0, "l0");

  auto read_l1 = tx.read("l1", found);
  assert(found);
  compare(data, read_l1.data_ptr(), l1, "l1");

  auto read_l1_1 = tx.read("lx", found);
  assert(found);
  compare(data, read_l1_1.data_ptr(), l1 + 1, "l1.1");

  auto read_l2 = tx.read("l2", found);
  assert(found);
  compare(data, read_l2.data_ptr(), l2, "l2");

  // Test partial reads

  auto read_l2_edge_start = tx.read_part("l2", 0, 20, found);
  compare(data + 0, read_l2_edge_start.data_ptr(), 20, "l2_edge_start");

  auto read_l2_boundary_1 = tx.read_part("l2", l0 - 10, 20, found);
  compare(data + l0 - 10, read_l2_boundary_1.data_ptr(), 20, "l2_boundary_1");

  auto read_l2_first_two = tx.read_part("l2", 1, l1, found);
  compare(data + 1, read_l2_first_two.data_ptr(), l1, "l2_first_two");

  auto read_l2_boundary_2 = tx.read_part("l2", l1 - 10, 20, found);
  compare(data + l1 - 10, read_l2_boundary_2.data_ptr(), 20, "l2_boundary_2");

  auto read_l2_edge_end = tx.read_part("l2", l2 - 10, 10, found);
  compare(data + l2 - 10, read_l2_edge_end.data_ptr(), 10, "l2_edge_end");

  tx.commit();
  return 0;
}