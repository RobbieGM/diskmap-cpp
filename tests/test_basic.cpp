#include "../diskmap.h"
#include <assert.h>
#include <stdio.h>

void test_basic() {
  remove("test.dm");
  DiskMap map("test.dm");
  int a = map.allocate_page(0);
  int b = map.allocate_page(1);
  int c = map.allocate_page(0);
  printf("Allocated pages: %d %d %d\n", a, b, c);
  map.free_page(a, 0);
  map.free_page(b, 1);
  map.free_page(c, 0);
  printf("Freed pages: %d %d %d\n", a, b, c);
  a = map.allocate_page(0);
  b = map.allocate_page(1);
  c = map.allocate_page(0);
  printf("Allocated pages: %d %d %d\n", a, b, c);
  printf("test_basic passed!\n");
}

int main() {
  test_basic();
  return 0;
}