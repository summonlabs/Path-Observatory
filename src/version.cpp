// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/version.hpp"

namespace pathobs {
namespace {

constexpr Version kVersion{PATHOBS_VERSION_MAJOR, PATHOBS_VERSION_MINOR, PATHOBS_VERSION_PATCH};

constexpr const char* kVersionString =
    "1.0.0";

} // namespace

Version version() noexcept { return kVersion; }

const char* version_string() noexcept { return kVersionString; }

const char* build_configuration() noexcept {
#if defined(PATHOBS_BUILD_CONFIGURATION)
  return PATHOBS_BUILD_CONFIGURATION;
#elif defined(NDEBUG)
  return "Release";
#else
  return "Debug";
#endif
}

bool built_with_address_sanitizer() noexcept {
#if defined(PATHOBS_ASAN_ENABLED)
  return true;
#else
  return false;
#endif
}

std::string build_description() {
  std::string description = "Path Observatory ";
  description += kVersionString;
  description += " (";
  description += build_configuration();
  if (built_with_address_sanitizer()) {
    description += ", asan";
  }
  description += ")";
  return description;
}

} // namespace pathobs
