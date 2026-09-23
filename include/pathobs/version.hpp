// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#ifndef PATHOBS_VERSION_HPP
#define PATHOBS_VERSION_HPP

#include "pathobs/export.hpp"

#include <cstdint>
#include <string>

#define PATHOBS_VERSION_MAJOR 1
#define PATHOBS_VERSION_MINOR 0
#define PATHOBS_VERSION_PATCH 0

// The persistence format version is independent of the library version: a
// library upgrade is not a format upgrade, and the format version changes only
// when the on-disk encoding changes in a way that requires a migration.
#define PATHOBS_STORE_FORMAT_VERSION 1

// Wire protocol version for the transport between an observation node and the
// observatory service.
#define PATHOBS_WIRE_PROTOCOL_VERSION 1

namespace pathobs {

struct Version {
  std::uint32_t major{0};
  std::uint32_t minor{0};
  std::uint32_t patch{0};

  friend constexpr bool operator==(const Version&, const Version&) noexcept = default;
  friend constexpr auto operator<=>(const Version&, const Version&) noexcept = default;
};

PATHOBS_API Version version() noexcept;
PATHOBS_API const char* version_string() noexcept;

/// "Debug" or "Release" (or the value of CMAKE_BUILD_TYPE when it is neither).
PATHOBS_API const char* build_configuration() noexcept;

/// True when this translation unit was compiled with an address sanitizer.
PATHOBS_API bool built_with_address_sanitizer() noexcept;

/// Human readable description of the build, for the version command and reports.
PATHOBS_API std::string build_description();

} // namespace pathobs

#endif // PATHOBS_VERSION_HPP
