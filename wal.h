#pragma once

#include "bufferpool.h"
#include "checksum.h"
#include "diskmap.h"

namespace diskmap {

enum class WALRecordType : uint8_t {
  BEGIN,
  COMMIT,
  ABORT_BEGIN,
  ABORT_END,
  CHECKPOINT,
  SET,
  CLR
};

struct WALCommonHeader {
  uint32_t checksum;
  uint64_t lsn;
  uint64_t txn_id;
  WALRecordType type;
} __attribute__((packed));

// Represents setting a range of bytes to other bytes.
// Sometimes, many bytes for the original or new value are all 0x00.
// If these occur at the end of the range, they are considered fill bytes.
struct SetRecord {
  uint64_t loc;           // Starting address of the range
  uint32_t length;        // Total number of bytes that are set
  uint32_t old_value_len; // Number of non-fill bytes that have been overwritten
  uint32_t new_value_len; // Number of non-fill bytes making up the new value
  // Next bytes: first old_value_len bytes of the original value, then
  // new_value_len bytes of the new value
} __attribute__((packed));

// Represents undoing a SetRecord.
struct CompensationRecord {
  uint64_t undo_lsn;
  uint64_t loc;
  uint32_t length;
  uint32_t new_value_len;
  // Next bytes: new_value_len bytes of the restored value
} __attribute__((packed));

// All data that appears in a WAL record, before any variable-length data.
struct WALHeader {
  WALCommonHeader common_header;
  union {
    SetRecord set;
    // CopyRecord copy;
    CompensationRecord compensation;
  };
} __attribute__((packed));

struct InMemoryWALRecord {
  WALHeader header;
  whl::vector<char> data; // Any variable-length data
};

// A physical write-ahead logging layer, guaranteeing transaction atomicity and
// durability.
class WAL : AbstractWAL {
  BufferPool *pool;
  int wal_fd;
  uint64_t next_lsn = 0;
  uint64_t next_txn_id = 0;
  whl::mutex wal_mutex;
  size_t log_pos = 0;
  whl::vector<InMemoryWALRecord> records;
  size_t unflushed_record_index = 0; // Index into records of the first
                                     // unflushed record
  int transactions_since_last_checkpoint = 0;
  int active_transactions = 0; // Number of currently running transactions. Not
                               // updated when recovering
  bool checkpoint_pending = false;
  whl::thread checkpointing_thread;
  whl::cv transaction_ended;
  whl::cv checkpoint_done;

  InMemoryWALRecord load_wal_record(size_t offset);
  void apply_record(InMemoryWALRecord &record);
  InMemoryWALRecord create_compensation_record(InMemoryWALRecord &record);
  void flush();
  virtual void flush_up_to(size_t lsn);
  void checkpoint_internal();
  static void *checkpointing_thread_func(void *arg);
  void recover();
  void sync_log();

  uint64_t begin(); // Returns txn id
  void commit(uint64_t txn_id);
  void abort(uint64_t txn_id);
  void set(uint64_t txn_id, uint64_t loc, size_t length, size_t to_length,
           const char *data);

  friend class Transaction;
  friend class PageHandle;

public:
  WAL(BufferPool *db, whl::string wal_path);
  ~WAL();

  class PageHandle {
    WAL *wal_layer;
    BufferPool::PageHandle page_handle;
    uint64_t txn_id;
    uint64_t page;
    PageHandle(WAL *wal_layer, uint64_t txn_id, uint64_t page,
               bool advise_eviction);
    friend class WAL;
    friend class Transaction;

  public:
    const char *ro_data();
    // Writes length bytes into the page starting at offset
    void write(int offset, const void *buffer, size_t length);
    // Writes buf_length bytes, zeroing the remaining bytes until written_length
    void write(int offset, const void *buffer, size_t buf_length,
               size_t written_length);
  };

  class Transaction {
    WAL *wal_layer;
    uint64_t txn_id;
    enum State { UNCOMMITTED, COMMITTED, ABORTED } state;
    Transaction(WAL *wal_layer, uint64_t txn_id);
    friend class WAL;
    friend class PageHandle;

  public:
    ~Transaction();
    void commit();
    void abort();

    PageHandle get_page(uint64_t page, bool advise_eviction = false);
  };

  Transaction begin_transaction();
  void checkpoint();

  // Only to be used internally
  void checkpoint_periodically();
};
} // namespace diskmap