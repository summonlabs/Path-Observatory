// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Content digests.
//
// Digests are the backbone of Path Observatory identity and integrity:
//   * finding identity is a digest over a canonical finding tuple, so the same
//     inputs in any order produce the same identity;
//   * persistence records are checksum protected and the store header carries a
//     content digest so a truncated or rewritten file cannot masquerade as a
//     valid one.

#ifndef PATHOBS_DIGEST_HPP
#define PATHOBS_DIGEST_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace pathobs {

inline constexpr std::size_t kDigestBytes = 32;
inline constexpr std::size_t kDigestHexLength = kDigestBytes * 2;

/// SHA-256 over an arbitrary byte sequence.
class PATHOBS_API Sha256 {
 public:
  Sha256() noexcept;

  void update(std::span<const std::byte> data) noexcept;
  void update(std::string_view text) noexcept;

  /// Finalise the digest. The object is reset and may be reused.
  std::array<std::uint8_t, kDigestBytes> finish() noexcept;

  /// Total number of bytes fed to the hasher so far.
  std::uint64_t byte_count() const noexcept { return byte_count_; }

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::uint64_t byte_count_{0};
  std::size_t buffered_{0};
};

/// CRC-32C (Castagnoli). Used for cheap per-record integrity checks; content
/// identity uses SHA-256.
PATHOBS_API std::uint32_t crc32c(std::span<const std::byte> data) noexcept;
PATHOBS_API std::uint32_t crc32c(std::string_view text) noexcept;

class Digest {
 public:
  constexpr Digest() noexcept = default;

  static Digest from_bytes(const std::uint8_t* data) noexcept;
  static Result<Digest> parse_hex(std::string_view hex);

  static Digest of(std::span<const std::byte> data) noexcept;
  static Digest of(std::string_view text) noexcept;

  const std::array<std::uint8_t, kDigestBytes>& bytes() const noexcept { return bytes_; }

  bool is_zero() const noexcept;
  std::string hex() const;

  friend constexpr bool operator==(const Digest&, const Digest&) noexcept = default;
  friend constexpr auto operator<=>(const Digest&, const Digest&) noexcept = default;

 private:
  std::array<std::uint8_t, kDigestBytes> bytes_{};
};

/// Strongly typed, fixed width identity derived from a digest. Used for
/// evidence, comparisons and findings, whose identity must be a pure function
/// of their canonical content rather than of arrival order.
template <class Tag, std::size_t Bytes = 16>
class DigestId {
 public:
  constexpr DigestId() noexcept = default;

  static DigestId from_digest(const Digest& digest) noexcept {
    DigestId id;
    for (std::size_t i = 0; i < Bytes; ++i) {
      id.bytes_[i] = digest.bytes()[i];
    }
    return id;
  }

  static Result<DigestId> parse(std::string_view hex) {
    if (hex.size() != Bytes * 2) {
      return Error{ErrorCode::InvalidIdentity, "digest identity has the wrong length"};
    }
    DigestId id;
    for (std::size_t i = 0; i < Bytes; ++i) {
      const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = nibble(hex[i * 2]);
      const int lo = nibble(hex[i * 2 + 1]);
      if (hi < 0 || lo < 0) {
        return Error{ErrorCode::InvalidIdentity, "digest identity is not hexadecimal"};
      }
      id.bytes_[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return id;
  }

  bool valid() const noexcept {
    for (const auto byte : bytes_) {
      if (byte != 0) return true;
    }
    return false;
  }

  const std::array<std::uint8_t, Bytes>& bytes() const noexcept { return bytes_; }

  std::string str() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(Bytes * 2);
    for (std::size_t i = 0; i < Bytes; ++i) {
      out[i * 2] = kHex[bytes_[i] >> 4];
      out[i * 2 + 1] = kHex[bytes_[i] & 0x0Fu];
    }
    return out;
  }

  friend constexpr bool operator==(const DigestId&, const DigestId&) noexcept = default;
  friend constexpr auto operator<=>(const DigestId&, const DigestId&) noexcept = default;

 private:
  std::array<std::uint8_t, Bytes> bytes_{};
};

} // namespace pathobs

#endif // PATHOBS_DIGEST_HPP
