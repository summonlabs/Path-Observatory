// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/socket.hpp"

#include <atomic>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#endif

namespace pathobs::net {
namespace {

#if defined(_WIN32)
constexpr std::uintptr_t kInvalid = static_cast<std::uintptr_t>(INVALID_SOCKET);
using RawSocket = SOCKET;
#else
constexpr std::uintptr_t kInvalid = static_cast<std::uintptr_t>(-1);
using RawSocket = int;
#endif

std::mutex g_init_mutex;
std::uint32_t g_init_count = 0;

RawSocket to_raw(std::uintptr_t handle) noexcept { return static_cast<RawSocket>(handle); }

std::string last_socket_error() {
#if defined(_WIN32)
  return "winsock error " + std::to_string(WSAGetLastError());
#else
  return std::string("errno ") + std::to_string(errno);
#endif
}

void close_raw(RawSocket handle) noexcept {
  if (handle == static_cast<RawSocket>(kInvalid)) {
    return;
  }
#if defined(_WIN32)
  ::closesocket(handle);
#else
  ::close(handle);
#endif
}

void shutdown_raw(RawSocket handle, int how) noexcept {
  if (handle == static_cast<RawSocket>(kInvalid)) {
    return;
  }
#if defined(_WIN32)
  ::shutdown(handle, how);
#else
  ::shutdown(handle, how);
#endif
}

/// Bounded connect implemented with a non blocking connect followed by a select
/// wait. The wait is a resource bound: a listener that never answers must not
/// pin the caller for the lifetime of the process.
constexpr int kConnectTimeoutMillis = 10000;

Result<RawSocket> connect_bounded(std::uint16_t port) {
  const RawSocket handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (handle == static_cast<RawSocket>(kInvalid)) {
    return Error{ErrorCode::TransportFailure, "socket() failed", last_socket_error()};
  }

#if defined(_WIN32)
  u_long non_blocking = 1;
  if (::ioctlsocket(handle, FIONBIO, &non_blocking) != 0) {
    close_raw(handle);
    return Error{ErrorCode::TransportFailure, "ioctlsocket() failed", last_socket_error()};
  }
#else
  const int flags = ::fcntl(handle, F_GETFL, 0);
  if (flags < 0 || ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) < 0) {
    close_raw(handle);
    return Error{ErrorCode::TransportFailure, "fcntl() failed", last_socket_error()};
  }
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const int rc = ::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (rc != 0) {
    bool in_progress = false;
#if defined(_WIN32)
    const int error = WSAGetLastError();
    in_progress = error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    in_progress = errno == EINPROGRESS;
#endif
    if (!in_progress) {
      close_raw(handle);
      return Error{ErrorCode::TransportFailure, "connect() failed", last_socket_error()};
    }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(handle, &write_set);
    timeval timeout{};
    timeout.tv_sec = kConnectTimeoutMillis / 1000;
    timeout.tv_usec = (kConnectTimeoutMillis % 1000) * 1000;
    const int selected = ::select(static_cast<int>(handle) + 1, nullptr, &write_set, nullptr, &timeout);
    if (selected <= 0) {
      close_raw(handle);
      return Error{ErrorCode::TransportFailure,
                   selected == 0 ? "connect() timed out" : "select() failed", last_socket_error()};
    }
    int socket_error = 0;
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(socket_error));
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &length) != 0) {
      close_raw(handle);
      return Error{ErrorCode::TransportFailure, "getsockopt() failed", last_socket_error()};
    }
#else
    socklen_t length = sizeof(socket_error);
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, &socket_error, &length) != 0) {
      close_raw(handle);
      return Error{ErrorCode::TransportFailure, "getsockopt() failed", last_socket_error()};
    }
#endif
    if (socket_error != 0) {
      close_raw(handle);
      return Error{ErrorCode::TransportFailure, "connect() failed after select",
                   std::to_string(socket_error)};
    }
  }

  // Back to blocking for every later operation.
#if defined(_WIN32)
  u_long blocking = 0;
  ::ioctlsocket(handle, FIONBIO, &blocking);
#else
  const int restore = ::fcntl(handle, F_GETFL, 0);
  ::fcntl(handle, F_SETFL, restore & ~O_NONBLOCK);
#endif
  return handle;
}

} // namespace

Status system_init() {
  std::lock_guard<std::mutex> lock(g_init_mutex);
  if (g_init_count == 0) {
#if defined(_WIN32)
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      return Error{ErrorCode::TransportFailure, "WSAStartup failed"};
    }
#endif
  }
  ++g_init_count;
  return ok();
}

void system_shutdown() {
  std::lock_guard<std::mutex> lock(g_init_mutex);
  if (g_init_count == 0) {
    return;
  }
  --g_init_count;
  if (g_init_count == 0) {
#if defined(_WIN32)
    ::WSACleanup();
#endif
  }
}

Socket::Socket() noexcept : handle_(kInvalid) {}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) { other.handle_ = kInvalid; }

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = kInvalid;
  }
  return *this;
}

bool Socket::valid() const noexcept { return handle_ != kInvalid; }

Result<Socket> Socket::listen_loopback(std::uint16_t port, int backlog) {
  if (backlog < 1) {
    return Error{ErrorCode::InvalidArgument, "listen backlog must be at least one"};
  }
  const RawSocket handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (handle == static_cast<RawSocket>(kInvalid)) {
    return Error{ErrorCode::TransportFailure, "socket() failed", last_socket_error()};
  }

  int reuse = 1;
#if defined(_WIN32)
  ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
  ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  // Strictly loopback: never INADDR_ANY.
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close_raw(handle);
    return Error{ErrorCode::TransportFailure, "bind() failed", last_socket_error()};
  }
  if (::listen(handle, backlog) != 0) {
    close_raw(handle);
    return Error{ErrorCode::TransportFailure, "listen() failed", last_socket_error()};
  }

  Socket socket;
  socket.handle_ = static_cast<std::uintptr_t>(handle);
  return socket;
}

Result<Socket> Socket::connect_loopback(std::uint16_t port) {
  PATHOBS_TRY(raw, connect_bounded(port));
  Socket socket;
  socket.handle_ = static_cast<std::uintptr_t>(raw);
  socket.set_no_delay(true);
  return socket;
}

Result<Socket> Socket::accept() {
  if (!valid()) {
    return Error{ErrorCode::InvalidState, "accept() on an invalid socket"};
  }
  const RawSocket handle = ::accept(to_raw(handle_), nullptr, nullptr);
  if (handle == static_cast<RawSocket>(kInvalid)) {
    return Error{ErrorCode::TransportFailure, "accept() failed", last_socket_error()};
  }
  Socket socket;
  socket.handle_ = static_cast<std::uintptr_t>(handle);
  socket.set_no_delay(true);
  return socket;
}

Status Socket::send_all(std::span<const std::byte> data) {
  if (!valid()) {
    return Error{ErrorCode::InvalidState, "send() on an invalid socket"};
  }
  std::size_t sent = 0;
  while (sent < data.size()) {
    const std::size_t remaining = data.size() - sent;
    const int chunk = remaining > 2147483647ull ? 2147483647 : static_cast<int>(remaining);
    const int rc = ::send(to_raw(handle_), reinterpret_cast<const char*>(data.data() + sent), chunk, 0);
    if (rc <= 0) {
      return Error{ErrorCode::TransportFailure, "send() failed", last_socket_error()};
    }
    sent += static_cast<std::size_t>(rc);
  }
  return ok();
}

Status Socket::recv_exact(std::span<std::byte> data) {
  if (!valid()) {
    return Error{ErrorCode::InvalidState, "recv() on an invalid socket"};
  }
  std::size_t received = 0;
  while (received < data.size()) {
    const std::size_t remaining = data.size() - received;
    const int chunk = remaining > 2147483647ull ? 2147483647 : static_cast<int>(remaining);
    const int rc = ::recv(to_raw(handle_), reinterpret_cast<char*>(data.data() + received), chunk, 0);
    if (rc == 0) {
      return Error{ErrorCode::ConnectionClosed, "peer closed the connection", last_socket_error()};
    }
    if (rc < 0) {
      return Error{ErrorCode::TransportFailure, "recv() failed", last_socket_error()};
    }
    received += static_cast<std::size_t>(rc);
  }
  return ok();
}

void Socket::shutdown_send() noexcept {
  shutdown_raw(to_raw(handle_), 
#if defined(_WIN32)
               SD_SEND
#else
               SHUT_WR
#endif
  );
}

void Socket::shutdown_both() noexcept {
  shutdown_raw(to_raw(handle_), 
#if defined(_WIN32)
               SD_BOTH
#else
               SHUT_RDWR
#endif
  );
}

void Socket::close() noexcept {
  close_raw(to_raw(handle_));
  handle_ = kInvalid;
}

std::uint16_t Socket::local_port() const noexcept {
  if (!valid()) {
    return 0;
  }
  sockaddr_in address{};
#if defined(_WIN32)
  int length = static_cast<int>(sizeof(address));
#else
  socklen_t length = sizeof(address);
#endif
  if (::getsockname(to_raw(handle_), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return 0;
  }
  return ntohs(address.sin_port);
}

void Socket::set_no_delay(bool enabled) noexcept {
  if (!valid()) {
    return;
  }
  const int value = enabled ? 1 : 0;
#if defined(_WIN32)
  ::setsockopt(to_raw(handle_), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&value),
               sizeof(value));
#else
  ::setsockopt(to_raw(handle_), IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
#endif
}

} // namespace pathobs::net
