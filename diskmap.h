#include <cstddef>
#include <wheel.h>

class DiskMapException {
public:
  DiskMapException(const char *message) : message_(message) {}
  const char *what() const noexcept { return message_.c_str(); }

private:
  whl::string message_;
};

class DiskMap {
  static const int PAGE_SIZE = 4096;
  static const char *MAGIC;
  int fd;
  void *mapped;
  size_t num_mapped_pages;

  void remap(size_t num_pages);
  void *get_addr(size_t page, int offset);
  size_t *kv_entry_count();
  size_t *next_free_page();
  size_t *dir_entry_count();
  size_t get_split_index();

  size_t allocate_page();

public:
  // Initialize a DiskMap, loading from the given path or creating a new file to
  // back the map at that path
  DiskMap(whl::string path);
  ~DiskMap();

  // Sets buffer to the location in (mmaped) memory where the value of the key
  // is found. Returns the length of the value.
  size_t read(whl::string key, void *&buffer);
  void write(whl::string key, void *buffer, size_t length);
};