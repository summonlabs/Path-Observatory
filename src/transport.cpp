// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/transport.hpp"

#include "pathobs/digest.hpp"
#include "pathobs/version.hpp"

#include <array>
#include <cstring>

namespace pathobs::transport {
namespace {

void put_u32(std::byte* out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[i] = static_cast<std::byte>((value >> (i * 8)) & 0xffu);
  }
}

void put_u16(std::byte* out, std::uint16_t value) {
  for (int i = 0; i < 2; ++i) {
    out[i] = static_cast<std::byte>((value >> (i * 8)) & 0xffu);
  }
}

std::uint32_t get_u32(const std::byte* in) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[i])) << (i * 8);
  }
  return value;
}

std::uint16_t get_u16(const std::byte* in) {
  return static_cast<std::uint16_t>(static_cast<std::uint8_t>(in[0]) |
                                    (static_cast<std::uint8_t>(in[1]) << 8));
}

Result<FrameType> parse_frame_type(std::uint16_t raw) {
  switch (raw) {
    case 1:
      return FrameType::Hello;
    case 2:
      return FrameType::Evidence;
    case 3:
      return FrameType::RouteGeneration;
    case 4:
      return FrameType::TopologyGeneration;
    case 5:
      return FrameType::SourceDescriptor;
    case 6:
      return FrameType::Ack;
    case 7:
      return FrameType::Error;
    case 8:
      return FrameType::Bye;
    case 9:
      return FrameType::Heartbeat;
    case 10:
      return FrameType::ComparisonResult;
    default:
      return Error{ErrorCode::ProtocolViolation, "unknown frame type", std::to_string(raw)};
  }
}

} // namespace

const char* to_string(FrameType type) noexcept {
  switch (type) {
    case FrameType::Hello:
      return "hello";
    case FrameType::Evidence:
      return "evidence";
    case FrameType::RouteGeneration:
      return "route_generation";
    case FrameType::TopologyGeneration:
      return "topology_generation";
    case FrameType::SourceDescriptor:
      return "source_descriptor";
    case FrameType::Ack:
      return "ack";
    case FrameType::Error:
      return "error";
    case FrameType::Bye:
      return "bye";
    case FrameType::Heartbeat:
      return "heartbeat";
    case FrameType::ComparisonResult:
      return "comparison_result";
  }
  return "heartbeat";
}

std::vector<std::byte> encode_frame(FrameType type, std::uint32_t flags,
                                    std::span<const std::byte> payload) {
  std::vector<std::byte> out(kFrameHeaderBytes + payload.size());
  put_u32(out.data() + 0, kFrameMagic);
  put_u16(out.data() + 4, static_cast<std::uint16_t>(PATHOBS_WIRE_PROTOCOL_VERSION));
  put_u16(out.data() + 6, static_cast<std::uint16_t>(type));
  put_u32(out.data() + 8, flags);
  put_u32(out.data() + 12, static_cast<std::uint32_t>(payload.size()));
  put_u32(out.data() + 16, crc32c(payload));
  if (!payload.empty()) {
    std::memcpy(out.data() + kFrameHeaderBytes, payload.data(), payload.size());
  }
  return out;
}

FramedChannel::FramedChannel(net::Socket socket, Limits limits)
    : socket_(std::move(socket)), limits_(limits) {}

Status FramedChannel::send(FrameType type, std::span<const std::byte> payload) {
  if (!socket_.valid()) {
    return Error{ErrorCode::InvalidState, "send on a closed channel"};
  }
  if (payload.size() > limits_.max_frame_bytes) {
    return Error{ErrorCode::CapacityExceeded, "frame payload exceeds the configured bound"};
  }
  const std::vector<std::byte> frame = encode_frame(type, 0, payload);
  return socket_.send_all(frame);
}

Result<Frame> FramedChannel::receive() {
  std::array<std::byte, kFrameHeaderBytes> header{};
  const Status header_status = socket_.recv_exact(header);
  if (!header_status.has_value()) {
    return header_status.error();
  }
  if (get_u32(header.data() + 0) != kFrameMagic) {
    socket_.shutdown_both();
    return Error{ErrorCode::ProtocolViolation, "frame magic does not match"};
  }
  const std::uint16_t version = get_u16(header.data() + 4);
  if (version != PATHOBS_WIRE_PROTOCOL_VERSION) {
    socket_.shutdown_both();
    return Error{ErrorCode::UnsupportedVersion, "wire protocol version is not supported",
                 std::to_string(version)};
  }
  PATHOBS_TRY(type, parse_frame_type(get_u16(header.data() + 6)));
  const std::uint32_t flags = get_u32(header.data() + 8);
  const std::uint32_t payload_bytes = get_u32(header.data() + 12);
  const std::uint32_t payload_crc = get_u32(header.data() + 16);
  if (payload_bytes > limits_.max_frame_bytes) {
    socket_.shutdown_both();
    return Error{ErrorCode::CapacityExceeded, "frame payload exceeds the configured bound"};
  }

  Frame frame;
  frame.type = type;
  frame.flags = flags;
  frame.payload.resize(payload_bytes);
  if (payload_bytes > 0) {
    const Status payload_status = socket_.recv_exact(frame.payload);
    if (!payload_status.has_value()) {
      return payload_status.error();
    }
  }
  if (crc32c(std::span<const std::byte>(frame.payload.data(), frame.payload.size())) !=
      payload_crc) {
    socket_.shutdown_both();
    return Error{ErrorCode::IntegrityFailure, "frame payload failed its integrity check"};
  }
  return frame;
}

void FramedChannel::shutdown() noexcept { socket_.shutdown_both(); }

void FramedChannel::close() noexcept { socket_.close(); }

TransportServer::~TransportServer() { stop(); }

Result<TransportServer> TransportServer::start(const ServerOptions& options) {
  const Status limits_valid = options.limits.validate();
  if (!limits_valid.has_value()) {
    return limits_valid.error();
  }
  const Status initialized = net::system_init();
  if (!initialized.has_value()) {
    return initialized.error();
  }
  PATHOBS_TRY(socket, net::Socket::listen_loopback(options.port, options.backlog));
  TransportServer server;
  server.socket_ = std::move(socket);
  server.limits_ = options.limits;
  return server;
}

Result<FramedChannel> TransportServer::accept() {
  if (!socket_.valid()) {
    return Error{ErrorCode::InvalidState, "accept on a stopped server"};
  }
  PATHOBS_TRY(peer, socket_.accept());
  return FramedChannel(std::move(peer), limits_);
}

void TransportServer::stop() noexcept {
  if (socket_.valid()) {
    socket_.shutdown_both();
    socket_.close();
  }
}

Result<FramedChannel> connect(std::uint16_t port, const Limits& limits) {
  const Status initialized = net::system_init();
  if (!initialized.has_value()) {
    return initialized.error();
  }
  PATHOBS_TRY(socket, net::Socket::connect_loopback(port));
  return FramedChannel(std::move(socket), limits);
}

} // namespace pathobs::transport
