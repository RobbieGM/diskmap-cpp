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
  static const int FPL_PAGE_CAPACITY = PAGE_SIZE / 8 - 2;
  static const char *MAGIC;
  int fd;
  void *mapped;
  int64_t num_mapped_pages;

  void remap(int64_t num_pages);
  void *get_addr(int64_t page, int offset);
  int64_t *kv_entry_count();
  int64_t *next_free_page();
  int64_t *last_fpl_page();
  // index into last page in FPL list (last non-empty entry), or -1 if empty
  int64_t *last_fpl_page_entries();

  void fpl_init(int64_t page);
  int64_t *fpl_previous(int64_t page);
  int64_t *fpl_next(int64_t page);
  int64_t *fpl_entry(int64_t page, int64_t index);

  // Allocate a new page. May contain undefined data
  int64_t allocate_page();
  void free_page(int64_t page);

public:
  // Initialize a DiskMap, loading from the given path or creating a new file to
  // back the map at that path
  DiskMap(whl::string path);
  ~DiskMap();

  // Sets buffer to the location in (mmaped) memory where the value of the key
  // is found. Returns the length of the value.
  int64_t read(whl::string key, void *&buffer);
  void write(whl::string key, void *buffer, int64_t length);
};