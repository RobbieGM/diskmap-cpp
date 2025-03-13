#include "../buffer_pool.h"
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

void test_bufferpool_basic_operations() {
  remove("test.dm");
  int fd = open("test.dm", O_RDWR | O_CREAT, 0666);
  assert(fd >= 0);

  diskmap::BufferPool bufferPool(fd, 10);

  // Allocate and write pages
  auto page1 = bufferPool.get_page(0);
  strcpy(page1.data(), "Test Data 1");
  page1.modified_by(1);

  auto page2 = bufferPool.get_page(1);
  strcpy(page2.data(), "Test Data 2");
  page2.modified_by(2);

  bufferPool.flush_all();

  // Re-open the file and read back the data
  int fd2 = open("test.dm", O_RDWR);
  assert(fd2 >= 0);
  diskmap::BufferPool bufferPool2(fd2, 10);

  auto readPage1 = bufferPool2.get_page(0);
  auto readPage2 = bufferPool2.get_page(1);

  assert(strcmp(readPage1.data(), "Test Data 1") == 0);
  assert(strcmp(readPage2.data(), "Test Data 2") == 0);

  std::cout << "test_bufferpool_basic_operations passed!\n";
  close(fd);
  close(fd2);
}

void test_bufferpool_persistence() {
  remove("test.dm");
  int fd = open("test.dm", O_RDWR | O_CREAT, 0666);
  assert(fd >= 0);
  diskmap::BufferPool bufferPool(fd, 10);

  auto page1 = bufferPool.get_page(0);
  strcpy(page1.data(), "Persisted Data");
  page1.modified_by(1);

  bufferPool.flush_all();
  close(fd);

  // Re-open and verify persistence
  int fd2 = open("test.dm", O_RDWR);
  assert(fd2 >= 0);
  diskmap::BufferPool bufferPool2(fd2, 10);

  auto readPage1 = bufferPool2.get_page(0);
  assert(strcmp(readPage1.data(), "Persisted Data") == 0);

  std::cout << "test_bufferpool_persistence passed!\n";
  close(fd2);
}

int main() {
  test_bufferpool_basic_operations();
  test_bufferpool_persistence();
  return 0;
}