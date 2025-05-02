#pragma once

#include "buffer_pool.h"
#include "checksum.h"
#include "exception.h"
#include <condition_variable>
#include <thread>

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
    CompensationRecord compensation;
  };
} __attribute__((packed));

struct InMemoryWALRecord {
  WALHeader header;
  std::vector<char> data; // Any variable-length data
};

// A physical write-ahead logging layer, guaranteeing transaction atomicity and
// durability.
class WAL : AbstractWAL {
  BufferPool *pool;
  int wal_fd;
  uint32_t next_lsn = 0;
  uint32_t next_txn_id = 0;
  std::mutex wal_mutex;
  size_t log_pos = 0;
  std::vector<InMemoryWALRecord> records;
  size_t unflushed_record_index = 0; // Index into records of the first
                                     // unflushed record
  int transactions_since_last_checkpoint = 0;
  int active_transactions = 0; // Number of currently running transactions. Not
                               // updated when recovering
  bool checkpoint_pending = false;
  bool shutting_down = false;
  std::condition_variable checkpoint_cv;
  std::condition_variable checkpoint_done;
  std::thread checkpointing_thread;

  InMemoryWALRecord load_wal_record(size_t offset) const;
  void apply_record(InMemoryWALRecord &record);
  InMemoryWALRecord create_compensation_record(InMemoryWALRecord &record);
  void flush();
  void flush_up_to(size_t lsn) override;
  void checkpoint_internal(std::unique_lock<std::mutex> &wal_lock);
  static void *checkpointing_thread_func(void *arg);
  void recover();
  void sync_log() const;

  uint32_t begin(); // Returns txn id
  void commit(uint32_t txn_id);
  void abort(uint32_t txn_id);
  void set(uint32_t txn_id, uint64_t loc, size_t data_length,
           size_t write_length, const char *data);

  friend class Transaction;
  friend class PageHandle;

public:
  WAL(BufferPool *pool, const std::string &wal_path);
  virtual ~WAL();

  // Delete move/copy constructors and assignment operators
  WAL(WAL &&) = delete;
  WAL &operator=(WAL &&) = delete;
  WAL(const WAL &) = delete;
  WAL &operator=(const WAL &) = delete;

  template <typename T> class ROPageHandle {
  protected:
    WAL *wal_layer;
    BufferPool::PageHandle page_handle;
    uint64_t page;
    ROPageHandle(WAL *wal_layer, uint64_t page, bool advise_eviction)
        : wal_layer(wal_layer),
          page_handle(wal_layer->pool->get_page(page, advise_eviction)),
          page(page) {}

    friend class WAL;
    friend class Transaction;

  public:
    int64_t get_page() const { return page; }
    const T *ro_data() {
      return reinterpret_cast<const T *>(page_handle.data());
    }
  };

  template <typename T> class PageHandle : public ROPageHandle<T> {
    uint32_t txn_id;
    PageHandle(WAL *wal_layer, uint32_t txn_id, uint64_t page,
               bool advise_eviction)
        : ROPageHandle<T>(wal_layer, page, advise_eviction), txn_id(txn_id) {}
    friend class WAL;
    friend class Transaction;

  public:
    // const char *ro_data_bin();
    // Clears the whole page
    void clear() { write(0, nullptr, 0, PAGE_SIZE); }
    // Writes length bytes into the page starting at offset
    void write(int offset, const void *buffer, size_t length) {
      write(offset, buffer, length, length);
    }
    // Writes buf_length bytes, zeroing the remaining bytes until written_length
    void write(int offset, const void *buffer, size_t buf_length,
               size_t written_length) {
      this->wal_layer->set(txn_id, (this->page * PAGE_SIZE) + offset,
                           buf_length, written_length,
                           static_cast<const char *>(buffer));
    }
    // Write field
    template <typename U> void write(U T::*field, U value) {
      auto offset =
          reinterpret_cast<size_t>(&(reinterpret_cast<T *>(0)->*field));
      this->wal_layer->set(txn_id, (this->page * PAGE_SIZE) + offset, sizeof(U),
                           sizeof(U), reinterpret_cast<const char *>(&value));
    }
    // Write into fixed size array
    template <typename U, size_t N>
    void write(U (T::*field)[N], size_t index, U value) {
      auto offset =
          reinterpret_cast<size_t>(&(reinterpret_cast<T *>(0)->*field)[index]);
      this->wal_layer->set(txn_id, (this->page * PAGE_SIZE) + offset, sizeof(U),
                           sizeof(U), reinterpret_cast<const char *>(&value));
    }
    // Write into pointer
    template <typename U> void write(U (T::*field)[], size_t index, U value) {
      auto offset =
          reinterpret_cast<size_t>(&(reinterpret_cast<T *>(0)->*field)[index]);
      this->wal_layer->set(txn_id, (this->page * PAGE_SIZE) + offset, sizeof(U),
                           sizeof(U), reinterpret_cast<const char *>(&value));
    }
    // Write into pointer (multiple)
    template <typename U>
    void write(U (T::*field)[], size_t index, size_t count, const U *value) {
      auto offset =
          reinterpret_cast<size_t>(&(reinterpret_cast<T *>(0)->*field)[index]);
      this->wal_layer->set(txn_id, (this->page * PAGE_SIZE) + offset,
                           sizeof(U) * count, sizeof(U) * count,
                           reinterpret_cast<const char *>(value));
    }
  };

  class ROTransaction {
  protected:
    WAL *wal_layer;
    explicit ROTransaction(WAL *wal_layer);

    ROTransaction(const ROTransaction &) = delete;
    ROTransaction &operator=(const ROTransaction &) = delete;

    friend class WAL;

  public:
    ROTransaction(ROTransaction &&) noexcept;
    ROTransaction &operator=(ROTransaction &&) noexcept;

    virtual ~ROTransaction() {}
    template <typename T>
    ROPageHandle<T> get_page(uint64_t page, bool advise_eviction = false) {
      return ROPageHandle<T>(wal_layer, page, advise_eviction);
    }
  };

  class RWTransaction : public ROTransaction {
    uint32_t txn_id;
    enum class State : uint8_t { UNCOMMITTED, COMMITTED, ABORTED } state;
    RWTransaction(WAL *wal_layer, uint32_t txn_id);
    friend class WAL;
    template <typename U> friend class PageHandle;

  public:
    RWTransaction(const RWTransaction &) = delete;
    RWTransaction &operator=(const RWTransaction &) = delete;
    RWTransaction(RWTransaction &&other) noexcept
        : ROTransaction(std::move(static_cast<ROTransaction &>(other))),
          txn_id(other.txn_id), state(other.state) {}
    RWTransaction &operator=(RWTransaction &&other) noexcept {
      if (this != &other) {
        ROTransaction::operator=(
            std::move(static_cast<ROTransaction &>(other)));
        txn_id = other.txn_id;
        state = other.state;
      }
      return *this;
    }

    ~RWTransaction() override;
    void commit();
    void abort();

    template <typename T>
    PageHandle<T> get_page(uint64_t page, bool advise_eviction = false) {
      if (state != State::UNCOMMITTED) {
        throw DiskMapException("get_page: transaction is already finished");
      }
      return PageHandle<T>(wal_layer, txn_id, page, advise_eviction);
    }
  };

  ROTransaction begin_ro_transaction();
  RWTransaction begin_rw_transaction();
  void checkpoint();

  // Only to be used internally
  void checkpoint_periodically();
};

} // namespace diskmap