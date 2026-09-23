// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/canonical.hpp"

#include <cstring>

namespace pathobs {

void CanonicalWriter::boolean(bool value) { u8(value ? 1u : 0u); }

void CanonicalWriter::u8(std::uint8_t value) {
  bytes_.push_back(static_cast<char>(value));
}

void CanonicalWriter::u16(std::uint16_t value) {
  bytes_.push_back(static_cast<char>(value & 0xffu));
  bytes_.push_back(static_cast<char>((value >> 8) & 0xffu));
}

void CanonicalWriter::u32(std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes_.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

void CanonicalWriter::u64(std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes_.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

void CanonicalWriter::i64(std::int64_t value) {
  u64(static_cast<std::uint64_t>(value));
}

void CanonicalWriter::bytes(std::span<const std::byte> value) {
  u32(static_cast<std::uint32_t>(value.size()));
  bytes_.append(reinterpret_cast<const char*>(value.data()), value.size());
}

void CanonicalWriter::text(std::string_view value) {
  u32(static_cast<std::uint32_t>(value.size()));
  bytes_.append(value.data(), value.size());
}

void CanonicalWriter::digest(const Digest& value) {
  bytes_.append(reinterpret_cast<const char*>(value.bytes().data()), value.bytes().size());
}

Result<const std::byte*> CanonicalReader::take(std::size_t count) {
  if (count > remaining()) {
    return Error{ErrorCode::TruncatedInput, "canonical payload ended early",
                 "need " + std::to_string(count) + " bytes, have " +
                     std::to_string(remaining())};
  }
  const std::byte* cursor = data_.data() + offset_;
  offset_ += count;
  return cursor;
}

Result<bool> CanonicalReader::boolean() {
  PATHOBS_TRY(value, u8());
  if (value > 1u) {
    return Error{ErrorCode::InvalidFormat, "canonical boolean is not 0 or 1"};
  }
  return value == 1u;
}

Result<std::uint8_t> CanonicalReader::u8() {
  PATHOBS_TRY(cursor, take(1));
  return static_cast<std::uint8_t>(*cursor);
}

Result<std::uint16_t> CanonicalReader::u16() {
  PATHOBS_TRY(cursor, take(2));
  std::uint16_t value = 0;
  for (int i = 0; i < 2; ++i) {
    value |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(cursor[i])) << (i * 8);
  }
  return value;
}

Result<std::uint32_t> CanonicalReader::u32() {
  PATHOBS_TRY(cursor, take(4));
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(cursor[i])) << (i * 8);
  }
  return value;
}

Result<std::uint64_t> CanonicalReader::u64() {
  PATHOBS_TRY(cursor, take(8));
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(cursor[i])) << (i * 8);
  }
  return value;
}

Result<std::int64_t> CanonicalReader::i64() {
  PATHOBS_TRY(value, u64());
  return static_cast<std::int64_t>(value);
}

Result<Digest> CanonicalReader::digest() {
  PATHOBS_TRY(cursor, take(kDigestBytes));
  std::array<std::uint8_t, kDigestBytes> buffer{};
  for (std::size_t i = 0; i < kDigestBytes; ++i) {
    buffer[i] = static_cast<std::uint8_t>(cursor[i]);
  }
  return Digest::from_bytes(buffer.data());
}

Result<std::vector<std::byte>> CanonicalReader::bytes(std::uint32_t max_bytes) {
  PATHOBS_TRY(length, u32());
  if (length > max_bytes) {
    return Error{ErrorCode::CapacityExceeded, "canonical byte string exceeds the configured bound"};
  }
  PATHOBS_TRY(cursor, take(length));
  std::vector<std::byte> out(cursor, cursor + length);
  return out;
}

Result<std::string> CanonicalReader::text(std::uint32_t max_bytes) {
  PATHOBS_TRY(length, u32());
  if (length > max_bytes) {
    return Error{ErrorCode::CapacityExceeded, "canonical text exceeds the configured bound"};
  }
  PATHOBS_TRY(cursor, take(length));
  return std::string(reinterpret_cast<const char*>(cursor), length);
}

Result<std::uint32_t> CanonicalReader::count(std::uint32_t max_count) {
  PATHOBS_TRY(value, u32());
  if (value > max_count) {
    return Error{ErrorCode::CapacityExceeded, "canonical sequence count exceeds the configured bound"};
  }
  return value;
}

Status CanonicalReader::expect_end() const {
  if (!empty()) {
    return Error{ErrorCode::InvalidFormat, "canonical payload has trailing bytes",
                 std::to_string(remaining()) + " bytes left"};
  }
  return ok();
}

} // namespace pathobs
