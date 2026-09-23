// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// Transport coverage.
//
// Two levels are proven here:
//   1. framing, bounds, integrity and lifecycle over a real loopback socket
//      between two threads of this process;
//   2. end to end behaviour between genuinely separate operating system
//      processes, by launching the pathobs_node executable and having it stream
//      evidence to a server hosted by this test.

#include "test_framework.hpp"
#include "test_paths.hpp"

#include "pathobs/codec.hpp"
#include "pathobs/socket.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/transport.hpp"
#include "pathobs/version.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace pathobs;

namespace {

std::vector<std::byte> payload_of(std::size_t size, unsigned char seed) {
  std::vector<std::byte> out(size);
  for (std::size_t i = 0; i < size; ++i) {
    out[i] = static_cast<std::byte>((seed + i) & 0xFFu);
  }
  return out;
}

/// The build system hands us a path with the platform separator. Normalise it
/// so the string can be quoted into a shell command on any platform.
std::string executable_path(const char* raw) {
  std::string path = raw;
  for (char& c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  return path;
}

std::string quote(const std::string& text) {
  return std::string("\"") + text + "\"";
}

} // namespace

PATHOBS_TEST(transport, frames_round_trip_over_a_real_loopback_socket) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  options.port = 0;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());
  CHECK(server.value().port() != 0);

  std::atomic<bool> accepted{false};
  std::thread server_thread([&server, &accepted]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      return;
    }
    accepted.store(true, std::memory_order_release);
    for (;;) {
      const auto frame = channel.value().receive();
      if (!frame.has_value()) {
        return;
      }
      if (frame.value().type == transport::FrameType::Bye) {
        return;
      }
      const auto echoed = channel.value().send(frame.value().type, frame.value().payload);
      static_cast<void>(echoed);
    }
  });

  auto client = transport::connect(server.value().port(), limits);
  REQUIRE(client.has_value());
  transport::FramedChannel channel = std::move(client).value();

  const std::vector<std::size_t> sizes = {0, 1, 7, 1024, 65535, 1024 * 1024};
  for (std::size_t index = 0; index < sizes.size(); ++index) {
    const std::vector<std::byte> payload =
        payload_of(sizes[index], static_cast<unsigned char>(index));
    REQUIRE(channel.send(transport::FrameType::Evidence, payload).has_value());
    const auto reply = channel.receive();
    REQUIRE(reply.has_value());
    CHECK_EQ(reply.value().type, transport::FrameType::Evidence);
    CHECK(reply.value().payload == payload);
  }
  CHECK(channel.send(transport::FrameType::Bye, {}).has_value());
  server_thread.join();
  CHECK(accepted.load(std::memory_order_acquire));
  channel.close();
  server.value().stop();
}

PATHOBS_TEST(transport, an_oversized_frame_length_is_refused) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());

  std::atomic<int> outcome{0};
  std::thread server_thread([&server, &outcome]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      outcome.store(-1, std::memory_order_release);
      return;
    }
    const auto frame = channel.value().receive();
    if (!frame.has_value() && frame.error().code == ErrorCode::CapacityExceeded) {
      outcome.store(1, std::memory_order_release);
      return;
    }
    outcome.store(2, std::memory_order_release);
  });

  const Status initialized = net::system_init();
  REQUIRE(initialized.has_value());
  auto raw = net::Socket::connect_loopback(server.value().port());
  REQUIRE(raw.has_value());

  std::vector<std::byte> header(transport::kFrameHeaderBytes);
  const auto put_u32 = [&header](std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      header[offset + static_cast<std::size_t>(i)] =
          static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
    }
  };
  put_u32(0, transport::kFrameMagic);
  header[4] = std::byte{static_cast<unsigned char>(PATHOBS_WIRE_PROTOCOL_VERSION)};
  header[5] = std::byte{0};
  header[6] = std::byte{2};  // Evidence
  header[7] = std::byte{0};
  put_u32(8, 0);
  put_u32(12, limits.max_frame_bytes + 1u);
  put_u32(16, 0x12345678u);

  const Status sent = raw.value().send_all(header);
  REQUIRE(sent.has_value());
  server_thread.join();
  raw.value().close();
  server.value().stop();
  CHECK_EQ(outcome.load(std::memory_order_acquire), 1);
}

PATHOBS_TEST(transport, a_frame_with_a_bad_checksum_is_refused) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());

  std::atomic<int> outcome{0};
  std::thread server_thread([&server, &outcome]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      outcome.store(-1, std::memory_order_release);
      return;
    }
    const auto frame = channel.value().receive();
    if (!frame.has_value() && frame.error().code == ErrorCode::IntegrityFailure) {
      outcome.store(1, std::memory_order_release);
      return;
    }
    outcome.store(2, std::memory_order_release);
  });

  REQUIRE(net::system_init().has_value());
  auto raw = net::Socket::connect_loopback(server.value().port());
  REQUIRE(raw.has_value());
  std::vector<std::byte> frame =
      transport::encode_frame(transport::FrameType::Evidence, 0, payload_of(64, 3));
  frame[transport::kFrameHeaderBytes + 5] = std::byte{0xEE};
  REQUIRE(raw.value().send_all(frame).has_value());
  server_thread.join();
  raw.value().close();
  server.value().stop();
  CHECK_EQ(outcome.load(std::memory_order_acquire), 1);
}

PATHOBS_TEST(transport, a_wrong_magic_is_a_protocol_violation) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());

  std::atomic<int> outcome{0};
  std::thread server_thread([&server, &outcome]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      outcome.store(-1, std::memory_order_release);
      return;
    }
    const auto frame = channel.value().receive();
    if (!frame.has_value() && frame.error().code == ErrorCode::ProtocolViolation) {
      outcome.store(1, std::memory_order_release);
      return;
    }
    outcome.store(2, std::memory_order_release);
  });

  REQUIRE(net::system_init().has_value());
  auto raw = net::Socket::connect_loopback(server.value().port());
  REQUIRE(raw.has_value());
  const std::vector<std::byte> junk = payload_of(transport::kFrameHeaderBytes, 0x11);
  REQUIRE(raw.value().send_all(junk).has_value());
  server_thread.join();
  raw.value().close();
  server.value().stop();
  CHECK_EQ(outcome.load(std::memory_order_acquire), 1);
}

PATHOBS_TEST(transport, stopping_a_server_unblocks_a_parked_accept) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());

  std::atomic<bool> returned{false};
  std::thread accepting([&server, &returned]() {
    auto channel = server.value().accept();
    CHECK(!channel.has_value());
    returned.store(true, std::memory_order_release);
  });
  server.value().stop();
  accepting.join();
  CHECK(returned.load(std::memory_order_acquire));
  CHECK(!server.value().listening());
}

PATHOBS_TEST(transport, shutdown_unblocks_a_parked_reader) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());

  std::atomic<bool> parked{false};
  std::atomic<bool> unblocked{false};
  std::thread server_thread([&server, &parked, &unblocked]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      return;
    }
    parked.store(true, std::memory_order_release);
    const auto frame = channel.value().receive();
    if (!frame.has_value()) {
      unblocked.store(true, std::memory_order_release);
    }
  });

  auto client = transport::connect(server.value().port(), limits);
  REQUIRE(client.has_value());
  transport::FramedChannel channel = std::move(client).value();
  while (!parked.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  channel.shutdown();
  server_thread.join();
  CHECK(unblocked.load(std::memory_order_acquire));
  channel.close();
  server.value().stop();
}

PATHOBS_TEST(transport, a_separate_process_streams_evidence_to_this_test) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());
  REQUIRE(server.value().port() != 0);

  const std::string node = executable_path(PATHOBS_NODE_EXECUTABLE);
  const std::string command = quote(node) + " --port " + std::to_string(server.value().port()) +
                              " --scenario match --evidence-count 4 --hops 4";

  std::vector<ObservedPathEvidence> received;
  SourceDescriptor registered;
  bool saw_descriptor = false;
  bool acknowledged = false;

  std::thread server_thread([&]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      return;
    }
    for (;;) {
      const auto frame = channel.value().receive();
      if (!frame.has_value()) {
        break;
      }
      if (frame.value().type == transport::FrameType::Bye) {
        break;
      }
      if (frame.value().type == transport::FrameType::Hello) {
        const auto descriptor = codec::decode_exact<SourceDescriptor>(
            std::span<const std::byte>(frame.value().payload.data(), frame.value().payload.size()),
            limits);
        if (descriptor.has_value() && descriptor.value().capabilities.observed_evidence) {
          registered = descriptor.value();
          saw_descriptor = true;
        }
        continue;
      }
      if (frame.value().type == transport::FrameType::Evidence) {
        const auto evidence = codec::decode_exact<ObservedPathEvidence>(
            std::span<const std::byte>(frame.value().payload.data(), frame.value().payload.size()),
            limits);
        if (evidence.has_value()) {
          received.push_back(evidence.value());
        }
        const std::string ack = evidence.has_value() ? "ok" : "bad";
        const auto sent = channel.value().send(
            transport::FrameType::Ack,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(ack.data()),
                                       ack.size()));
        acknowledged = acknowledged || sent.has_value();
        continue;
      }
    }
  });

  const int exit_code = std::system(command.c_str());
  server_thread.join();
  server.value().stop();

  CHECK_EQ(exit_code, 0);
  CHECK(saw_descriptor);
  CHECK(acknowledged);
  CHECK_EQ(registered.surface, ProofSurface::Synthetic);
  CHECK_EQ(received.size(), 4u);
  if (received.size() == 4u) {
    for (std::size_t i = 0; i < received.size(); ++i) {
      CHECK_EQ(received[i].sequence.value(), i + 1);
      CHECK_EQ(received[i].id.str(), compute_evidence_id(received[i]).str());
      CHECK(received[i].source.valid());
      CHECK_EQ(received[i].hops.size(), 4u);
    }
  }
}

PATHOBS_TEST(transport, a_separate_process_that_sends_garbage_is_refused) {
  const Limits limits = default_limits();
  transport::ServerOptions options;
  options.limits = limits;
  auto server = transport::TransportServer::start(options);
  REQUIRE(server.has_value());

  std::atomic<bool> refused{false};
  std::thread server_thread([&server, &refused]() {
    auto channel = server.value().accept();
    if (!channel.has_value()) {
      return;
    }
    const auto frame = channel.value().receive();
    if (!frame.has_value() && frame.error().code == ErrorCode::ProtocolViolation) {
      refused.store(true, std::memory_order_release);
    }
  });

  const std::string node = executable_path(PATHOBS_NODE_EXECUTABLE);
  const std::string command =
      quote(node) + " --port " + std::to_string(server.value().port()) + " --scenario garbage";
  const int exit_code = std::system(command.c_str());
  server_thread.join();
  server.value().stop();
  static_cast<void>(exit_code);
  CHECK(refused.load(std::memory_order_acquire));
}
