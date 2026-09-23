// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// The pathobs inspection and ingest tool.
//
// It is a thin shell over the library. Every judgement it reports is a library
// judgement, and every report names the proof surface it is based on.

#include "pathobs/codec.hpp"
#include "pathobs/documents.hpp"
#include "pathobs/runtime.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/transport.hpp"
#include "pathobs/version.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace pathobs;

struct Options {
  std::map<std::string, std::string> values;

  bool has(const std::string& key) const { return values.find(key) != values.end(); }

  std::string get(const std::string& key, const std::string& fallback) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  }

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
};

Options parse_options(int argc, char** argv, int first) {
  Options options;
  for (int i = first; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.size() > 2 && argument[0] == '-' && argument[1] == '-') {
      const std::size_t equals = argument.find('=');
      if (equals != std::string::npos) {
        options.values[argument.substr(2, equals - 2)] = argument.substr(equals + 1);
      } else if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0) {
        // A value may begin with a single dash: negative numbers are values, a
        // long option is not.
        options.values[argument.substr(2)] = argv[++i];
      } else {
        options.values[argument.substr(2)] = "true";
      }
    }
  }
  return options;
}

int usage() {
  std::printf(
      "Path Observatory %s\n"
      "usage: pathobs <command> [options]\n"
      "\n"
      "  version\n"
      "  selftest\n"
      "  compare   [--seed N --diverge --partial --multipath N --pretty --now TIME]\n"
      "  explain   [same options as compare]\n"
      "  ingest    --store DIR [--seed N ...]\n"
      "  history   --store DIR [--limit N --pretty]\n"
      "  export    --store DIR [--out FILE --pretty]\n"
      "  validate  --store DIR\n"
      "  serve     --port N [--store DIR --count N --pretty]\n",
      version_string());
  return 2;
}

std::string read_file(const std::string& path, bool& ok) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    ok = false;
    return {};
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  ok = true;
  return buffer.str();
}

synthetic::LabOptions lab_options(const Options& options) {
  synthetic::LabOptions lab;
  lab.seed = options.number("seed", 0);
  lab.hop_count = static_cast<std::uint32_t>(options.number("hops", 4));
  lab.multipath_members = static_cast<std::uint32_t>(options.number("multipath", 1));
  lab.evidence_count = static_cast<std::uint32_t>(options.number("evidence-count", 1));
  lab.first_sequence = options.number("first-sequence", 1);
  lab.evidence_age_nanos = options.signed_number("evidence-age-nanos", 0);
  lab.epoch = options.number("epoch", 1);
  lab.incarnation = options.number("incarnation", 3);
  lab.route_generation = options.number("route-generation", 7);
  lab.topology_generation = options.number("topology-generation", 5);
  lab.revision = options.number("revision", 12);
  lab.diverge = options.has("diverge");
  lab.partial = options.has("partial");
  lab.omit_links = options.has("omit-links");
  lab.stale_epoch = options.has("stale-epoch");
  lab.stale_revision = options.has("stale-revision");
  lab.unknown_generation = options.has("unknown-generation");
  lab.empty_trace = options.has("empty-trace");
  return lab;
}

struct Session {
  std::unique_ptr<ObservatoryRuntime> runtime;
};

Result<std::unique_ptr<ObservatoryRuntime>> make_runtime(const Options& options, TimePoint now,
                                                         bool persist) {
  RuntimeConfig config;
  config.limits = default_limits();
  config.persist = persist;
  if (persist) {
    config.store_path = options.get("store", "pathobs-store.pob");
  }
  config.load_evidence_on_start = !options.has("no-load");
  config.worker_threads = static_cast<std::uint32_t>(options.number("workers", 2));
  auto clock = std::make_unique<ManualClock>(now);
  Result<std::unique_ptr<ObservatoryRuntime>> runtime =
      ObservatoryRuntime::create(config, std::move(clock));
  if (!runtime.has_value()) {
    return runtime.error();
  }
  std::unique_ptr<ObservatoryRuntime> observatory = std::move(runtime).value();
  const Status started = observatory->start();
  if (!started.has_value()) {
    return started.error();
  }
  return observatory;
}

Status publish_lab(ObservatoryRuntime& runtime, const synthetic::LabFabric& fabric) {
  for (const auto& descriptor : fabric.sources) {
    const Status registered = runtime.register_source(descriptor);
    if (!registered.has_value()) {
      return registered.error();
    }
  }
  const Status topology = runtime.publish_topology(fabric.topology);
  if (!topology.has_value()) {
    return topology.error();
  }
  const Status routes = runtime.publish_routes(fabric.routes);
  if (!routes.has_value()) {
    return routes.error();
  }
  return ok();
}

int fail(const Error& error) {
  std::fprintf(stderr, "pathobs: %s\n", describe(error).c_str());
  return 1;
}

int command_compare(const Options& options, bool explain) {
  const bool pretty = options.has("pretty");
  Limits limits = default_limits();
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab_options(options), limits);
  if (!fabric.has_value()) {
    return fail(fabric.error());
  }
  TimePoint now = fabric.value().routes.received;
  if (options.has("now")) {
    Result<TimePoint> parsed = TimePoint::parse_rfc3339(options.get("now", std::string{}));
    if (!parsed.has_value()) {
      return fail(parsed.error());
    }
    now = parsed.value();
  }

  Result<std::unique_ptr<ObservatoryRuntime>> created = make_runtime(options, now, false);
  if (!created.has_value()) {
    return fail(created.error());
  }
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();
  const Status published = publish_lab(*runtime, fabric.value());
  if (!published.has_value()) {
    return fail(published.error());
  }
  for (const auto& evidence : fabric.value().evidence) {
    Result<IngestResult> ingested = runtime->ingest_evidence(evidence);
    if (!ingested.has_value()) {
      return fail(ingested.error());
    }
    if (!ingested.value().accepted) {
      std::fprintf(stderr, "pathobs: evidence rejected: %s\n",
                   ingested.value().detail.c_str());
    }
  }
  Result<std::vector<ComparisonResult>> results = runtime->compare_all();
  if (!results.has_value()) {
    return fail(results.error());
  }
  for (const auto& result : results.value()) {
    std::cout << documents::to_json(result, pretty) << std::endl;
    if (explain) {
      std::cout << "-- rationale --" << std::endl;
      for (const auto& line : result.rationale) {
        std::cout << "  * " << line << std::endl;
      }
    }
  }
  const Status stopped = runtime->stop();
  if (!stopped.has_value()) {
    return fail(stopped.error());
  }
  return 0;
}

int command_ingest(const Options& options) {
  const bool pretty = options.has("pretty");
  Limits limits = default_limits();
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab_options(options), limits);
  if (!fabric.has_value()) {
    return fail(fabric.error());
  }
  const TimePoint now = fabric.value().routes.received;
  Result<std::unique_ptr<ObservatoryRuntime>> created = make_runtime(options, now, true);
  if (!created.has_value()) {
    return fail(created.error());
  }
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();
  const Status published = publish_lab(*runtime, fabric.value());
  if (!published.has_value()) {
    return fail(published.error());
  }
  for (const auto& evidence : fabric.value().evidence) {
    Result<IngestResult> ingested = runtime->ingest_evidence(evidence);
    if (!ingested.has_value()) {
      return fail(ingested.error());
    }
  }
  Result<std::vector<ComparisonResult>> results = runtime->compare_all();
  if (!results.has_value()) {
    return fail(results.error());
  }
  std::cout << documents::to_json(runtime->stats(), pretty) << std::endl;
  const Status stopped = runtime->stop();
  if (!stopped.has_value()) {
    return fail(stopped.error());
  }
  return 0;
}

int command_history(const Options& options) {
  const bool pretty = options.has("pretty");
  StoreOptions store_options;
  store_options.limits = default_limits();
  store_options.read_only = true;
  Result<PersistentStore> store =
      PersistentStore::open(options.get("store", "pathobs-store.pob"), store_options,
                            TimePoint::from_unix_nanos(1767225600000000000LL));
  if (!store.has_value()) {
    return fail(store.error());
  }
  HistoryQuery query;
  query.limit = static_cast<std::size_t>(options.number("limit", 64));
  if (options.has("group")) {
    query.group = MaybeId<PathGroupId>(PathGroupId::from_value(options.number("group", 0)));
  }
  HistoryStore history(store_options.limits);
  Result<std::vector<StoredRecord>> records = store->read_all();
  if (!records.has_value()) {
    return fail(records.error());
  }
  for (const auto& record : records.value()) {
    if (record.type != RecordType::Comparison) {
      continue;
    }
    Result<ComparisonResult> decoded = codec::decode_exact<ComparisonResult>(
        std::span<const std::byte>(record.payload.data(), record.payload.size()),
        store_options.limits);
    if (!decoded.has_value()) {
      return fail(decoded.error());
    }
    const Status appended = history.append(decoded.value());
    if (!appended.has_value()) {
      return fail(appended.error());
    }
  }
  const HistoryQueryResult result = history.query(query);
  std::cout << documents::to_json(result, pretty) << std::endl;
  return 0;
}

int command_export(const Options& options) {
  const bool pretty = options.has("pretty");
  StoreOptions store_options;
  store_options.limits = default_limits();
  store_options.read_only = true;
  Result<PersistentStore> store =
      PersistentStore::open(options.get("store", "pathobs-store.pob"), store_options,
                            TimePoint::from_unix_nanos(1767225600000000000LL));
  if (!store.has_value()) {
    return fail(store.error());
  }
  Result<std::vector<StoredRecord>> records = store->read_all();
  if (!records.has_value()) {
    return fail(records.error());
  }

  std::ostringstream out;
  out << "{" << (pretty ? "\n" : "");
  out << documents::to_json(store->recovery(), pretty);
  out << (pretty ? ",\n" : ",");
  out << "\"records\":[";
  bool first = true;
  std::uint64_t limit = options.number("limit", 1000000);
  for (const auto& record : records.value()) {
    if (limit == 0) {
      break;
    }
    --limit;
    if (!first) {
      out << ",";
    }
    first = false;
    switch (record.type) {
      case RecordType::Source: {
        Result<SourceDescriptor> decoded = codec::decode_exact<SourceDescriptor>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            store_options.limits);
        out << (decoded.has_value() ? documents::to_json(decoded.value(), false)
                                    : std::string("{\"kind\":\"pathobs.unreadable\"}"));
        break;
      }
      case RecordType::Evidence: {
        Result<ObservedPathEvidence> decoded = codec::decode_exact<ObservedPathEvidence>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            store_options.limits);
        out << (decoded.has_value() ? documents::to_json(decoded.value(), false)
                                    : std::string("{\"kind\":\"pathobs.unreadable\"}"));
        break;
      }
      case RecordType::Comparison: {
        Result<ComparisonResult> decoded = codec::decode_exact<ComparisonResult>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            store_options.limits);
        out << (decoded.has_value() ? documents::to_json(decoded.value(), false)
                                    : std::string("{\"kind\":\"pathobs.unreadable\"}"));
        break;
      }
      case RecordType::Finding: {
        Result<Finding> decoded = codec::decode_exact<Finding>(
            std::span<const std::byte>(record.payload.data(), record.payload.size()),
            store_options.limits);
        out << (decoded.has_value() ? documents::to_json(decoded.value(), false)
                                    : std::string("{\"kind\":\"pathobs.unreadable\"}"));
        break;
      }
      case RecordType::TopologyGeneration:
      case RecordType::RouteGeneration:
      case RecordType::Marker:
        out << "{\"kind\":\"pathobs.opaque\",\"type\":\"" << to_string(record.type)
            << "\",\"sequence\":" << record.sequence << ",\"bytes\":" << record.payload.size()
            << "}";
        break;
    }
  }
  out << "]}";

  const std::string document = out.str();
  if (options.has("out")) {
    std::ofstream stream(options.get("out", std::string{}), std::ios::binary);
    if (!stream) {
      std::fprintf(stderr, "pathobs: cannot write %s\n", options.get("out", "").c_str());
      return 1;
    }
    stream << document;
  } else {
    std::cout << document << std::endl;
  }
  return 0;
}

int command_validate(const Options& options) {
  StoreOptions store_options;
  store_options.limits = default_limits();
  store_options.read_only = true;
  Result<PersistentStore> store =
      PersistentStore::open(options.get("store", "pathobs-store.pob"), store_options,
                            TimePoint::from_unix_nanos(1767225600000000000LL));
  if (!store.has_value()) {
    return fail(store.error());
  }
  Result<std::vector<StoredRecord>> records = store->read_all();
  if (!records.has_value()) {
    return fail(records.error());
  }
  std::cout << documents::to_json(store->recovery(), true) << std::endl;
  std::printf("validated %zu records from %s\n", records.value().size(),
              store->path().string().c_str());
  return 0;
}

int command_selftest() {
  std::printf("Path Observatory self test (%s)\n", build_description().c_str());
  int failures = 0;
  const auto check = [&failures](bool condition, const char* what) {
    std::printf("  [%s] %s\n", condition ? "ok" : "FAIL", what);
    if (!condition) {
      ++failures;
    }
  };

  const Limits limits = default_limits();
  check(limits.validate().has_value(), "default limits validate");
  Limits beyond = limits;
  beyond.max_hops_per_path = Limits::hard_ceiling().max_hops_per_path + 1;
  check(!beyond.validate().has_value(), "a limit above the hard ceiling is rejected");

  Sha256 sha;
  sha.update(std::string_view("abc"));
  check(Digest::from_bytes(sha.finish().data()).hex() ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 matches the published vector for abc");
  check(crc32c(std::string_view("123456789")) == 0xe3069283u, "CRC-32C check value");

  Result<JsonValue> parsed = JsonValue::parse("{\"a\":[1,2,3]}", limits);
  check(parsed.has_value(), "a small JSON document parses");
  Result<JsonValue> duplicate = JsonValue::parse("{\"a\":1,\"a\":2}", limits);
  check(!duplicate.has_value(), "a duplicate JSON key is rejected");

  std::size_t descriptors = 0;
  all_divergence_descriptors(&descriptors);
  check(descriptors == 26, "the divergence class list is closed and complete");

  synthetic::LabOptions lab;
  Result<synthetic::LabFabric> fabric = synthetic::build_lab_fabric(lab, limits);
  check(fabric.has_value(), "the synthetic laboratory fabric builds");

  std::printf("%s\n", failures == 0 ? "self test passed" : "self test FAILED");
  return failures == 0 ? 0 : 1;
}

int command_serve(const Options& options) {
  const bool pretty = options.has("pretty");
  transport::ServerOptions server_options;
  server_options.limits = default_limits();
  server_options.port = static_cast<std::uint16_t>(options.number("port", 0));
  Result<transport::TransportServer> server = transport::TransportServer::start(server_options);
  if (!server.has_value()) {
    return fail(server.error());
  }
  std::printf("listening on 127.0.0.1:%u\n", static_cast<unsigned>(server.value().port()));
  std::fflush(stdout);

  Result<std::unique_ptr<ObservatoryRuntime>> created = make_runtime(
      options, TimePoint::from_unix_nanos(1767225600000000000LL), options.has("store"));
  if (!created.has_value()) {
    return fail(created.error());
  }
  std::unique_ptr<ObservatoryRuntime> runtime = std::move(created).value();

  const std::uint64_t connections = options.number("count", 1);
  std::uint64_t accepted = 0;
  for (std::uint64_t i = 0; i < connections; ++i) {
    Result<transport::FramedChannel> channel = server.value().accept();
    if (!channel.has_value()) {
      std::fprintf(stderr, "pathobs: accept failed: %s\n", describe(channel.error()).c_str());
      break;
    }
    ++accepted;
    for (;;) {
      Result<transport::Frame> frame = channel.value().receive();
      if (!frame.has_value()) {
        if (frame.error().code != ErrorCode::ConnectionClosed) {
          std::fprintf(stderr, "pathobs: %s\n", describe(frame.error()).c_str());
        }
        break;
      }
      if (frame.value().type == transport::FrameType::Bye) {
        break;
      }
      if (frame.value().type == transport::FrameType::Hello) {
        Result<SourceDescriptor> descriptor = codec::decode_exact<SourceDescriptor>(
            std::span<const std::byte>(frame.value().payload.data(), frame.value().payload.size()),
            server_options.limits);
        if (descriptor.has_value()) {
          const Status registered = runtime->register_source(descriptor.value());
          static_cast<void>(registered);
        }
        continue;
      }
      if (frame.value().type == transport::FrameType::Evidence) {
        Result<ObservedPathEvidence> evidence = codec::decode_exact<ObservedPathEvidence>(
            std::span<const std::byte>(frame.value().payload.data(), frame.value().payload.size()),
            server_options.limits);
        if (!evidence.has_value()) {
          std::fprintf(stderr, "pathobs: bad evidence frame: %s\n",
                       describe(evidence.error()).c_str());
          continue;
        }
        const Result<IngestResult> ingested = runtime->ingest_evidence(evidence.value());
        if (!ingested.has_value()) {
          return fail(ingested.error());
        }
        const std::string ack = ingested.value().accepted ? "accepted" : "rejected";
        const Status sent = channel.value().send(
            transport::FrameType::Ack,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(ack.data()),
                                       ack.size()));
        static_cast<void>(sent);
        continue;
      }
    }
    channel.value().close();
  }

  Result<std::vector<ComparisonResult>> results = runtime->compare_all();
  if (!results.has_value()) {
    return fail(results.error());
  }
  for (const auto& result : results.value()) {
    std::cout << documents::to_json(result, pretty) << std::endl;
  }
  const Status stopped = runtime->stop();
  if (!stopped.has_value()) {
    return fail(stopped.error());
  }
  std::printf("served %llu connection(s)\n", static_cast<unsigned long long>(accepted));
  return accepted == connections ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    return usage();
  }
  const std::string command = argv[1];
  const Options options = parse_options(argc, argv, 2);

  if (command == "version") {
    std::printf("%s\n", build_description().c_str());
    std::printf("persistence format version %d\n", PATHOBS_STORE_FORMAT_VERSION);
    std::printf("wire protocol version %d\n", PATHOBS_WIRE_PROTOCOL_VERSION);
    std::printf("address sanitizer: %s\n", built_with_address_sanitizer() ? "yes" : "no");
    return 0;
  }
  if (command == "selftest") {
    return command_selftest();
  }
  if (command == "compare") {
    return command_compare(options, false);
  }
  if (command == "explain") {
    return command_compare(options, true);
  }
  if (command == "ingest") {
    return command_ingest(options);
  }
  if (command == "history") {
    return command_history(options);
  }
  if (command == "export") {
    return command_export(options);
  }
  if (command == "validate") {
    return command_validate(options);
  }
  if (command == "serve") {
    return command_serve(options);
  }
  return usage();
}
