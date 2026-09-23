// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Framed transport between an observation node and an observatory service.
//
// The transport carries opaque, already canonical payloads. It owns framing,
// integrity, bounds and the connection lifecycle; it never interprets a record,
// which keeps the wire format and the on-disk format from drifting apart.
//
// A frame carries a magic number, a protocol version, a type, a flags word and
// a length, followed by the payload and a checksum. Every field is validated
// before a byte is allocated, and any violation closes the connection: a stream
// that has lost framing cannot be resynchronised safely.

#ifndef PATHOBS_TRANSPORT_HPP
#define PATHOBS_TRANSPORT_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/socket.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pathobs::transport {

inline constexpr std::uint32_t kFrameMagic = 0x54424F50u;  // "POBT" little endian
inline constexpr std::size_t kFrameHeaderBytes = 20;

enum class FrameType : std::uint16_t {
  Hello = 1,
  Evidence = 2,
  RouteGeneration = 3,
  TopologyGeneration = 4,
  SourceDescriptor = 5,
  Ack = 6,
  Error = 7,
  Bye = 8,
  Heartbeat = 9,
  ComparisonResult = 10,
};

PATHOBS_API const char* to_string(FrameType type) noexcept;

struct Frame {
  FrameType type{FrameType::Heartbeat};
  std::uint32_t flags{0};
  std::vector<std::byte> payload{};
};

/// A framed, integrity checked channel over one socket.
class PATHOBS_API FramedChannel {
 public:
  FramedChannel() = default;
  FramedChannel(net::Socket socket, Limits limits);

  FramedChannel(FramedChannel&&) noexcept = default;
  FramedChannel& operator=(FramedChannel&&) noexcept = default;
  FramedChannel(const FramedChannel&) = delete;
  FramedChannel& operator=(const FramedChannel&) = delete;

  bool valid() const noexcept { return socket_.valid(); }

  Status send(FrameType type, std::span<const std::byte> payload);

  /// Receive one frame. Never allocates more than the configured frame bound.
  Result<Frame> receive();

  /// Unblock a reader parked in receive() from another thread.
  void shutdown() noexcept;
  void close() noexcept;

  const Limits& limits() const noexcept { return limits_; }

 private:
  net::Socket socket_{};
  Limits limits_{};
};

struct ServerOptions {
  Limits limits{};
  std::uint16_t port{0};
  int backlog{8};
};

/// A loopback listener. Only 127.0.0.1 is ever bound.
class PATHOBS_API TransportServer {
 public:
  TransportServer() = default;
  ~TransportServer();

  TransportServer(TransportServer&&) noexcept = default;
  TransportServer& operator=(TransportServer&&) noexcept = default;
  TransportServer(const TransportServer&) = delete;
  TransportServer& operator=(const TransportServer&) = delete;

  static Result<TransportServer> start(const ServerOptions& options);

  /// Block until a peer connects. stop() unblocks this call.
  Result<FramedChannel> accept();

  void stop() noexcept;
  std::uint16_t port() const noexcept { return socket_.local_port(); }
  bool listening() const noexcept { return socket_.valid(); }

 private:
  net::Socket socket_{};
  Limits limits_{};
};

/// Connect a channel to a loopback server.
PATHOBS_API Result<FramedChannel> connect(std::uint16_t port, const Limits& limits);

/// Encode and decode a single length prefixed frame, used by tests and tooling.
PATHOBS_API std::vector<std::byte> encode_frame(FrameType type, std::uint32_t flags,
                                                std::span<const std::byte> payload);

} // namespace pathobs::transport

#endif // PATHOBS_TRANSPORT_HPP
