#include "../diskmap.h"
#include <assert.h>
#include <cstdio>

int main() {
  remove("test.dm");
  remove("test.dm.wal");
  {
    diskmap::DiskMap map("test.dm");
    const char short_value[] = "short";
    // a, ad, hK hash to [446, ...]
    // b hashes to something else
    auto tx1 = map.begin_transaction();
    tx1.write("a", static_cast<const void *>(short_value), sizeof(short_value));
    tx1.write("ad", static_cast<const void *>(short_value),
              sizeof(short_value));
    tx1.write("b", static_cast<const void *>(short_value), sizeof(short_value));
    bool found;
    tx1.read("a", found);
    assert(found);
    tx1.read("ad", found);
    assert(found);
    tx1.read("b", found);
    assert(found);
    tx1.commit();

    auto tx2 = map.begin_transaction();
    tx2.write("explicit abort", static_cast<const void *>(short_value),
              sizeof(short_value));
    tx2.abort();

    auto tx3 = map.begin_transaction();
    tx3.write("implicit abort", static_cast<const void *>(short_value),
              sizeof(short_value));
  }

  {
    diskmap::DiskMap map("test.dm");
    auto tx = map.begin_transaction();
    bool found;
    tx.read("a", found);
    assert(found);
    tx.read("ad", found);
    assert(found);
    tx.read("b", found);
    assert(found);
    tx.read("explicit abort", found);
    assert(!found);
    tx.read("implicit abort", found);
    assert(!found);

    tx.debug_dump();
  }
  return 0;
}