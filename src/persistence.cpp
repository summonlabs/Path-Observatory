// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/persistence.hpp"

#include "pathobs/digest.hpp"
#include "pathobs/version.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <io.h>
#endif

namespace pathobs {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'P', 'A', 'T', 'H', 'O', 'B', 'S', 0x01u};
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::uint32_t kFlagCleanShutdown = 0x00000001u;

struct HeaderSlot {
  std::uint32_t format_version{0};
  std::uint32_t flags{0};
  std::int64_t created_unix_nanos{0};
  std::int64_t opened_unix_nanos{0};
  std::uint32_t restart_count{0};
  std::uint64_t generation{0};
  bool valid{false};
};

void put_u32(std::uint8_t* out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
  }
}

void put_u64(std::uint8_t* out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
  }
}

void put_i64(std::uint8_t* out, std::int64_t value) {
  put_u64(out, static_cast<std::uint64_t>(value));
}

std::uint32_t get_u32(const std::uint8_t* in) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(in[i]) << (i * 8);
  }
  return value;
}

std::uint64_t get_u64(const std::uint8_t* in) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(in[i]) << (i * 8);
  }
  return value;
}

std::int64_t get_i64(const std::uint8_t* in) { return static_cast<std::int64_t>(get_u64(in)); }

std::string io_error(const char* what, const std::filesystem::path& path) {
  return std::string(what) + " failed for " + path.string();
}

/// Portable file open. The Windows CRT deprecates fopen and suggests fopen_s;
/// using the checked variant keeps this translation unit free of warning
/// suppressions.
std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#if defined(_WIN32)
  std::FILE* handle = nullptr;
  if (::fopen_s(&handle, path.string().c_str(), mode) != 0) {
    return nullptr;
  }
  return handle;
#else
  return std::fopen(path.string().c_str(), mode);
#endif
}

Result<HeaderSlot> read_header_slot(std::FILE* handle, std::uint64_t slot_index) {
  std::array<std::uint8_t, kStoreHeaderSlotBytes> slot{};
  if (std::fseek(handle, static_cast<long>(slot_index * kStoreHeaderSlotBytes), SEEK_SET) != 0) {
    return Error{ErrorCode::IoFailure, "fseek failed while reading a header slot"};
  }
  const std::size_t read = std::fread(slot.data(), 1, slot.size(), handle);
  HeaderSlot out;
  if (read != slot.size()) {
    return out;
  }
  if (std::memcmp(slot.data(), kMagic.data(), kMagic.size()) != 0) {
    return out;
  }
  if (get_u32(slot.data() + 12) != kStoreHeaderSlotBytes) {
    return out;
  }
  if (get_u32(slot.data() + 16) != kEndianMarker) {
    return out;
  }
  const std::uint32_t stored_checksum = get_u32(slot.data() + 56);
  const std::uint32_t computed =
      crc32c(std::span<const std::byte>(reinterpret_cast<const std::byte*>(slot.data()), 56));
  if (stored_checksum != computed) {
    return out;
  }
  out.format_version = get_u32(slot.data() + 8);
  out.flags = get_u32(slot.data() + 20);
  out.created_unix_nanos = get_i64(slot.data() + 24);
  out.opened_unix_nanos = get_i64(slot.data() + 32);
  out.restart_count = get_u32(slot.data() + 40);
  out.generation = get_u64(slot.data() + 48);
  out.valid = true;
  return out;
}

void write_record_header(std::uint8_t* header, std::size_t payload_bytes, RecordType type,
                         std::uint64_t sequence, std::uint32_t payload_checksum) {
  put_u32(header + 0, static_cast<std::uint32_t>(payload_bytes));
  const auto type_raw = static_cast<std::uint16_t>(type);
  header[4] = static_cast<std::uint8_t>(type_raw & 0xffu);
  header[5] = static_cast<std::uint8_t>((type_raw >> 8) & 0xffu);
  header[6] = 0;
  header[7] = 0;
  put_u64(header + 8, sequence);
  put_u32(header + 16, payload_checksum);
  put_u32(header + 20,
          crc32c(std::span<const std::byte>(reinterpret_cast<const std::byte*>(header), 20)));
  put_u64(header + 24, 0);
}

} // namespace

const char* to_string(RecordType type) noexcept {
  switch (type) {
    case RecordType::Source:
      return "source";
    case RecordType::TopologyGeneration:
      return "topology_generation";
    case RecordType::RouteGeneration:
      return "route_generation";
    case RecordType::Evidence:
      return "evidence";
    case RecordType::Comparison:
      return "comparison";
    case RecordType::Finding:
      return "finding";
    case RecordType::Marker:
      return "marker";
  }
  return "marker";
}

Result<RecordType> parse_record_type(std::uint16_t raw) {
  switch (raw) {
    case 1:
      return RecordType::Source;
    case 2:
      return RecordType::TopologyGeneration;
    case 3:
      return RecordType::RouteGeneration;
    case 4:
      return RecordType::Evidence;
    case 5:
      return RecordType::Comparison;
    case 6:
      return RecordType::Finding;
    case 7:
      return RecordType::Marker;
    default:
      return Error{ErrorCode::InvalidFormat, "unknown record type", std::to_string(raw)};
  }
}

PersistentStore::~PersistentStore() { close(); }

PersistentStore::PersistentStore(PersistentStore&& other) noexcept
    : path_(std::move(other.path_)),
      options_(other.options_),
      handle_(other.handle_),
      recovery_(other.recovery_),
      record_count_(other.record_count_),
      size_bytes_(other.size_bytes_),
      next_sequence_(other.next_sequence_),
      active_slot_(other.active_slot_) {
  other.handle_ = nullptr;
}

PersistentStore& PersistentStore::operator=(PersistentStore&& other) noexcept {
  if (this != &other) {
    close();
    path_ = std::move(other.path_);
    options_ = other.options_;
    handle_ = other.handle_;
    recovery_ = other.recovery_;
    record_count_ = other.record_count_;
    size_bytes_ = other.size_bytes_;
    next_sequence_ = other.next_sequence_;
    active_slot_ = other.active_slot_;
    other.handle_ = nullptr;
  }
  return *this;
}

void PersistentStore::close() {
  if (handle_ != nullptr) {
    std::fflush(handle_);
    std::fclose(handle_);
    handle_ = nullptr;
  }
}

namespace {

/// Write one header slot into an arbitrary handle. Shared by the live store and
/// by compaction, so a rewritten store is indistinguishable from a store that
/// was closed and reopened.
void write_slot(std::FILE* handle, std::uint64_t slot_index, std::uint64_t generation,
                std::uint32_t restart_count, bool clean, TimePoint created, TimePoint opened) {
  std::array<std::uint8_t, kStoreHeaderSlotBytes> slot{};
  std::memcpy(slot.data(), kMagic.data(), kMagic.size());
  put_u32(slot.data() + 8, PATHOBS_STORE_FORMAT_VERSION);
  put_u32(slot.data() + 12, static_cast<std::uint32_t>(kStoreHeaderSlotBytes));
  put_u32(slot.data() + 16, kEndianMarker);
  put_u32(slot.data() + 20, clean ? kFlagCleanShutdown : 0u);
  put_i64(slot.data() + 24, created.unix_nanos());
  put_i64(slot.data() + 32, opened.unix_nanos());
  put_u32(slot.data() + 40, restart_count);
  put_u32(slot.data() + 44, 0u);
  put_u64(slot.data() + 48, generation);
  put_u32(slot.data() + 56,
          crc32c(std::span<const std::byte>(reinterpret_cast<const std::byte*>(slot.data()), 56)));
  put_u32(slot.data() + 60, 0u);
  std::fseek(handle, static_cast<long>(slot_index * kStoreHeaderSlotBytes), SEEK_SET);
  std::fwrite(slot.data(), 1, slot.size(), handle);
}

} // namespace

Status PersistentStore::write_header_slot(std::uint64_t generation, std::uint32_t restart_count,
                                          bool clean, TimePoint created, TimePoint opened) {
  write_slot(handle_, active_slot_, generation, restart_count, clean, created, opened);
  if (std::fflush(handle_) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fflush", path_)};
  }
  return ok();
}

Result<PersistentStore> PersistentStore::open(const std::filesystem::path& path,
                                              const StoreOptions& options, TimePoint now) {
  const Status limits_valid = options.limits.validate();
  if (!limits_valid.has_value()) {
    return limits_valid.error();
  }
  if (!now.is_set()) {
    return Error{ErrorCode::InvalidArgument, "opening a store requires an evaluation time"};
  }

  PersistentStore store;
  store.path_ = path;
  store.options_ = options;
  store.recovery_.opened_at = now;

  store.handle_ = open_file(path, options.read_only ? "rb" : "r+b");
  if (store.handle_ == nullptr && !options.read_only) {
    store.handle_ = open_file(path, "w+b");
  }
  if (store.handle_ == nullptr) {
    return Error{ErrorCode::IoFailure, io_error("fopen", path)};
  }

  std::error_code size_error;
  const auto file_size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return Error{ErrorCode::IoFailure, io_error("file_size", path), size_error.message()};
  }

  if (file_size < kStoreHeaderBytes) {
    if (options.read_only) {
      return Error{ErrorCode::CorruptStore, "store is too short to contain a header"};
    }
    const std::array<std::uint8_t, kStoreHeaderBytes> zero{};
    if (std::fseek(store.handle_, 0, SEEK_SET) != 0 ||
        std::fwrite(zero.data(), 1, zero.size(), store.handle_) != zero.size()) {
      return Error{ErrorCode::IoFailure, io_error("fwrite", path)};
    }
    store.active_slot_ = 0;
    const Status written = store.write_header_slot(1, 0, false, now, now);
    if (!written.has_value()) {
      return written.error();
    }
    store.recovery_.created = true;
    store.recovery_.created_at = now;
    store.recovery_.header_generation = 1;
    store.recovery_.restart_count = 0;
  } else {
    PATHOBS_TRY(primary, read_header_slot(store.handle_, 0));
    PATHOBS_TRY(secondary, read_header_slot(store.handle_, 1));
    const HeaderSlot* chosen = nullptr;
    std::uint64_t chosen_slot = 0;
    if (primary.valid && secondary.valid) {
      if (secondary.generation >= primary.generation) {
        chosen = &secondary;
        chosen_slot = 1;
      } else {
        chosen = &primary;
        chosen_slot = 0;
      }
    } else if (primary.valid) {
      chosen = &primary;
      chosen_slot = 0;
    } else if (secondary.valid) {
      chosen = &secondary;
      chosen_slot = 1;
      store.recovery_.used_secondary_header_slot = true;
    } else {
      return Error{ErrorCode::CorruptStore,
                   "neither header slot is valid; refusing to guess at the store contents"};
    }
    if (chosen->format_version != PATHOBS_STORE_FORMAT_VERSION) {
      return Error{ErrorCode::UnsupportedVersion, "store format version is not supported",
                   std::to_string(chosen->format_version)};
    }
    store.recovery_.created_at = TimePoint::from_unix_nanos(chosen->created_unix_nanos);
    store.recovery_.restart_count = chosen->restart_count;
    store.recovery_.header_generation = chosen->generation;
    store.recovery_.unclean_previous_shutdown = (chosen->flags & kFlagCleanShutdown) == 0u;
    if (options.read_only) {
      store.active_slot_ = chosen_slot;
    } else {
      store.active_slot_ = chosen_slot == 0 ? 1 : 0;
      const Status written =
          store.write_header_slot(chosen->generation + 1, chosen->restart_count + 1, false,
                                  store.recovery_.created_at, now);
      if (!written.has_value()) {
        return written.error();
      }
      store.recovery_.header_generation = chosen->generation + 1;
      store.recovery_.restart_count = chosen->restart_count + 1;
    }
  }

  store.recovery_.incarnation = StoreIncarnationId::from_value(
      store.recovery_.header_generation == 0 ? 1 : store.recovery_.header_generation);

  PATHOBS_TRY(valid_records, store.scan(!options.read_only));
  store.recovery_.valid_records = valid_records;
  store.record_count_ = valid_records;
  store.next_sequence_ = valid_records + 1;

  if (std::fseek(store.handle_, 0, SEEK_END) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fseek", path)};
  }
  const long end = std::ftell(store.handle_);
  store.size_bytes_ = end < 0 ? 0 : static_cast<std::uint64_t>(end);
  store.recovery_.bytes = store.size_bytes_;
  return store;
}

Result<std::uint64_t> PersistentStore::scan(bool truncate_corrupt_tail) {
  if (std::fseek(handle_, static_cast<long>(kStoreHeaderBytes), SEEK_SET) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fseek", path_)};
  }

  std::uint64_t valid = 0;
  std::uint64_t offset = kStoreHeaderBytes;
  std::array<std::uint8_t, kStoreRecordHeaderBytes> header{};

  for (;;) {
    const std::size_t read = std::fread(header.data(), 1, header.size(), handle_);
    if (read == 0) {
      break;
    }
    if (read != header.size()) {
      if (!truncate_corrupt_tail) {
        recovery_.invalid_records = 1;
        return Error{ErrorCode::TruncatedInput, "store ends inside a record header"};
      }
      recovery_.corrupt_tail_truncated = true;
      recovery_.truncate_offset = offset;
      recovery_.truncate_detail = "store ends inside a record header";
      break;
    }

    const std::uint32_t payload_bytes = get_u32(header.data() + 0);
    const auto type_raw =
        static_cast<std::uint16_t>(header.data()[4] | (header.data()[5] << 8));
    const std::uint32_t payload_crc = get_u32(header.data() + 16);
    const std::uint32_t header_crc = get_u32(header.data() + 20);
    const std::uint32_t computed_header_crc = crc32c(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(header.data()), 20));

    bool record_valid = header_crc == computed_header_crc &&
                        payload_bytes <= options_.limits.max_store_record_bytes &&
                        parse_record_type(type_raw).has_value();
    if (record_valid) {
      std::vector<std::byte> payload(payload_bytes);
      if (payload_bytes > 0 &&
          std::fread(payload.data(), 1, payload_bytes, handle_) != payload_bytes) {
        record_valid = false;
      } else if (crc32c(std::span<const std::byte>(payload.data(), payload.size())) != payload_crc) {
        record_valid = false;
      }
    }

    if (!record_valid) {
      if (!truncate_corrupt_tail) {
        recovery_.invalid_records = 1;
        return Error{ErrorCode::CorruptStore, "store contains an invalid record",
                     "offset " + std::to_string(offset)};
      }
      recovery_.corrupt_tail_truncated = true;
      recovery_.truncate_offset = offset;
      recovery_.truncate_detail = "record failed its integrity check";
      break;
    }

    ++valid;
    offset += kStoreRecordHeaderBytes + payload_bytes;
    if (valid > options_.limits.max_store_records) {
      return Error{ErrorCode::CapacityExceeded,
                   "store holds more records than the configured bound"};
    }
  }

  if (recovery_.corrupt_tail_truncated && truncate_corrupt_tail) {
    if (std::fflush(handle_) != 0) {
      return Error{ErrorCode::IoFailure, io_error("fflush", path_)};
    }
#if defined(_WIN32)
    if (_chsize_s(_fileno(handle_), static_cast<long long>(recovery_.truncate_offset)) != 0) {
      return Error{ErrorCode::IoFailure, io_error("_chsize_s", path_)};
    }
#else
    if (ftruncate(fileno(handle_), static_cast<off_t>(recovery_.truncate_offset)) != 0) {
      return Error{ErrorCode::IoFailure, io_error("ftruncate", path_)};
    }
#endif
  }

  return valid;
}

Status PersistentStore::append(RecordType type, std::span<const std::byte> payload) {
  if (handle_ == nullptr) {
    return Error{ErrorCode::InvalidState, "append on a closed store"};
  }
  if (options_.read_only) {
    return Error{ErrorCode::ReadOnlyStore, "append on a read only store"};
  }
  if (payload.size() > options_.limits.max_store_record_bytes) {
    return Error{ErrorCode::CapacityExceeded, "record exceeds the configured record bound"};
  }
  if (record_count_ + 1 > options_.limits.max_store_records) {
    return Error{ErrorCode::CapacityExceeded, "store has reached its configured record bound"};
  }

  const std::uint64_t record_bytes = kStoreRecordHeaderBytes + payload.size();
  if (size_bytes_ > options_.limits.max_store_bytes ||
      record_bytes > options_.limits.max_store_bytes - size_bytes_) {
    return Error{ErrorCode::CapacityExceeded, "store has reached its configured byte bound"};
  }

  std::array<std::uint8_t, kStoreRecordHeaderBytes> header{};
  write_record_header(header.data(), payload.size(), type, next_sequence_, crc32c(payload));

  if (std::fseek(handle_, 0, SEEK_END) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fseek", path_)};
  }
  if (std::fwrite(header.data(), 1, header.size(), handle_) != header.size()) {
    return Error{ErrorCode::IoFailure, io_error("fwrite", path_)};
  }
  if (!payload.empty() &&
      std::fwrite(payload.data(), 1, payload.size(), handle_) != payload.size()) {
    return Error{ErrorCode::IoFailure, io_error("fwrite", path_)};
  }
  if (std::fflush(handle_) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fflush", path_)};
  }

  ++record_count_;
  size_bytes_ += record_bytes;
  ++next_sequence_;
  return ok();
}

Result<std::vector<StoredRecord>> PersistentStore::read_all() const {
  std::vector<StoredRecord> records;
  if (handle_ == nullptr) {
    return Error{ErrorCode::InvalidState, "read_all on a closed store"};
  }
  // The store's own handle is used on purpose. Opening the file a second time
  // while this instance holds it open is refused by Windows for read/write
  // modes, and a store that cannot read itself back would be useless.
  std::FILE* handle = handle_;
  if (std::fseek(handle, static_cast<long>(kStoreHeaderBytes), SEEK_SET) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fseek", path_)};
  }

  std::array<std::uint8_t, kStoreRecordHeaderBytes> header{};
  for (;;) {
    const std::size_t read = std::fread(header.data(), 1, header.size(), handle);
    if (read == 0) {
      break;
    }
    if (read != header.size()) {
      return Error{ErrorCode::TruncatedInput, "store ends inside a record header"};
    }
    const std::uint32_t payload_bytes = get_u32(header.data() + 0);
    const auto type_raw =
        static_cast<std::uint16_t>(header.data()[4] | (header.data()[5] << 8));
    const std::uint64_t sequence = get_u64(header.data() + 8);
    const std::uint32_t payload_crc = get_u32(header.data() + 16);
    const std::uint32_t header_crc = get_u32(header.data() + 20);
    if (crc32c(std::span<const std::byte>(reinterpret_cast<const std::byte*>(header.data()), 20)) !=
        header_crc) {
      return Error{ErrorCode::CorruptStore, "record header failed its integrity check"};
    }
    if (payload_bytes > options_.limits.max_store_record_bytes) {
      return Error{ErrorCode::CapacityExceeded, "record exceeds the configured record bound"};
    }
    const Result<RecordType> type = parse_record_type(type_raw);
    if (!type.has_value()) {
      return type.error();
    }
    StoredRecord record;
    record.type = type.value();
    record.sequence = sequence;
    record.payload.resize(payload_bytes);
    if (payload_bytes > 0 &&
        std::fread(record.payload.data(), 1, payload_bytes, handle) != payload_bytes) {
      return Error{ErrorCode::TruncatedInput, "store ends inside a record payload"};
    }
    if (crc32c(std::span<const std::byte>(record.payload.data(), record.payload.size())) !=
        payload_crc) {
      return Error{ErrorCode::CorruptStore, "record payload failed its integrity check"};
    }
    records.push_back(std::move(record));
    if (records.size() > options_.limits.max_store_records) {
      return Error{ErrorCode::CapacityExceeded, "store holds more records than the bound"};
    }
  }
  if (std::fseek(handle, 0, SEEK_END) != 0) {
    return Error{ErrorCode::IoFailure, io_error("fseek", path_)};
  }
  return records;
}

Result<std::size_t> PersistentStore::compact(std::size_t keep_newest) {
  if (options_.read_only) {
    return Error{ErrorCode::ReadOnlyStore, "compaction of a read only store"};
  }
  PATHOBS_TRY(records, read_all());
  const std::size_t keep = keep_newest == 0 ? 0 : std::min<std::size_t>(keep_newest, records.size());
  const std::size_t first = records.size() - keep;

  const std::filesystem::path temporary = std::filesystem::path(path_.string() + ".compact");
  std::FILE* out = open_file(temporary, "w+b");
  if (out == nullptr) {
    return Error{ErrorCode::IoFailure, io_error("fopen", temporary)};
  }

  const std::array<std::uint8_t, kStoreHeaderBytes> zero{};
  if (std::fwrite(zero.data(), 1, zero.size(), out) != zero.size()) {
    std::fclose(out);
    return Error{ErrorCode::IoFailure, io_error("fwrite", temporary)};
  }

  std::uint64_t written = 0;
  for (std::size_t i = first; i < records.size(); ++i) {
    const StoredRecord& record = records[i];
    std::array<std::uint8_t, kStoreRecordHeaderBytes> header{};
    write_record_header(header.data(), record.payload.size(), record.type, written + 1,
                        crc32c(std::span<const std::byte>(record.payload.data(),
                                                         record.payload.size())));
    if (std::fwrite(header.data(), 1, header.size(), out) != header.size() ||
        (!record.payload.empty() &&
         std::fwrite(record.payload.data(), 1, record.payload.size(), out) !=
             record.payload.size())) {
      std::fclose(out);
      return Error{ErrorCode::IoFailure, io_error("fwrite", temporary)};
    }
    ++written;
  }
  // The rewritten file carries a real header before it replaces the original, so
  // a crash between the rename and the reopen still leaves a store that opens.
  write_slot(out, 0, recovery_.header_generation + 1, recovery_.restart_count, false,
             recovery_.created_at, recovery_.opened_at);
  if (std::fflush(out) != 0) {
    std::fclose(out);
    return Error{ErrorCode::IoFailure, io_error("fflush", temporary)};
  }
  std::fclose(out);

  close();
  std::error_code rename_error;
  std::filesystem::rename(temporary, path_, rename_error);
  if (rename_error) {
    return Error{ErrorCode::IoFailure, "compaction could not replace the store",
                 rename_error.message()};
  }
  const std::filesystem::path reopened_path = path_;
  const TimePoint opened_at = recovery_.opened_at;
  Result<PersistentStore> reopened = open(reopened_path, options_, opened_at);
  if (!reopened.has_value()) {
    return reopened.error();
  }
  *this = std::move(reopened).value();
  return keep;
}

Status PersistentStore::mark_clean(TimePoint now) {
  if (options_.read_only) {
    return ok();
  }
  if (handle_ == nullptr) {
    return Error{ErrorCode::InvalidState, "mark_clean on a closed store"};
  }
  const std::uint64_t previous_slot = active_slot_;
  active_slot_ = active_slot_ == 0 ? 1 : 0;
  const Status status = write_header_slot(recovery_.header_generation + 1, recovery_.restart_count,
                                          true, recovery_.created_at, now);
  if (!status.has_value()) {
    active_slot_ = previous_slot;
    return status;
  }
  recovery_.header_generation += 1;
  recovery_.incarnation = StoreIncarnationId::from_value(recovery_.header_generation);
  return ok();
}

} // namespace pathobs
