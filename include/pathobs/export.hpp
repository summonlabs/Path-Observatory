// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Path Observatory - export/visibility decoration for the public API.

#ifndef PATHOBS_EXPORT_HPP
#define PATHOBS_EXPORT_HPP

#if defined(PATHOBS_BUILD_SHARED)
#if defined(_MSC_VER)
#if defined(PATHOBS_BUILDING_LIBRARY)
#define PATHOBS_API __declspec(dllexport)
#else
#define PATHOBS_API __declspec(dllimport)
#endif
#else
#define PATHOBS_API __attribute__((visibility("default")))
#endif
#else
// The default distribution of Path Observatory is a static library; no
// decoration is required and using none keeps the ABI story simple.
#define PATHOBS_API
#endif

#endif // PATHOBS_EXPORT_HPP
