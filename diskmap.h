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
  static const char *magic;
  int fd;
  int page_size;
  size_t entries;
  void *mapped;
  size_t num_mapped_pages;

public:
  // Initialize a DiskMap, loading from the given path or creating a new file to
  // back the map at that path
  DiskMap(whl::string path);
  ~DiskMap();

  void remap(size_t num_pages);

  // Sets buffer to the location in (mmaped) memory where the value of the key
  // is found. Returns the length of the value.
  size_t read(whl::string key, void *&buffer);
  void write(whl::string key, void *buffer, size_t length);
};