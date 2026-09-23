// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/digest.hpp"

#include <cstring>

namespace pathobs {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t value, std::uint32_t amount) noexcept {
  return (value >> amount) | (value << (32u - amount));
}

constexpr std::uint32_t big_sigma0(std::uint32_t x) noexcept {
  return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

constexpr std::uint32_t big_sigma1(std::uint32_t x) noexcept {
  return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

constexpr std::uint32_t small_sigma0(std::uint32_t x) noexcept {
  return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

constexpr std::uint32_t small_sigma1(std::uint32_t x) noexcept {
  return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

std::array<std::uint32_t, 256> make_crc32c_table() noexcept {
  std::array<std::uint32_t, 256> table{};
  constexpr std::uint32_t kPolynomial = 0x82f63b78u;
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t crc = i;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) != 0u ? (crc >> 1) ^ kPolynomial : (crc >> 1);
    }
    table[i] = crc;
  }
  return table;
}

const std::array<std::uint32_t, 256>& crc32c_table() noexcept {
  static const std::array<std::uint32_t, 256> table = make_crc32c_table();
  return table;
}

char hex_digit(std::uint8_t value) noexcept {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + (value - 10));
}

} // namespace

Sha256::Sha256() noexcept {
  state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  buffer_.fill(0);
}

void Sha256::compress(const std::uint8_t* block) noexcept {
  std::array<std::uint32_t, 64> schedule{};
  for (std::size_t i = 0; i < 16; ++i) {
    schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                  (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                  (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                  static_cast<std::uint32_t>(block[i * 4 + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    schedule[i] = small_sigma1(schedule[i - 2]) + schedule[i - 7] +
                  small_sigma0(schedule[i - 15]) + schedule[i - 16];
  }

  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];

  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t temp1 =
        h + big_sigma1(e) + ((e & f) ^ (~e & g)) + kSha256K[i] + schedule[i];
    const std::uint32_t temp2 = big_sigma0(a) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(std::span<const std::byte> data) noexcept {
  const auto* cursor = reinterpret_cast<const std::uint8_t*>(data.data());
  std::size_t remaining = data.size();
  byte_count_ += static_cast<std::uint64_t>(remaining);

  if (buffered_ != 0) {
    while (remaining > 0 && buffered_ < buffer_.size()) {
      buffer_[buffered_++] = *cursor++;
      --remaining;
    }
    if (buffered_ == buffer_.size()) {
      compress(buffer_.data());
      buffered_ = 0;
    }
  }

  while (remaining >= buffer_.size()) {
    compress(cursor);
    cursor += buffer_.size();
    remaining -= buffer_.size();
  }

  while (remaining > 0) {
    buffer_[buffered_++] = *cursor++;
    --remaining;
  }
}

void Sha256::update(std::string_view text) noexcept {
  update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

std::array<std::uint8_t, kDigestBytes> Sha256::finish() noexcept {
  const std::uint64_t bit_length = byte_count_ * 8u;
  const std::uint8_t pad = 0x80u;
  update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&pad), 1));
  const std::uint8_t zero = 0x00u;
  while (buffered_ != 56) {
    update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&zero), 1));
  }
  std::array<std::uint8_t, 8> length_bytes{};
  for (std::size_t i = 0; i < 8; ++i) {
    length_bytes[i] = static_cast<std::uint8_t>((bit_length >> (56 - i * 8)) & 0xffu);
  }
  // The length field itself must not change byte_count_, so feed it directly.
  for (std::size_t i = 0; i < 8; ++i) {
    buffer_[buffered_++] = length_bytes[i];
  }
  compress(buffer_.data());
  buffered_ = 0;

  std::array<std::uint8_t, kDigestBytes> out{};
  for (std::size_t i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xffu);
    out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xffu);
    out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xffu);
    out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xffu);
  }

  state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  byte_count_ = 0;
  buffer_.fill(0);
  return out;
}

std::uint32_t crc32c(std::span<const std::byte> data) noexcept {
  const auto& table = crc32c_table();
  std::uint32_t crc = 0xffffffffu;
  for (const auto byte : data) {
    crc = table[(crc ^ static_cast<std::uint8_t>(byte)) & 0xffu] ^ (crc >> 8);
  }
  return crc ^ 0xffffffffu;
}

std::uint32_t crc32c(std::string_view text) noexcept {
  return crc32c(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                           text.size()));
}

Digest Digest::from_bytes(const std::uint8_t* data) noexcept {
  Digest digest;
  std::memcpy(digest.bytes_.data(), data, kDigestBytes);
  return digest;
}

Result<Digest> Digest::parse_hex(std::string_view hex) {
  if (hex.size() != kDigestHexLength) {
    return Error{ErrorCode::InvalidFormat, "digest must be 64 hexadecimal characters"};
  }
  Digest digest;
  for (std::size_t i = 0; i < kDigestBytes; ++i) {
    const auto decode = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const int hi = decode(hex[i * 2]);
    const int lo = decode(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return Error{ErrorCode::InvalidFormat, "digest contains a non-hexadecimal character"};
    }
    digest.bytes_[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return digest;
}

Digest Digest::of(std::span<const std::byte> data) noexcept {
  Sha256 hasher;
  hasher.update(data);
  return from_bytes(hasher.finish().data());
}

Digest Digest::of(std::string_view text) noexcept {
  Sha256 hasher;
  hasher.update(text);
  return from_bytes(hasher.finish().data());
}

bool Digest::is_zero() const noexcept {
  for (const auto byte : bytes_) {
    if (byte != 0) return false;
  }
  return true;
}

std::string Digest::hex() const {
  std::string out;
  out.resize(kDigestHexLength);
  for (std::size_t i = 0; i < kDigestBytes; ++i) {
    out[i * 2] = hex_digit(static_cast<std::uint8_t>(bytes_[i] >> 4));
    out[i * 2 + 1] = hex_digit(static_cast<std::uint8_t>(bytes_[i] & 0x0fu));
  }
  return out;
}

} // namespace pathobs
