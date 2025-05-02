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
  for (ssize_t i = 0; i < length; i++) {
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
  auto tx = map.begin_rw_transaction();

  char short_value[300]{0};
  char long_value[3000]{0};
  pseudorandom_fill(short_value, sizeof(short_value));
  pseudorandom_fill(long_value, sizeof(long_value));
  bool found{};

  char expected_a[400]{0};
  // Put another key in the same leaf node to avoid using BigValue semantics
  tx.write("ad", short_value, sizeof(short_value));
  // Write 0-299
  tx.write_part("a", 0, short_value, sizeof(short_value));
  memcpy(expected_a, short_value, sizeof(short_value));
  // Write 100-399
  tx.write_part("a", 100, short_value, sizeof(short_value));
  memcpy(expected_a + 100, short_value, sizeof(short_value));
  auto actual_a = tx.read("a", found);
  assert(found);
  assert(actual_a.size() == 400);
  compare(expected_a, actual_a.data(), sizeof(expected_a), "a");

  char expected_b[5000]{0};
  // Write 0-2999
  tx.write_part("b", 0, long_value, sizeof(long_value));
  memcpy(expected_b, long_value, sizeof(long_value));
  // Write 2000-4999
  tx.write_part("b", 2000, long_value, sizeof(long_value));
  memcpy(expected_b + 2000, long_value, sizeof(long_value));
  auto b = tx.read("b", found);
  assert(found);
  assert(b.size() == 5000);
  compare(expected_b, b.data(), sizeof(expected_b), "b");

  tx.commit();
  return 0;
}