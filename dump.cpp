#include <diskmap.h>
#include <unistd.h>

int main(int argc, const char *argv[]) {
  if (argc < 2) {
    printf("Usage: dump <file> [key]\n");
    return 1;
  }

  whl::string path = argv[1];
  diskmap::DiskMap db(path);
  auto txn = db.begin_ro_transaction();
  if (argc == 2) {
    txn.debug_dump();
  } else {
    whl::string key = argv[2];
    bool found{};
    auto value = txn.read(key, found);
    if (!found) {
      printf("Key '%s' not found", key.c_str());
      return 1;
    }
    write(1, value.data_ptr(), value.size());
  }
  return 0;
}