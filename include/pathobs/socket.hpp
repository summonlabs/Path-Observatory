// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A small blocking TCP socket abstraction.
//
// The transport claims real behaviour between real processes, so it is built on
// an ordinary stream socket rather than on an in-process queue. Everything is
// loopback only: Path Observatory never binds a routable address on its own
// initiative.

#ifndef PATHOBS_SOCKET_HPP
#define PATHOBS_SOCKET_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace pathobs::net {

/// Process wide socket subsystem initialisation, reference counted.
PATHOBS_API Status system_init();
PATHOBS_API void system_shutdown();

class PATHOBS_API Socket {
 public:
  Socket() noexcept;
  ~Socket();

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  bool valid() const noexcept;
  explicit operator bool() const noexcept { return valid(); }

  /// Bind and listen on 127.0.0.1. A port of 0 selects an ephemeral port; read
  /// the chosen port back with local_port().
  static Result<Socket> listen_loopback(std::uint16_t port, int backlog);

  /// Connect to 127.0.0.1 with a bounded connect. The bound is a resource
  /// bound, not a test timeout: it exists so a half open listener cannot pin a
  /// worker forever.
  static Result<Socket> connect_loopback(std::uint16_t port);

  Result<Socket> accept();

  Status send_all(std::span<const std::byte> data);

  /// Read exactly data.size() bytes. Yields ConnectionClosed when the peer
  /// closes before the buffer is full.
  Status recv_exact(std::span<std::byte> data);

  /// Unblock a reader that is parked in recv_exact from another thread. Safe to
  /// call repeatedly and on an invalid socket.
  void shutdown_send() noexcept;
  void shutdown_both() noexcept;
  void close() noexcept;

  std::uint16_t local_port() const noexcept;
  void set_no_delay(bool enabled) noexcept;

 private:
  std::uintptr_t handle_;
};

} // namespace pathobs::net

#endif // PATHOBS_SOCKET_HPP
