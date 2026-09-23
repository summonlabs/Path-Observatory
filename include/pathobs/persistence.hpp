// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Versioned, integrity checked persistence with conservative recovery.
//
// Layout
// ------
//   offset 0   header slot A (64 bytes)
//   offset 64  header slot B (64 bytes)
//   offset 128 records
//
// Two header slots alternate. Each slot carries a generation counter and a
// checksum, so a torn header write leaves the previous slot intact and the
// store still opens. Records are length prefixed, checksummed twice (header and
// payload) and bounded in every dimension.
//
// Recovery policy
// ---------------
//   * a header slot that fails its checksum is ignored; the other slot is used;
//   * if neither slot is valid the store is refused rather than guessed at;
//   * records are validated one at a time; the first invalid record truncates
//     the file at that boundary and the truncation is reported, never hidden;
//   * an unclean previous shutdown is reported, not repaired silently.
//
// Restart semantics
// -----------------
// Loading is not reviving. A record keeps the receive time it was written with,
// so evidence that was stale before a restart is still stale afterwards.

#ifndef PATHOBS_PERSISTENCE_HPP
#define PATHOBS_PERSISTENCE_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/time.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace pathobs {

inline constexpr std::size_t kStoreHeaderSlotBytes = 64;
inline constexpr std::size_t kStoreHeaderBytes = kStoreHeaderSlotBytes * 2;
inline constexpr std::size_t kStoreRecordHeaderBytes = 32;

enum class RecordType : std::uint16_t {
  Source = 1,
  TopologyGeneration = 2,
  RouteGeneration = 3,
  Evidence = 4,
  Comparison = 5,
  Finding = 6,
  Marker = 7,
};

PATHOBS_API const char* to_string(RecordType type) noexcept;
PATHOBS_API Result<RecordType> parse_record_type(std::uint16_t raw);

struct StoredRecord {
  RecordType type{RecordType::Marker};
  std::uint64_t sequence{0};
  std::vector<std::byte> payload{};
};

struct RecoveryReport {
  bool created{false};
  bool used_secondary_header_slot{false};
  bool unclean_previous_shutdown{false};
  bool corrupt_tail_truncated{false};
  std::uint64_t truncate_offset{0};
  std::string truncate_detail;
  std::uint64_t valid_records{0};
  std::uint64_t invalid_records{0};
  std::uint64_t bytes{0};
  std::uint32_t restart_count{0};
  std::uint64_t header_generation{0};
  StoreIncarnationId incarnation{};
  TimePoint created_at{};
  TimePoint opened_at{};

  friend bool operator==(const RecoveryReport&, const RecoveryReport&) noexcept = default;
};

struct StoreOptions {
  Limits limits{};
  bool read_only{false};
};

class PATHOBS_API PersistentStore {
 public:
  PersistentStore() = default;
  ~PersistentStore();

  PersistentStore(PersistentStore&& other) noexcept;
  PersistentStore& operator=(PersistentStore&& other) noexcept;
  PersistentStore(const PersistentStore&) = delete;
  PersistentStore& operator=(const PersistentStore&) = delete;

  static Result<PersistentStore> open(const std::filesystem::path& path,
                                      const StoreOptions& options, TimePoint now);

  Status append(RecordType type, std::span<const std::byte> payload);

  /// Read and validate every record. Bounded by the configured record cap.
  Result<std::vector<StoredRecord>> read_all() const;

  /// Rewrite the store keeping only the newest records, up to the requested
  /// count. The rewrite goes to a temporary file that atomically replaces the
  /// original.
  Result<std::size_t> compact(std::size_t keep_newest);

  /// Record a clean shutdown in the header so the next open can tell a crash
  /// from a shutdown.
  Status mark_clean(TimePoint now);

  const RecoveryReport& recovery() const noexcept { return recovery_; }
  std::uint64_t record_count() const noexcept { return record_count_; }
  std::uint64_t size_bytes() const noexcept { return size_bytes_; }
  const std::filesystem::path& path() const noexcept { return path_; }
  bool is_open() const noexcept { return handle_ != nullptr; }

 private:
  Status write_header_slot(std::uint64_t generation, std::uint32_t restart_count, bool clean,
                           TimePoint created, TimePoint opened);
  Result<std::uint64_t> scan(bool truncate_corrupt_tail);
  void close();

  std::filesystem::path path_{};
  StoreOptions options_{};
  // Mutable because read_all() reads through the open handle rather than
  // opening the file a second time: a second read/write open of a file that is
  // already open in this process is refused on Windows, and a store that cannot
  // read itself back would be useless.
  mutable std::FILE* handle_{nullptr};
  RecoveryReport recovery_{};
  std::uint64_t record_count_{0};
  std::uint64_t size_bytes_{0};
  std::uint64_t next_sequence_{1};
  std::uint64_t active_slot_{0};
};

} // namespace pathobs

#endif // PATHOBS_PERSISTENCE_HPP
