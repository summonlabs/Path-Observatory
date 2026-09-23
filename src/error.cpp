// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/error.hpp"

namespace pathobs {

const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::None:
      return "none";
    case ErrorCode::InvalidArgument:
      return "invalid_argument";
    case ErrorCode::OutOfRange:
      return "out_of_range";
    case ErrorCode::CapacityExceeded:
      return "capacity_exceeded";
    case ErrorCode::NotSupported:
      return "not_supported";
    case ErrorCode::Internal:
      return "internal";
    case ErrorCode::InvalidIdentity:
      return "invalid_identity";
    case ErrorCode::UnknownIdentity:
      return "unknown_identity";
    case ErrorCode::DuplicateIdentity:
      return "duplicate_identity";
    case ErrorCode::InvalidFormat:
      return "invalid_format";
    case ErrorCode::TruncatedInput:
      return "truncated_input";
    case ErrorCode::IntegrityFailure:
      return "integrity_failure";
    case ErrorCode::UnsupportedVersion:
      return "unsupported_version";
    case ErrorCode::UnknownGeneration:
      return "unknown_generation";
    case ErrorCode::FencedEvidence:
      return "fenced_evidence";
    case ErrorCode::InsufficientEvidence:
      return "insufficient_evidence";
    case ErrorCode::IoFailure:
      return "io_failure";
    case ErrorCode::CorruptStore:
      return "corrupt_store";
    case ErrorCode::ReadOnlyStore:
      return "read_only_store";
    case ErrorCode::TransportFailure:
      return "transport_failure";
    case ErrorCode::ProtocolViolation:
      return "protocol_violation";
    case ErrorCode::ConnectionClosed:
      return "connection_closed";
    case ErrorCode::InvalidState:
      return "invalid_state";
    case ErrorCode::Cancelled:
      return "cancelled";
    case ErrorCode::ShuttingDown:
      return "shutting_down";
  }
  return "unknown";
}

std::string Error::to_string() const {
  std::string out = pathobs::to_string(code);
  out += ": ";
  out += message;
  if (!detail.empty()) {
    out += " (";
    out += detail;
    out += ")";
  }
  return out;
}

std::string describe(const Error& error) { return error.to_string(); }

} // namespace pathobs
