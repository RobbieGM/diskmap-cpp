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
  uint32_t lsn;    // 32-bit LSN may overflow, but that's fine (and frequent
                   // checkpointing will reset LSN anyway)
  uint32_t txn_id; // Same with transaction ID
  WALRecordType type;
} __attribute__((packed));

// Represents setting a range of bytes to other bytes (within a page)
// Sometimes, many bytes for the original or new value are all 0x00.
// If these occur at the end of the range, they are considered fill bytes.
struct SetRecord {
  uint64_t loc;           // Starting address of the range
  uint16_t length;        // Total number of bytes that are set
  uint16_t old_value_len; // Number of non-fill bytes that have been overwritten
  uint16_t new_value_len; // Number of non-fill bytes making up the new value
  // Next bytes: first old_value_len bytes of the original value, then
  // new_value_len bytes of the new value
} __attribute__((packed));

// Represents undoing a SetRecord.
struct CompensationRecord {
  uint32_t undo_lsn;
  uint64_t loc;
  uint16_t length;
  uint16_t new_value_len;
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
  uint32_t next_lsn = 0;
  uint32_t next_txn_id = 0;
  whl::mutex wal_mutex;
  size_t log_pos = 0;
  whl::vector<InMemoryWALRecord> records;
  size_t unflushed_record_index = 0; // Index into records of the first
                                     // unflushed record
  int transactions_since_last_checkpoint = 0;
  int active_transactions = 0; // Number of currently running transactions. Not
                               // updated when recovering
  bool checkpoint_pending = false;
  bool shutting_down = false;
  whl::thread checkpointing_thread;
  whl::cv checkpoint_cv;
  whl::cv checkpoint_done;

  InMemoryWALRecord load_wal_record(size_t offset) const;
  void apply_record(InMemoryWALRecord &record);
  InMemoryWALRecord create_compensation_record(InMemoryWALRecord &record);
  void flush();
  void flush_up_to(size_t lsn) override;
  void checkpoint_internal();
  static void *checkpointing_thread_func(void *arg);
  void recover();
  void sync_log() const;

  uint32_t begin(); // Returns txn id
  void commit(uint32_t txn_id);
  void abort(uint32_t txn_id);
  void set(uint32_t txn_id, uint64_t loc, size_t length, size_t to_length,
           const char *data);

  friend class Transaction;
  friend class PageHandle;

public:
  WAL(BufferPool *pool, const whl::string &wal_path);
  virtual ~WAL();

  // Delete move/copy constructors and assignment operators
  WAL(WAL &&) = delete;
  WAL &operator=(WAL &&) = delete;
  WAL(const WAL &) = delete;
  WAL &operator=(const WAL &) = delete;

  class PageHandle {
    WAL *wal_layer;
    BufferPool::PageHandle page_handle;
    uint32_t txn_id;
    uint64_t page;
    PageHandle(WAL *wal_layer, uint32_t txn_id, uint64_t page,
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
    uint32_t txn_id;
    enum class State : uint8_t { UNCOMMITTED, COMMITTED, ABORTED } state;
    Transaction(WAL *wal_layer, uint32_t txn_id);
    Transaction(const Transaction &) = delete;
    Transaction(Transaction &&) = delete;
    Transaction &operator=(const Transaction &) = delete;
    Transaction &operator=(Transaction &&) = delete;
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