// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/random.hpp"

#include <bit>

namespace pathobs {
namespace {

std::uint64_t rotl(std::uint64_t value, int amount) noexcept {
  return (value << amount) | (value >> (64 - amount));
}

} // namespace

std::uint64_t SplitMix64::next() noexcept {
  state_ += 0x9e3779b97f4a7c15ull;
  std::uint64_t z = state_;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

Rng::Rng(std::uint64_t seed) noexcept : seed_(seed) {
  SplitMix64 seeder(seed);
  for (auto& word : state_) {
    word = seeder.next();
  }
  if ((state_[0] | state_[1] | state_[2] | state_[3]) == 0) {
    state_[0] = 0x9e3779b97f4a7c15ull;
  }
}

std::uint64_t Rng::next_u64() noexcept {
  const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
  const std::uint64_t t = state_[1] << 17;
  state_[2] ^= state_[0];
  state_[3] ^= state_[1];
  state_[1] ^= state_[2];
  state_[0] ^= state_[3];
  state_[2] ^= t;
  state_[3] = rotl(state_[3], 45);
  return result;
}

std::uint64_t Rng::next_below(std::uint64_t bound) noexcept {
  if (bound == 0) {
    return 0;
  }
  const std::uint64_t threshold = (~bound + 1) % bound;
  for (;;) {
    const std::uint64_t value = next_u64();
    if (value >= threshold) {
      return value % bound;
    }
  }
}

std::uint64_t Rng::next_in_range(std::uint64_t low, std::uint64_t high) noexcept {
  if (high <= low) {
    return low;
  }
  return low + next_below(high - low + 1);
}

double Rng::next_unit() noexcept {
  return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace pathobs
