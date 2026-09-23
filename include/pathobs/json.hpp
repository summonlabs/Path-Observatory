// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.
//
// A strict, bounded JSON reader and a deterministic JSON writer.
//
// The reader is the ingest boundary for routing authority documents and
// observation documents, so it is treated as hostile input handling: every
// bound is enforced before an allocation, duplicate object keys are rejected so
// that a document cannot mean two things at once, numbers keep their original
// lexeme so a 64 bit identity never round trips through a double, and invalid
// UTF-8 or a malformed escape is an error rather than a replacement character.

#ifndef PATHOBS_JSON_HPP
#define PATHOBS_JSON_HPP

#include "pathobs/error.hpp"
#include "pathobs/export.hpp"
#include "pathobs/limits.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pathobs {

class JsonValue {
 public:
  enum class Kind : std::uint8_t { Null = 0, Boolean = 1, Number = 2, String = 3, Array = 4, Object = 5 };

  JsonValue() = default;

  static JsonValue make_null();
  static JsonValue make_boolean(bool value);
  static JsonValue make_number(std::int64_t value);
  static JsonValue make_number(std::uint64_t value);
  static JsonValue make_number(double value);
  static JsonValue make_string(std::string value);
  static JsonValue make_array();
  static JsonValue make_object();
  /// Number built from a validated JSON number lexeme. The reader uses this so
  /// that the exact digits the document contained survive into the tree.
  static JsonValue make_number_lexeme(std::string lexeme);

  Kind kind() const noexcept { return kind_; }
  bool is_null() const noexcept { return kind_ == Kind::Null; }
  bool is_boolean() const noexcept { return kind_ == Kind::Boolean; }
  bool is_number() const noexcept { return kind_ == Kind::Number; }
  bool is_string() const noexcept { return kind_ == Kind::String; }
  bool is_array() const noexcept { return kind_ == Kind::Array; }
  bool is_object() const noexcept { return kind_ == Kind::Object; }

  bool as_boolean() const noexcept { return kind_ == Kind::Boolean && boolean_; }
  const std::string& as_string() const noexcept { return text_; }
  const std::string& number_text() const noexcept { return text_; }

  Result<std::uint64_t> as_u64() const;
  Result<std::int64_t> as_i64() const;
  Result<double> as_double() const;
  Result<bool> as_bool_checked() const;

  const std::vector<JsonValue>& items() const noexcept { return items_; }
  const std::vector<std::pair<std::string, JsonValue>>& fields() const noexcept { return fields_; }
  std::size_t size() const noexcept;

  /// Object lookup. Returns nullptr when absent or when this is not an object.
  const JsonValue* find(std::string_view key) const noexcept;

  void push_back(JsonValue value);
  void set(std::string key, JsonValue value);

  static Result<JsonValue> parse(std::string_view text, const Limits& limits);

  std::string dump(bool pretty = false) const;
  void dump_to(std::string& out, bool pretty) const;

 private:
  void dump_into(std::string& out, bool pretty, int indent) const;

  Kind kind_{Kind::Null};
  bool boolean_{false};
  std::string text_{};
  std::vector<JsonValue> items_{};
  std::vector<std::pair<std::string, JsonValue>> fields_{};
};

/// True when the byte sequence is well formed UTF-8 with no overlong form, no
/// surrogate code point and no code point above U+10FFFF.
PATHOBS_API bool is_valid_utf8(std::string_view text) noexcept;

/// Append one code point as UTF-8. Returns false when the code point is not a
/// legal scalar value.
PATHOBS_API bool append_utf8(std::string& out, std::uint32_t code_point);

} // namespace pathobs

#endif // PATHOBS_JSON_HPP
