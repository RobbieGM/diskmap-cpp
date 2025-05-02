#include <diskmap.h>
#include <unistd.h>

int main(int argc, const char *argv[]) {
  if (argc < 3) {
    printf("Usage: remove <file> <key>\n");
    return 1;
  }

  std::string path = argv[1];
  diskmap::DiskMap db(path);
  auto txn = db.begin_rw_transaction();
  std::string key = argv[2];
  txn.remove(argv[2]);
  txn.commit();
  return 0;
}
