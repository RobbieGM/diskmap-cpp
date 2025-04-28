#include "wal.h"
#include "page_types.h"
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <wheel.h>

namespace diskmap {

static size_t record_header_size(WALRecordType type) {
  if (type == WALRecordType::CLR) {
    return sizeof(WALCommonHeader) + sizeof(CompensationRecord);
  }
  if (type == WALRecordType::SET) {
    return sizeof(WALCommonHeader) + sizeof(SetRecord);
  }
  return sizeof(WALCommonHeader);
}

static size_t record_size(WALHeader &record) {
  size_t header_size = record_header_size(record.common_header.type);
  if (record.common_header.type == WALRecordType::CLR) {
    return header_size + record.compensation.new_value_len;
  }
  if (record.common_header.type == WALRecordType::SET) {
    return header_size + record.set.old_value_len + record.set.new_value_len;
  }
  return header_size;
}

static uint32_t checksum(const InMemoryWALRecord &record) {
  Checksum cs;
  // Digest header, starting after checksum field
  cs.digest(static_cast<const char *>(static_cast<const void *>(
                &record.header.common_header.checksum)) +
                sizeof(record.header.common_header.checksum),
            record_header_size(record.header.common_header.type) -
                sizeof(record.header.common_header.checksum));
  // Digest variable-length data
  if (record.data.size() > 0) {
    cs.digest(record.data.data_ptr(), record.data.size());
  }
  return cs.value();
}

static size_t get_unpadded_length(size_t length, const char *data) {
  size_t i = length;
  while (i > 0) {
    --i;
    if (data[i] != 0) {
      return i + 1;
    }
  }

  return 0;
}

InMemoryWALRecord WAL::load_wal_record(size_t offset) const {
  WALHeader header{};
  // Read as many bytes as the most possible required to read the whole header
  size_t max_header_size = sizeof(WALHeader);
  ::pread(wal_fd, &header, max_header_size, static_cast<long>(offset));
  size_t header_size = record_header_size(header.common_header.type);

  // Read record data
  size_t data_size = record_size(header) - header_size;
  InMemoryWALRecord result;
  result.header = header;
  result.data.resize(data_size);
  ::pread(wal_fd, result.data.data_ptr(), data_size,
          static_cast<long>(offset + header_size));
  return result;
}

void WAL::apply_record(InMemoryWALRecord &record) {
  if (record.header.common_header.type == WALRecordType::CLR) {
    BufferPool::PageHandle handle =
        pool->get_page(record.header.compensation.loc / PAGE_SIZE, false);
    // Set new value
    memcpy(handle.data() + (record.header.compensation.loc % PAGE_SIZE),
           record.data.data_ptr(), record.header.compensation.new_value_len);
    // Fill zeros after new value
    uint32_t zeros = record.header.compensation.length -
                     record.header.compensation.new_value_len;
    memset(handle.data() + (record.header.compensation.loc % PAGE_SIZE) +
               record.header.compensation.new_value_len,
           0, zeros);

    handle.modified_by(record.header.common_header.lsn);
  } else if (record.header.common_header.type == WALRecordType::SET) {
    BufferPool::PageHandle handle =
        pool->get_page(record.header.set.loc / PAGE_SIZE, false);
    // Set new value
    if (record.header.set.new_value_len > 0) {
      memcpy(handle.data() + (record.header.set.loc % PAGE_SIZE),
             &record.data[record.header.set.old_value_len],
             record.header.set.new_value_len);
    }
    // Fill zeros after new value
    uint32_t zeros = record.header.set.length - record.header.set.new_value_len;
    memset(handle.data() + (record.header.set.loc % PAGE_SIZE) +
               record.header.set.new_value_len,
           0, zeros);

    handle.modified_by(record.header.common_header.lsn);
  }
  // Other record types do not directly modify database
}

InMemoryWALRecord WAL::create_compensation_record(InMemoryWALRecord &record) {
  InMemoryWALRecord result;
  result.header.common_header.txn_id = record.header.common_header.txn_id;
  result.header.common_header.type = WALRecordType::CLR;
  result.header.compensation.undo_lsn = record.header.common_header.lsn;

  if (record.header.common_header.type == WALRecordType::SET) {
    result.header.compensation.length = record.header.set.length;
    result.header.compensation.new_value_len = record.header.set.old_value_len;
    result.header.compensation.loc = record.header.set.loc;

    result.data.resize(result.header.compensation.new_value_len);
    // Copy record old_value to compensation record data new_value
    memcpy(result.data.data_ptr(), record.data.data_ptr(),
           result.header.compensation.new_value_len);
  } else {
    throw DiskMapException(
        "WAL::create_compensation_record: record is not of type SET");
  }

  result.header.common_header.lsn = next_lsn++;
  result.header.common_header.checksum = checksum(result);
  return result;
}

void WAL::flush() {
  size_t appended_bytes = 0;
  for (size_t i = unflushed_record_index; i < records.size(); i++) {
    appended_bytes += record_size(records[i].header);
  }
  whl::vector<char> buffer(appended_bytes);
  size_t offset = 0;
  for (size_t i = unflushed_record_index; i < records.size(); i++) {
    auto &record = records[i];
    memcpy(buffer.data_ptr() + offset, &record.header,
           record_header_size(record.header.common_header.type));
    offset += record_header_size(record.header.common_header.type);
    memcpy(buffer.data_ptr() + offset, record.data.data_ptr(),
           record.data.size());
    offset += record.data.size();
  }
  ::pwrite(wal_fd, buffer.data_ptr(), appended_bytes,
           static_cast<long>(log_pos));
  log_pos += appended_bytes;
  unflushed_record_index = records.size();
  sync_log();
}

void WAL::flush_up_to(size_t lsn) {
  // If any unflushed record is as old as lsn, flush all records
  if (unflushed_record_index < records.size() &&
      records[unflushed_record_index].header.common_header.lsn <= lsn) {
    flush();
  }
}

void WAL::checkpoint_internal() {
  // Prevent new transactions from beginning
  checkpoint_pending = true;

  // Wait for all transactions to end
  while (active_transactions > 0) {
    checkpoint_cv.wait(wal_mutex);
  }

  // Flush all data to disk
  pool->flush_all();

  // Truncate log file
  ftruncate(wal_fd, 0);
  log_pos = 0;
  sync_log();

  // Allow transactions to begin again
  checkpoint_pending = false;
  checkpoint_done.broadcast();
}

void WAL::recover() {
  // Load all records from disk
  records.clear();
  size_t wal_length = lseek(wal_fd, 0, SEEK_END);
  log_pos = 0;

  while (log_pos < wal_length) {
    InMemoryWALRecord record = load_wal_record(log_pos);
    if (record.header.common_header.checksum != checksum(record)) {
      printf("recover: warning: checksum does not match; "
             "stopping here (at position %lx)\n",
             log_pos);
      // log_pos stops in a position to overwrite the corrupted record
      break;
    }
    log_pos += record_size(record.header);
    records.push_back(record);
  }

  // First pass: replay log and determine which transactions did not finish and
  // which LSNs have already been reverted
  whl::unordered_set<uint64_t> finished_txns;
  whl::unordered_set<uint64_t> reverted_lsns;
  for (size_t i = 0; i < records.size(); i++) {
    apply_record(records[i]);
    if (records[i].header.common_header.type == WALRecordType::COMMIT ||
        records[i].header.common_header.type == WALRecordType::ABORT_END) {
      finished_txns.insert(records[i].header.common_header.txn_id);
    }
    if (records[i].header.common_header.type == WALRecordType::CLR) {
      reverted_lsns.insert(records[i].header.compensation.undo_lsn);
    }
    next_lsn = whl::max(next_lsn, records[i].header.common_header.lsn + 1);
    next_txn_id =
        whl::max(next_txn_id, records[i].header.common_header.txn_id + 1);
  }

  // Second pass: find not-already-undone operations in order, and their
  // corresponding Txn IDs
  whl::vector<size_t> to_undo_indices; // Indices into records to undo
  whl::unordered_set<uint64_t>
      aborted_txns; // Txn IDs that need to finish aborting
  for (size_t i = 0; i < records.size(); i++) {
    InMemoryWALRecord &r = records[i];
    // If the record's transaction is unfinished, finish aborting it later and
    // undo the record if it's a SET
    if (!finished_txns.contains(r.header.common_header.txn_id)) {
      if (r.header.common_header.type == WALRecordType::SET) {
        to_undo_indices.push_back(i);
      }
      aborted_txns.insert(r.header.common_header.txn_id);
    }
  }

  // Create CLRs for each record in reverse order
  for (size_t j = to_undo_indices.size(); j-- > 0;) {
    size_t i = to_undo_indices[j];
    InMemoryWALRecord clr = create_compensation_record(records[i]);
    records.push_back(clr);
    apply_record(clr);
  }

  // Write abort end records
  aborted_txns.foreach ([&](uint64_t txn_id) {
    InMemoryWALRecord abort_end;
    abort_end.header.common_header.type = WALRecordType::ABORT_END;
    abort_end.header.common_header.lsn = next_lsn++;
    abort_end.header.common_header.txn_id = txn_id;
    abort_end.header.common_header.checksum = checksum(abort_end);
    records.push_back(abort_end);
    apply_record(abort_end);
  });
}

void WAL::sync_log() const {
  for (int attempts = 0; attempts < 3; attempts++) {
    if (fsync(wal_fd) == 0)
      return;
  }
  throw DiskMapException(
      "WAL::sync: fsync failed three times. Data cannot be saved properly.");
}

// Functions for writing new log records (and modifying the database)

uint32_t WAL::begin() {
  whl::mutex_guard _(&wal_mutex);
  while (checkpoint_pending) {
    checkpoint_done.wait(wal_mutex);
  }
  active_transactions++;

  InMemoryWALRecord new_record;
  new_record.header.common_header.type = WALRecordType::BEGIN;
  new_record.header.common_header.lsn = next_lsn++;
  new_record.header.common_header.txn_id = next_txn_id++;
  new_record.header.common_header.checksum = checksum(new_record);
  records.push_back(new_record);
  apply_record(new_record);
  return new_record.header.common_header.lsn;
}

void WAL::commit(uint32_t txn_id) {
  whl::mutex_guard _(&wal_mutex);
  InMemoryWALRecord new_record;
  new_record.header.common_header.type = WALRecordType::COMMIT;
  new_record.header.common_header.lsn = next_lsn++;
  new_record.header.common_header.txn_id = txn_id;
  new_record.header.common_header.checksum = checksum(new_record);
  records.push_back(new_record);
  apply_record(new_record);

  flush();
  active_transactions--;
  checkpoint_cv.broadcast();
}

void WAL::abort(uint32_t txn_id) {
  whl::mutex_guard _(&wal_mutex);
  InMemoryWALRecord abort_begin;
  abort_begin.header.common_header.type = WALRecordType::ABORT_BEGIN;
  abort_begin.header.common_header.lsn = next_lsn++;
  abort_begin.header.common_header.txn_id = txn_id;
  abort_begin.header.common_header.checksum = checksum(abort_begin);
  records.push_back(abort_begin);
  apply_record(abort_begin);

  // Find the beginning of the transaction
  ssize_t txn_begin_idx = -1;
  for (size_t i = 0; i < records.size(); i++) {
    if (records[i].header.common_header.txn_id == txn_id &&
        records[i].header.common_header.type == WALRecordType::BEGIN) {
      txn_begin_idx = static_cast<ssize_t>(i);
      break;
    }
  }

  // Find out what CLRs need to be made
  whl::vector<size_t> to_undo_indices;
  for (size_t i = txn_begin_idx + 1; i < records.size(); i++) {
    InMemoryWALRecord &r = records[i];
    if (r.header.common_header.txn_id == txn_id) {
      if (r.header.common_header.type == WALRecordType::SET) {
        to_undo_indices.push_back(i);
      } else if (r.header.common_header.type == WALRecordType::CLR) {
        if (r.header.compensation.undo_lsn ==
            records[to_undo_indices.back()].header.common_header.lsn) {
          to_undo_indices.pop_back();
        }
      }
    }
  }
  // Write CLRs
  for (size_t i = to_undo_indices.size(); i-- > 0;) {
    InMemoryWALRecord clr =
        create_compensation_record(records[to_undo_indices[i]]);
    records.push_back(clr);
    apply_record(clr);
  }

  InMemoryWALRecord abort_end;
  abort_end.header.common_header.type = WALRecordType::ABORT_END;
  abort_end.header.common_header.lsn = next_lsn++;
  abort_end.header.common_header.txn_id = txn_id;
  abort_end.header.common_header.checksum = checksum(abort_end);
  records.push_back(abort_end);
  apply_record(abort_end);

  active_transactions--;
  checkpoint_cv.broadcast();
}

void WAL::set(uint32_t txn_id, uint64_t loc, size_t data_length,
              size_t write_length, const char *data) {
  whl::mutex_guard _(&wal_mutex);
  InMemoryWALRecord new_record;
  new_record.header.common_header.type = WALRecordType::SET;
  new_record.header.common_header.lsn = next_lsn++;
  new_record.header.common_header.txn_id = txn_id;

  // Read old value
  BufferPool::PageHandle handle = pool->get_page(loc / PAGE_SIZE, false);
  char old_value[write_length];
  memcpy(old_value, handle.data() + (loc % PAGE_SIZE), write_length);
  size_t from_length = get_unpadded_length(write_length, old_value);

  // Set header
  new_record.header.set.loc = loc;
  new_record.header.set.length = write_length;
  new_record.header.set.old_value_len = from_length;
  new_record.header.set.new_value_len = data_length;

  // Set data to old value + new value
  new_record.data.resize(from_length + data_length);
  memcpy(new_record.data.data_ptr(), old_value, from_length);
  memcpy(new_record.data.data_ptr() + from_length, data, data_length);

  new_record.header.common_header.checksum = checksum(new_record);
  apply_record(
      new_record); // Probably safe to apply first since we hold the lock.
  records.push_back(whl::move(new_record));
}

void *WAL::checkpointing_thread_func(void *arg) {
  static_cast<WAL *>(arg)->checkpoint_periodically();
  return nullptr;
}

// Public functions

WAL::WAL(BufferPool *pool, const whl::string &wal_path)
    : pool(pool), checkpointing_thread(checkpointing_thread_func, this) {
  pool->set_wal(this);
  whl::mutex_guard _(&wal_mutex);
  // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
  wal_fd = open(wal_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  bool was_created = wal_fd > 0;
  if (!was_created) {
    wal_fd = open(wal_path.c_str(), O_RDWR);
    recover();
    // Checkpoint to reduce log file size, and because now is a good
    // opportunity
    checkpoint_internal();
  }
}

WAL::~WAL() {
  shutting_down = true;
  checkpoint_cv.broadcast();
  checkpointing_thread.join();
  close(wal_fd);
}

void WAL::checkpoint_periodically() {
  whl::mutex_guard _(&wal_mutex);
  while (true) {
    // Wait for some number of transactions to happen
    while (transactions_since_last_checkpoint < 5 && !shutting_down) {
      checkpoint_cv.wait(wal_mutex);
    }
    checkpoint_internal();
    if (shutting_down)
      return;
    transactions_since_last_checkpoint = 0;
  }
}

void WAL::checkpoint() {
  whl::mutex_guard _(&wal_mutex);
  checkpoint_internal();
}

WAL::ROTransaction WAL::begin_ro_transaction() { return ROTransaction(this); }
WAL::RWTransaction WAL::begin_rw_transaction() {
  uint32_t txn_id = begin();
  transactions_since_last_checkpoint++;
  return RWTransaction(this, txn_id);
}

// Transaction

WAL::ROTransaction::ROTransaction(WAL *wal_layer) : wal_layer(wal_layer) {}

WAL::ROTransaction::ROTransaction(ROTransaction &&other) noexcept
    : wal_layer(other.wal_layer) {
  other.wal_layer = nullptr;
}

WAL::ROTransaction &
WAL::ROTransaction::operator=(ROTransaction &&other) noexcept {
  wal_layer = other.wal_layer;
  other.wal_layer = nullptr;
  return *this;
}

WAL::RWTransaction::RWTransaction(WAL *wal_layer, uint32_t txn_id)
    : ROTransaction(wal_layer), txn_id(txn_id), state(State::UNCOMMITTED) {}

WAL::RWTransaction::~RWTransaction() {
  if (state == State::UNCOMMITTED && wal_layer != nullptr) {
    wal_layer->abort(txn_id);
  }
}

void WAL::RWTransaction::commit() {
  wal_layer->commit(txn_id);
  state = State::COMMITTED;
}

void WAL::RWTransaction::abort() {
  wal_layer->abort(txn_id);
  state = State::ABORTED;
}

} // namespace diskmap
