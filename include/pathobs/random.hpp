// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Deterministic pseudo random numbers.
//
// Randomized tests in Path Observatory are seeded and reproducible: a failing
// seed printed in a report replays exactly the same input sequence on any
// machine. Nothing in the runtime uses these generators for anything except
// test and benchmark input synthesis.

#ifndef PATHOBS_RANDOM_HPP
#define PATHOBS_RANDOM_HPP

#include "pathobs/export.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pathobs {

/// splitmix64. Used to expand a single seed into generator state.
class PATHOBS_API SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}
  std::uint64_t next() noexcept;

 private:
  std::uint64_t state_;
};

/// xoshiro256**. Small, fast, and fully specified by its state, which is what
/// reproducibility needs.
class PATHOBS_API Rng {
 public:
  explicit Rng(std::uint64_t seed) noexcept;

  std::uint64_t next_u64() noexcept;

  /// Uniform value in [0, bound). Uses rejection sampling so the result has no
  /// modulo bias. Returns 0 when bound is 0.
  std::uint64_t next_below(std::uint64_t bound) noexcept;

  /// Uniform value in [low, high]. Returns low when the range is empty.
  std::uint64_t next_in_range(std::uint64_t low, std::uint64_t high) noexcept;

  bool next_bool() noexcept { return (next_u64() & 1u) != 0u; }

  /// Double in [0, 1).
  double next_unit() noexcept;

  /// Deterministic in-place shuffle (Fisher-Yates over the internal stream).
  template <class T>
  void shuffle(std::vector<T>& items) noexcept {
    if (items.size() < 2) {
      return;
    }
    for (std::size_t i = items.size() - 1; i > 0; --i) {
      const std::size_t j = static_cast<std::size_t>(next_below(i + 1));
      T temporary = items[i];
      items[i] = items[j];
      items[j] = temporary;
    }
  }

  std::uint64_t seed() const noexcept { return seed_; }

 private:
  std::uint64_t seed_;
  std::uint64_t state_[4]{};
};

} // namespace pathobs

#endif // PATHOBS_RANDOM_HPP
