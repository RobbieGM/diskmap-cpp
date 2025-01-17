#include "../diskmap.h"
#include <assert.h>
#include <stdio.h>

void test_basic() {
  // Remove test.dm
  remove("test.dm");
  DiskMap map("test.dm");
  printf("test_basic passed!\n");
}

int main() {
  test_basic();
  return 0;
}