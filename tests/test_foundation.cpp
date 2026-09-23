// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "test_framework.hpp"

#include "pathobs/canonical.hpp"
#include "pathobs/checked.hpp"
#include "pathobs/digest.hpp"
#include "pathobs/documents.hpp"
#include "pathobs/json.hpp"
#include "pathobs/limits.hpp"
#include "pathobs/random.hpp"
#include "pathobs/strong_id.hpp"
#include "pathobs/synthetic.hpp"
#include "pathobs/time.hpp"
#include "pathobs/version.hpp"

#include <array>
#include <limits>
#include <string>
#include <vector>

using namespace pathobs;

PATHOBS_TEST(foundation, digest_matches_published_vectors) {
  struct Vector {
    const char* input;
    const char* hex;
  };
  const Vector vectors[] = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
  };
  for (const auto& vector : vectors) {
    CHECK_EQ(Digest::of(std::string_view(vector.input)).hex(), std::string(vector.hex));
  }

  // A single byte fed one at a time must produce the same digest as one call.
  Sha256 split;
  const std::string alphabet = "abcdefghijklmnopqrstuvwxyz";
  for (const char c : alphabet) {
    split.update(std::string_view(&c, 1));
  }
  Sha256 whole;
  whole.update(std::string_view(alphabet));
  CHECK_EQ(Digest::from_bytes(split.finish().data()).hex(),
           Digest::from_bytes(whole.finish().data()).hex());
}

PATHOBS_TEST(foundation, crc32c_check_value) {
  CHECK_EQ(crc32c(std::string_view("123456789")), 0xe3069283u);
  CHECK_EQ(crc32c(std::string_view("")), 0u);
}

PATHOBS_TEST(foundation, checked_arithmetic_reports_overflow) {
  CHECK(checked_add<std::uint32_t>(1u, 2u).value() == 3u);
  CHECK(!checked_add<std::uint32_t>(std::numeric_limits<std::uint32_t>::max(), 1u).has_value());
  CHECK(!checked_sub<std::uint32_t>(0u, 1u).has_value());
  CHECK(!checked_mul<std::uint32_t>(0x10000u, 0x10000u).has_value());
  CHECK_EQ(checked_mul<std::uint32_t>(0x1000u, 0x10u).value(), 0x10000u);
  CHECK(!checked_add<std::int32_t>(std::numeric_limits<std::int32_t>::max(), 1).has_value());
  CHECK(!checked_add<std::int32_t>(std::numeric_limits<std::int32_t>::min(), -1).has_value());
  CHECK_EQ(checked_mul<std::int32_t>(-3, 4).value(), -12);
  CHECK(!checked_mul<std::int32_t>(std::numeric_limits<std::int32_t>::min(), -1).has_value());
  CHECK(!checked_cast<std::uint8_t>(256).has_value());
  CHECK_EQ(checked_cast<std::uint8_t>(255).value(), 255u);
  CHECK(!checked_cast<std::uint8_t>(-1).has_value());
  CHECK(fits_within(1024, 1024, 1024ull * 1024ull));
  CHECK(!fits_within(1024, 1025, 1024ull * 1024ull));
}

PATHOBS_TEST(foundation, strong_identity_rejects_zero_and_parses_decimal) {
  CHECK(!SourceId{}.valid());
  CHECK_EQ(SourceId::from_value(7).value(), 7u);
  CHECK_EQ(SourceId::from_value(7).str(), std::string("7"));
  CHECK(SourceId::parse("42").has_value());
  CHECK(!SourceId::parse("0").has_value());
  CHECK(!SourceId::parse("").has_value());
  CHECK(!SourceId::parse("4x").has_value());
  CHECK(!SourceId::parse("42 ").has_value());

  const auto parsed = EvidenceId::parse(std::string(32, 'a'));
  CHECK(parsed.has_value());
  CHECK_EQ(parsed.value().str(), std::string(32, 'a'));
  CHECK(!EvidenceId::parse(std::string(31, 'a')).has_value());
  CHECK(!EvidenceId::parse(std::string(32, 'z')).has_value());
  CHECK(!EvidenceId{}.valid());
}

PATHOBS_TEST(foundation, time_round_trips_and_rejects_bad_input) {
  const auto parsed = TimePoint::parse_rfc3339("2026-02-14T03:04:05.123456789Z");
  CHECK(parsed.has_value());
  CHECK_EQ(parsed.value().to_rfc3339(), std::string("2026-02-14T03:04:05.123456789Z"));
  CHECK_EQ(TimePoint::parse_rfc3339(parsed.value().to_rfc3339()).value().unix_nanos(),
           parsed.value().unix_nanos());

  const auto whole = TimePoint::parse_rfc3339("1970-01-01T00:00:00Z");
  CHECK(whole.has_value());
  CHECK_EQ(whole.value().unix_nanos(), 0);
  CHECK(whole.value().is_set());
  CHECK(!TimePoint::unset().is_set());
  CHECK_EQ(TimePoint::unset().to_rfc3339(), std::string("unset"));

  CHECK(!TimePoint::parse_rfc3339("2026-02-14T03:04:05+01:00").has_value());
  CHECK(!TimePoint::parse_rfc3339("2026-13-01T00:00:00Z").has_value());
  CHECK(!TimePoint::parse_rfc3339("2026-02-30T00:00:00Z").has_value());
  CHECK(!TimePoint::parse_rfc3339("2026-02-14T25:00:00Z").has_value());
  CHECK(!TimePoint::parse_rfc3339("").has_value());
  CHECK(!TimePoint::parse_rfc3339("2026-02-14T03:04:05").has_value());
}

PATHOBS_TEST(foundation, random_is_reproducible_and_unbiased_in_range) {
  Rng first(20260214);
  Rng second(20260214);
  for (int i = 0; i < 256; ++i) {
    CHECK_EQ(first.next_u64(), second.next_u64());
  }
  Rng bounded(7);
  for (int i = 0; i < 4096; ++i) {
    const std::uint64_t value = bounded.next_below(10);
    CHECK(value < 10);
  }
  CHECK_EQ(bounded.next_below(0), 0u);
  CHECK_EQ(bounded.next_in_range(5, 4), 5u);
  std::vector<int> items = {1, 2, 3, 4, 5, 6, 7, 8};
  Rng shuffle_one(1);
  Rng shuffle_two(1);
  std::vector<int> copy = items;
  shuffle_one.shuffle(items);
  shuffle_two.shuffle(copy);
  CHECK(items == copy);
}

PATHOBS_TEST(foundation, limits_reject_a_configuration_above_the_ceiling) {
  CHECK(default_limits().validate().has_value());
  Limits limits = default_limits();
  limits.max_history_per_group = limits.max_history_records + 1;
  CHECK(!limits.validate().has_value());
  limits = default_limits();
  limits.worker_threads = 0;
  CHECK(!limits.validate().has_value());
  limits = default_limits();
  limits.max_store_bytes = Limits::hard_ceiling().max_store_bytes + 1;
  CHECK(!limits.validate().has_value());
}

PATHOBS_TEST(foundation, canonical_encoding_is_stable_and_bounded) {
  CanonicalWriter writer;
  writer.text("pathobs.test");
  writer.u8(1);
  writer.u16(2);
  writer.u32(3);
  writer.u64(4);
  writer.i64(-5);
  writer.boolean(true);
  writer.id(SourceId::from_value(9));
  const std::string payload = writer.take();

  CanonicalReader reader(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
  CHECK_EQ(reader.text(64).value(), std::string("pathobs.test"));
  CHECK_EQ(reader.u8().value(), 1u);
  CHECK_EQ(reader.u16().value(), 2u);
  CHECK_EQ(reader.u32().value(), 3u);
  CHECK_EQ(reader.u64().value(), 4u);
  CHECK_EQ(reader.i64().value(), -5);
  CHECK_EQ(reader.boolean().value(), true);
  CHECK_EQ(reader.id<SourceIdTag>().value().value(), 9u);
  CHECK(reader.expect_end().has_value());

  // A length prefix beyond the bound must be refused before allocating.
  CanonicalWriter big;
  big.u32(0xFFFFFFFFu);
  CanonicalReader refusing(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(big.buffer().data()), big.buffer().size()));
  const auto refused = refusing.bytes(1024);
  CHECK(!refused.has_value());
  CHECK_EQ(static_cast<int>(refused.error().code),
           static_cast<int>(ErrorCode::CapacityExceeded));

  // A truncated payload is truncated input, not a silent zero.
  CanonicalReader short_reader(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(payload.data()), 4));
  CHECK(!short_reader.text(64).has_value());
}

PATHOBS_TEST(json, parses_and_dumps_deterministically) {
  const Limits limits = default_limits();
  const char* document = R"json({"b":[1,2,{"c":null}],"a":true,"n":18446744073709551615})json";
  const auto parsed = JsonValue::parse(document, limits);
  REQUIRE(parsed.has_value());
  CHECK(parsed.value().is_object());
  CHECK(parsed.value().find("a") != nullptr);
  CHECK_EQ(parsed.value().find("n")->as_u64().value(), 18446744073709551615ull);
  CHECK(parsed.value().find("missing") == nullptr);

  const std::string compact = parsed.value().dump(false);
  const auto reparsed = JsonValue::parse(compact, limits);
  REQUIRE(reparsed.has_value());
  CHECK_EQ(reparsed.value().dump(false), compact);
  const std::string pretty = parsed.value().dump(true);
  const auto recompacted = JsonValue::parse(pretty, limits);
  REQUIRE(recompacted.has_value());
  CHECK_EQ(recompacted.value().dump(false), compact);
}

PATHOBS_TEST(json, rejects_adversarial_documents) {
  Limits limits = default_limits();

  CHECK(!JsonValue::parse(R"json({"a":1,"a":2})json", limits).has_value());
  CHECK(!JsonValue::parse(R"json({"a":1,})json", limits).has_value());
  CHECK(!JsonValue::parse(R"json({"a":01})json", limits).has_value());
  CHECK(!JsonValue::parse(R"json({"a":1e})json", limits).has_value());
  // The backslash is built from its code point so that the escape sequences
  // below are unambiguous in the source.
  const char escape = static_cast<char>(92);
  const std::string bad_escape =
      std::string(R"json({"a":")json") + escape + R"json(q"})json";
  CHECK(!JsonValue::parse(bad_escape, limits).has_value());
  const std::string bad_surrogate =
      std::string(R"json({"a":")json") + escape + R"json(ud800"})json";
  CHECK(!JsonValue::parse(bad_surrogate, limits).has_value());
  const std::string bad_low_surrogate =
      std::string(R"json({"a":")json") + escape + R"json(udc00"})json";
  CHECK(!JsonValue::parse(bad_low_surrogate, limits).has_value());
  CHECK(!JsonValue::parse(R"json({"a":tru})json", limits).has_value());
  CHECK(!JsonValue::parse(R"json([1,2)json", limits).has_value());
  CHECK(!JsonValue::parse(R"json({"a":1} trailing)json", limits).has_value());
  CHECK(!JsonValue::parse("", limits).has_value());

  // A raw control character inside a string is refused.
  std::string control = R"json({"a":")json";
  control.push_back(0x01);
  control += R"json("})json";
  CHECK(!JsonValue::parse(control, limits).has_value());

  // Invalid UTF-8 is refused rather than replaced.
  std::string invalid_utf8 = R"json({"a":")json";
  invalid_utf8.push_back(static_cast<char>(0xC0));
  invalid_utf8.push_back(static_cast<char>(0xAF));
  invalid_utf8 += R"json("})json";
  CHECK(!JsonValue::parse(invalid_utf8, limits).has_value());

  // A deep document is refused rather than exhausting the stack.
  std::string deep;
  deep.reserve(400000);
  for (int i = 0; i < 200000; ++i) {
    deep.push_back('[');
  }
  for (int i = 0; i < 200000; ++i) {
    deep.push_back(']');
  }
  const auto deep_result = JsonValue::parse(deep, limits);
  CHECK(!deep_result.has_value());

  // A well formed surrogate pair is accepted and encoded as four UTF-8 bytes.
  const std::string surrogate_pair = std::string(R"json({"a":")json") + escape +
                                     R"json(ud83d)json" + escape + R"json(ude00"})json";
  const auto pair = JsonValue::parse(surrogate_pair, limits);
  REQUIRE(pair.has_value());
  CHECK_EQ(pair.value().find("a")->as_string().size(), 4u);

  // A string above the configured bound is refused.
  Limits small = limits;
  small.max_string_bytes = 8;
  CHECK(!JsonValue::parse(R"json({"a":"0123456789"})json", small).has_value());
}

PATHOBS_TEST(json, number_text_is_preserved_exactly) {
  const Limits limits = default_limits();
  const auto parsed = JsonValue::parse(R"json([-0, 1e3, 0.5, 18446744073709551615])json", limits);
  REQUIRE(parsed.has_value());
  CHECK_EQ(parsed.value().items()[0].number_text(), std::string("-0"));
  CHECK_EQ(parsed.value().items()[1].number_text(), std::string("1e3"));
  CHECK(!parsed.value().items()[1].as_u64().has_value());
  CHECK_EQ(parsed.value().items()[2].as_double().value(), 0.5);
  CHECK_EQ(parsed.value().items()[3].as_u64().value(), 18446744073709551615ull);
  CHECK(!parsed.value().items()[3].as_i64().has_value());
}

PATHOBS_TEST(json, utf8_validation_is_strict) {
  CHECK(is_valid_utf8("plain ascii"));
  CHECK(is_valid_utf8("\xc3\xa9"));
  CHECK(!is_valid_utf8("\xc0\xaf"));      // overlong
  CHECK(!is_valid_utf8("\xed\xa0\x80"));  // surrogate
  CHECK(!is_valid_utf8("\xf5\x80\x80\x80"));
  CHECK(!is_valid_utf8("\xe2\x82"));      // truncated
  std::string out;
  CHECK(append_utf8(out, 0x41));
  CHECK_EQ(out.size(), 1u);
  CHECK(!append_utf8(out, 0xD800));
  CHECK(!append_utf8(out, 0x110000));
}

PATHOBS_TEST(documents, route_and_topology_documents_round_trip) {
  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.multipath_members = 2;
  const auto fabric = synthetic::build_lab_fabric(lab, limits);
  REQUIRE(fabric.has_value());

  const std::string topology_text = documents::to_json(fabric.value().topology, false);
  const auto topology = documents::parse_topology_generation(topology_text, limits);
  REQUIRE(topology.has_value());
  CHECK_EQ(topology.value().digest.hex(), fabric.value().topology.digest.hex());

  const std::string routes_text = documents::to_json(fabric.value().routes, false);
  const auto routes = documents::parse_route_generation(routes_text, limits);
  REQUIRE(routes.has_value());
  CHECK_EQ(routes.value().paths.size(), fabric.value().routes.paths.size());
  CHECK_EQ(routes.value().groups.size(), 1u);

  const std::string source_text = documents::to_json(fabric.value().sources.front(), false);
  const auto source = documents::parse_source_descriptor(source_text, limits);
  REQUIRE(source.has_value());
  CHECK_EQ(source.value().id.value(), fabric.value().sources.front().id.value());
}

PATHOBS_TEST(documents, evidence_document_round_trips_and_lines_parse) {
  const Limits limits = default_limits();
  synthetic::LabOptions lab;
  lab.evidence_count = 3;
  lab.omit_links = true;
  const auto fabric = synthetic::build_lab_fabric(lab, limits);
  REQUIRE(fabric.has_value());

  const std::string text = documents::to_json(fabric.value().evidence.front(), false);
  const auto evidence = documents::parse_evidence(text, limits);
  REQUIRE(evidence.has_value());
  CHECK_EQ(evidence.value().id.str(), fabric.value().evidence.front().id.str());

  std::string lines = "# a comment\n";
  for (const auto& item : fabric.value().evidence) {
    lines += documents::to_json(item, false);
    lines += "\n";
  }
  lines += "\n";
  const auto parsed = documents::parse_evidence_lines(lines, limits);
  REQUIRE(parsed.has_value());
  CHECK_EQ(parsed.value().size(), 3u);

  const auto broken = documents::parse_evidence_lines("{\"kind\":\"pathobs.evidence\"}", limits);
  CHECK(!broken.has_value());
}

PATHOBS_TEST(documents, a_document_must_declare_its_kind) {
  const Limits limits = default_limits();
  CHECK(!documents::parse_route_generation(R"json({"generation":1})json", limits).has_value());
  CHECK(!documents::parse_route_generation(
             R"json({"kind":"pathobs.topology_generation"})json", limits)
             .has_value());
  const auto wrong = documents::parse_source_descriptor(
      R"json({"kind":"something.else","id":1})json", limits);
  CHECK(!wrong.has_value());
}
