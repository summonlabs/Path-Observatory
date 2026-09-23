// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// pathobs_node is an independent observation process.
//
// It builds the synthetic laboratory fabric described by its arguments, then
// streams that fabric's evidence to a Path Observatory service over a real
// loopback TCP connection. It exists so that the transport can be tested
// between genuinely separate operating system processes rather than between two
// threads of one process.
//
// PROOF SURFACE: SYNTHETIC. This process produces laboratory evidence and
// labels it as such; it is not a switch telemetry agent.

#include "pathobs/codec.hpp"
#include "pathobs/socket.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/transport.hpp"
#include "pathobs/version.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <span>
#include <string>

namespace {

using namespace pathobs;

struct Options {
  std::map<std::string, std::string> values;

  bool has(const std::string& key) const { return values.find(key) != values.end(); }

  std::uint64_t number(const std::string& key, std::uint64_t fallback) const {
    const auto it = values.find(key);
    if (it == values.end()) {
      return fallback;
    }
    return std::strtoull(it->second.c_str(), nullptr, 10);
  }

  std::int64_t signed_number(const std::string& key, std::int64_t fallback) const {
    const auto it = values.find(key);
    if (it == values.end()) {
      return fallback;
    }
    return std::strtoll(it->second.c_str(), nullptr, 10);
  }

  std::string text(const std::string& key, const std::string& fallback) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  }
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.size() > 2 && argument[0] == '-' && argument[1] == '-') {
      const std::size_t equals = argument.find('=');
      if (equals != std::string::npos) {
        options.values[argument.substr(2, equals - 2)] = argument.substr(equals + 1);
      } else if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) {
        options.values[argument.substr(2)] = argv[++i];
      } else {
        options.values[argument.substr(2)] = "true";
      }
    }
  }
  return options;
}

} // namespace

int main(int argc, char** argv) {
  const Options options = parse_options(argc, argv);
  if (!options.has("port")) {
    std::fprintf(stderr,
                 "usage: pathobs_node --port N [--seed N --hops N --multipath N] [--scenario "
                 "match|diverge|partial|empty|flood|garbage] [--evidence-count N]\n");
    return 2;
  }

  const std::uint16_t port = static_cast<std::uint16_t>(options.number("port", 0));
  const std::string scenario = options.text("scenario", "match");
  const Limits limits = default_limits();

  synthetic::LabOptions lab;
  lab.seed = options.number("seed", 0);
  lab.hop_count = static_cast<std::uint32_t>(options.number("hops", 4));
  lab.multipath_members = static_cast<std::uint32_t>(options.number("multipath", 1));
  lab.evidence_count = static_cast<std::uint32_t>(options.number("evidence-count", 2));
  lab.first_sequence = options.number("first-sequence", 1);
  lab.evidence_age_nanos = options.signed_number("evidence-age-nanos", 0);
  lab.epoch = options.number("epoch", 1);
  lab.incarnation = options.number("incarnation", 3);
  lab.route_generation = options.number("route-generation", 7);
  lab.topology_generation = options.number("topology-generation", 5);
  lab.revision = options.number("revision", 12);
  lab.diverge = scenario == "diverge";
  lab.partial = scenario == "partial";
  lab.empty_trace = scenario == "empty";
  lab.omit_links = options.has("omit-links");

  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  if (!fabric.has_value()) {
    std::fprintf(stderr, "pathobs_node: %s\n", describe(fabric.error()).c_str());
    return 1;
  }

  if (scenario == "garbage") {
    // Adversarial: bytes that are not a frame at all are written straight onto
    // the socket, deliberately bypassing the framing code. The service must
    // refuse the stream instead of trying to resynchronise on it.
    const Status initialized = net::system_init();
    if (!initialized.has_value()) {
      return 1;
    }
    Result<net::Socket> raw = net::Socket::connect_loopback(port);
    if (!raw.has_value()) {
      return 1;
    }
    std::vector<std::byte> junk(24);
    for (std::size_t i = 0; i < junk.size(); ++i) {
      junk[i] = static_cast<std::byte>((0x40 + i) & 0xFFu);
    }
    const Status sent = raw.value().send_all(junk);
    raw.value().close();
    return sent.has_value() ? 3 : 1;
  }

  Result<transport::FramedChannel> connected = transport::connect(port, limits);
  if (!connected.has_value()) {
    return 1;
  }
  transport::FramedChannel channel = std::move(connected).value();

  for (const auto& descriptor : fabric.value().sources) {
    const std::string payload = codec::encode(descriptor);
    const Status sent = channel.send(
        transport::FrameType::Hello,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                   payload.size()));
    if (!sent.has_value()) {
      std::fprintf(stderr, "pathobs_node: %s\n", describe(sent.error()).c_str());
      return 1;
    }
  }

  std::uint64_t sent_count = 0;
  for (const auto& evidence : fabric.value().evidence) {
    const std::string payload = codec::encode(evidence);
    const Status sent = channel.send(
        transport::FrameType::Evidence,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                   payload.size()));
    if (!sent.has_value()) {
      std::fprintf(stderr, "pathobs_node: %s\n", describe(sent.error()).c_str());
      return 1;
    }
    Result<transport::Frame> reply = channel.receive();
    if (!reply.has_value()) {
      std::fprintf(stderr, "pathobs_node: no acknowledgement: %s\n",
                   describe(reply.error()).c_str());
      return 1;
    }
    ++sent_count;
  }

  const Status bye = channel.send(transport::FrameType::Bye, {});
  static_cast<void>(bye);
  std::printf("sent %llu evidence frame(s)\n", static_cast<unsigned long long>(sent_count));
  channel.close();
  return 0;
}
