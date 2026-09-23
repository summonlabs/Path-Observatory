// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Canonical encoding.
//
// Identity in Path Observatory is content derived: an evidence identity, a
// comparison identity and a finding identity are digests over a canonical byte
// encoding of the object. The encoding is fixed width little endian with
// explicit lengths so that it is:
//   * unambiguous (no two distinct objects share an encoding),
//   * order independent for sequences (the caller sorts before writing), and
//   * reproducible on any machine and any compiler.

#ifndef PATHOBS_CANONICAL_HPP
#define PATHOBS_CANONICAL_HPP

#include "pathobs/digest.hpp"
#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/strong_id.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pathobs {

class PATHOBS_API CanonicalWriter {
 public:
  CanonicalWriter() = default;

  void boolean(bool value);
  void u8(std::uint8_t value);
  void u16(std::uint16_t value);
  void u32(std::uint32_t value);
  void u64(std::uint64_t value);
  void i64(std::int64_t value);

  /// Length prefixed raw bytes (u32 length, then the bytes).
  void bytes(std::span<const std::byte> value);

  /// Length prefixed UTF-8 text (u32 byte length, then the bytes).
  void text(std::string_view value);

  void digest(const Digest& value);

  template <class Tag, class Rep>
  void id(StrongId<Tag, Rep> value) {
    u64(static_cast<std::uint64_t>(value.value()));
  }

  template <class Tag>
  void digest_id(DigestId<Tag> value) {
    bytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(value.bytes().data()), value.bytes().size()));
  }

  /// Sequence element count. Callers must have already sorted the elements.
  void count(std::size_t value) { u32(static_cast<std::uint32_t>(value)); }

  const std::string& buffer() const noexcept { return bytes_; }
  std::size_t size() const noexcept { return bytes_.size(); }
  Digest digest() const { return Digest::of(std::string_view(bytes_)); }
  std::string take() { return std::move(bytes_); }
  void clear() noexcept { bytes_.clear(); }

 private:
  std::string bytes_;
};

class PATHOBS_API CanonicalReader {
 public:
  explicit CanonicalReader(std::span<const std::byte> data) noexcept : data_(data) {}

  Result<bool> boolean();
  Result<std::uint8_t> u8();
  Result<std::uint16_t> u16();
  Result<std::uint32_t> u32();
  Result<std::uint64_t> u64();
  Result<std::int64_t> i64();
  Result<Digest> digest();

  /// Read a length prefixed byte string, refusing a length above max_bytes.
  Result<std::vector<std::byte>> bytes(std::uint32_t max_bytes);
  Result<std::string> text(std::uint32_t max_bytes);

  /// Read a sequence count, refusing a count above max_count.
  Result<std::uint32_t> count(std::uint32_t max_count);

  template <class Tag, class Rep = std::uint64_t>
  Result<StrongId<Tag, Rep>> id() {
    PATHOBS_TRY(value, u64());
    return StrongId<Tag, Rep>::from_value(static_cast<Rep>(value));
  }

  template <class Tag>
  Result<DigestId<Tag>> digest_id() {
    PATHOBS_TRY(raw, bytes(64));
    if (raw.size() > 32) {
      return Error{ErrorCode::InvalidFormat, "digest identity is too long"};
    }
    Digest digest;
    std::array<std::uint8_t, kDigestBytes> buffer{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
      buffer[i] = static_cast<std::uint8_t>(raw[i]);
    }
    digest = Digest::from_bytes(buffer.data());
    return DigestId<Tag>::from_digest(digest);
  }

  bool empty() const noexcept { return offset_ >= data_.size(); }
  std::size_t remaining() const noexcept { return data_.size() - offset_; }
  std::size_t offset() const noexcept { return offset_; }

  /// Every reader is expected to consume the whole payload; a trailing byte
  /// means the encoder and the decoder disagree about the shape.
  Status expect_end() const;

 private:
  Result<const std::byte*> take(std::size_t count);

  std::span<const std::byte> data_;
  std::size_t offset_{0};
};

} // namespace pathobs

#endif // PATHOBS_CANONICAL_HPP
