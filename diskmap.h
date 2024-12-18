#include <cstddef>
#include <wheel.h>

class DiskMap {
  int fd;
  int page_size;
  size_t entries;

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