// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs.

#include "pathobs/json.hpp"

#include <charconv>
#include <cmath>

namespace pathobs {

bool append_utf8(std::string& out, std::uint32_t code_point) {
  if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu)) {
    return false;
  }
  if (code_point < 0x80u) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800u) {
    out.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
  } else if (code_point < 0x10000u) {
    out.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
  }
  return true;
}

bool is_valid_utf8(std::string_view text) noexcept {
  std::size_t i = 0;
  const std::size_t size = text.size();
  while (i < size) {
    const auto byte = static_cast<std::uint8_t>(text[i]);
    std::size_t extra = 0;
    std::uint32_t code_point = 0;
    if (byte < 0x80u) {
      ++i;
      continue;
    } else if ((byte & 0xE0u) == 0xC0u) {
      extra = 1;
      code_point = byte & 0x1Fu;
    } else if ((byte & 0xF0u) == 0xE0u) {
      extra = 2;
      code_point = byte & 0x0Fu;
    } else if ((byte & 0xF8u) == 0xF0u) {
      extra = 3;
      code_point = byte & 0x07u;
    } else {
      return false;
    }
    if (i + extra >= size) {
      return false;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      const auto continuation = static_cast<std::uint8_t>(text[i + k]);
      if ((continuation & 0xC0u) != 0x80u) {
        return false;
      }
      code_point = (code_point << 6) | (continuation & 0x3Fu);
    }
    // Reject overlong encodings, surrogates and out of range code points.
    const std::uint32_t minimum = extra == 1 ? 0x80u : (extra == 2 ? 0x800u : 0x10000u);
    if (code_point < minimum || code_point > 0x10FFFFu ||
        (code_point >= 0xD800u && code_point <= 0xDFFFu)) {
      return false;
    }
    i += extra + 1;
  }
  return true;
}

namespace {

/// The escape character, written without a character literal so that the
/// escaping logic below contains no nested escapes of its own.
constexpr char kEscape = static_cast<char>(92);
constexpr char kQuote = static_cast<char>(34);

char hex_digit(unsigned value) {
  return value < 10u ? static_cast<char>('0' + value) : static_cast<char>('a' + (value - 10u));
}

const char* short_escape(unsigned char byte) {
  switch (byte) {
    case 0x08u:
      return "b";
    case 0x0Cu:
      return "f";
    case 0x0Au:
      return "n";
    case 0x0Du:
      return "r";
    case 0x09u:
      return "t";
    default:
      return nullptr;
  }
}

void escape_into(std::string& out, std::string_view text) {
  out.push_back(kQuote);
  for (const char raw : text) {
    const auto byte = static_cast<unsigned char>(raw);
    if (raw == kQuote || raw == kEscape) {
      out.push_back(kEscape);
      out.push_back(raw);
      continue;
    }
    if (byte < 0x20u) {
      out.push_back(kEscape);
      if (const char* short_form = short_escape(byte); short_form != nullptr) {
        out += short_form;
      } else {
        out.push_back('u');
        out.push_back('0');
        out.push_back('0');
        out.push_back(hex_digit((byte >> 4) & 0x0Fu));
        out.push_back(hex_digit(byte & 0x0Fu));
      }
      continue;
    }
    out.push_back(raw);
  }
  out.push_back(kQuote);
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

class Parser {
 public:
  Parser(std::string_view text, const Limits& limits) : text_(text), limits_(limits) {}

  Result<JsonValue> run() {
    if (text_.size() > limits_.max_document_bytes) {
      return Error{ErrorCode::CapacityExceeded, "document exceeds the configured byte bound"};
    }
    skip_whitespace();
    JsonValue value = parse_value(0);
    if (failed_) {
      return error_;
    }
    skip_whitespace();
    if (!at_end()) {
      return Error{ErrorCode::InvalidFormat, "document has trailing content"};
    }
    return value;
  }

 private:
  bool at_end() const noexcept { return position_ >= text_.size(); }
  char peek() const noexcept { return at_end() ? '\0' : text_[position_]; }
  char peek(std::size_t ahead) const noexcept {
    return position_ + ahead >= text_.size() ? '\0' : text_[position_ + ahead];
  }

  void fail(ErrorCode code, std::string message) {
    if (!failed_) {
      failed_ = true;
      error_ = Error{code, std::move(message)};
    }
  }

  void skip_whitespace() {
    while (!at_end()) {
      const char c = text_[position_];
      if (c != ' ' && c != 0x09 && c != 0x0A && c != 0x0D) {
        break;
      }
      ++position_;
    }
  }

  bool consume(char expected) {
    if (peek() == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  bool consume_literal(std::string_view literal) {
    if (text_.size() - position_ < literal.size()) {
      fail(ErrorCode::InvalidFormat, "document ended inside a literal");
      return false;
    }
    if (text_.compare(position_, literal.size(), literal) != 0) {
      fail(ErrorCode::InvalidFormat, "malformed literal");
      return false;
    }
    position_ += literal.size();
    return true;
  }

  JsonValue parse_value(std::size_t depth) {
    if (failed_) {
      return JsonValue{};
    }
    if (depth > limits_.max_json_depth) {
      fail(ErrorCode::InvalidFormat, "document nesting exceeds the configured depth bound");
      return JsonValue{};
    }
    if (++nodes_ > limits_.max_json_nodes) {
      fail(ErrorCode::CapacityExceeded, "document exceeds the configured node bound");
      return JsonValue{};
    }
    switch (peek()) {
      case '{':
        return parse_object(depth);
      case '[':
        return parse_array(depth);
      case '"': {
        std::string text;
        if (!parse_string(text)) {
          return JsonValue{};
        }
        return JsonValue::make_string(std::move(text));
      }
      case 't':
        return consume_literal("true") ? JsonValue::make_boolean(true) : JsonValue{};
      case 'f':
        return consume_literal("false") ? JsonValue::make_boolean(false) : JsonValue{};
      case 'n':
        return consume_literal("null") ? JsonValue::make_null() : JsonValue{};
      default:
        break;
    }
    if (peek() == '-' || is_digit(peek())) {
      std::string lexeme;
      if (!parse_number(lexeme)) {
        return JsonValue{};
      }
      return JsonValue::make_number_lexeme(std::move(lexeme));
    }
    fail(ErrorCode::InvalidFormat, "unexpected character in document");
    return JsonValue{};
  }

  JsonValue parse_object(std::size_t depth) {
    ++position_;  // consume the opening brace
    JsonValue object = JsonValue::make_object();
    skip_whitespace();
    if (consume('}')) {
      return object;
    }
    for (;;) {
      skip_whitespace();
      if (peek() != '"') {
        fail(ErrorCode::InvalidFormat, "object key is not a string");
        return JsonValue{};
      }
      std::string key;
      if (!parse_string(key)) {
        return JsonValue{};
      }
      skip_whitespace();
      if (!consume(':')) {
        fail(ErrorCode::InvalidFormat, "object key is not followed by a colon");
        return JsonValue{};
      }
      skip_whitespace();
      JsonValue value = parse_value(depth + 1);
      if (failed_) {
        return JsonValue{};
      }
      if (object.find(key) != nullptr) {
        fail(ErrorCode::InvalidFormat, "object contains a duplicate key");
        return JsonValue{};
      }
      object.set(std::move(key), std::move(value));
      skip_whitespace();
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        break;
      }
      fail(ErrorCode::InvalidFormat, "object is not terminated");
      return JsonValue{};
    }
    return object;
  }

  JsonValue parse_array(std::size_t depth) {
    ++position_;  // consume the opening bracket
    JsonValue array = JsonValue::make_array();
    skip_whitespace();
    if (consume(']')) {
      return array;
    }
    for (;;) {
      skip_whitespace();
      JsonValue value = parse_value(depth + 1);
      if (failed_) {
        return JsonValue{};
      }
      array.push_back(std::move(value));
      skip_whitespace();
      if (consume(',')) {
        continue;
      }
      if (consume(']')) {
        break;
      }
      fail(ErrorCode::InvalidFormat, "array is not terminated");
      return JsonValue{};
    }
    return array;
  }

  bool parse_string(std::string& out) {
    ++position_;  // consume the opening quote
    out.clear();
    for (;;) {
      if (at_end()) {
        fail(ErrorCode::InvalidFormat, "string is not terminated");
        return false;
      }
      const char raw = text_[position_];
      const auto byte = static_cast<unsigned char>(raw);
      if (raw == kQuote) {
        ++position_;
        break;
      }
      if (byte < 0x20u) {
        fail(ErrorCode::InvalidFormat, "string contains an unescaped control character");
        return false;
      }
      if (raw != kEscape) {
        out.push_back(raw);
        ++position_;
        if (out.size() > limits_.max_string_bytes) {
          fail(ErrorCode::CapacityExceeded, "string exceeds the configured bound");
          return false;
        }
        continue;
      }
      // Escape sequence.
      ++position_;
      if (at_end()) {
        fail(ErrorCode::InvalidFormat, "string ends inside an escape sequence");
        return false;
      }
      const char kind = text_[position_++];
      switch (kind) {
        case 'b':
          out.push_back(static_cast<char>(0x08));
          break;
        case 'f':
          out.push_back(static_cast<char>(0x0C));
          break;
        case 'n':
          out.push_back(static_cast<char>(0x0A));
          break;
        case 'r':
          out.push_back(static_cast<char>(0x0D));
          break;
        case 't':
          out.push_back(static_cast<char>(0x09));
          break;
        case 'u': {
          std::uint32_t code_point = 0;
          if (!read_hex4(code_point)) {
            return false;
          }
          if (code_point >= 0xD800u && code_point <= 0xDBFFu) {
            if (peek() != kEscape || peek(1) != 'u') {
              fail(ErrorCode::InvalidFormat, "high surrogate is not followed by a low surrogate");
              return false;
            }
            position_ += 2;
            std::uint32_t low = 0;
            if (!read_hex4(low)) {
              return false;
            }
            if (low < 0xDC00u || low > 0xDFFFu) {
              fail(ErrorCode::InvalidFormat, "surrogate pair is malformed");
              return false;
            }
            code_point = 0x10000u + ((code_point - 0xD800u) << 10) + (low - 0xDC00u);
          } else if (code_point >= 0xDC00u && code_point <= 0xDFFFu) {
            fail(ErrorCode::InvalidFormat, "unpaired low surrogate");
            return false;
          }
          if (!append_utf8(out, code_point)) {
            fail(ErrorCode::InvalidFormat, "escape sequence is not a legal scalar value");
            return false;
          }
          break;
        }
        case '"':
          out.push_back(kQuote);
          break;
        case kEscape:
          out.push_back(kEscape);
          break;
        case '/':
          out.push_back('/');
          break;
        default:
          fail(ErrorCode::InvalidFormat, "unknown escape sequence in string");
          return false;
      }
      if (out.size() > limits_.max_string_bytes) {
        fail(ErrorCode::CapacityExceeded, "string exceeds the configured bound");
        return false;
      }
    }
    if (!is_valid_utf8(out)) {
      fail(ErrorCode::InvalidFormat, "string is not valid UTF-8");
      return false;
    }
    return true;
  }

  bool read_hex4(std::uint32_t& out) {
    if (text_.size() - position_ < 4) {
      fail(ErrorCode::InvalidFormat, "unicode escape is truncated");
      return false;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = hex_value(text_[position_ + static_cast<std::size_t>(i)]);
      if (digit < 0) {
        fail(ErrorCode::InvalidFormat, "unicode escape is not hexadecimal");
        return false;
      }
      value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    position_ += 4;
    out = value;
    return true;
  }

  bool parse_number(std::string& out) {
    out.clear();
    const std::size_t start = position_;
    if (consume('-')) {
      // A leading minus must be followed by a digit.
      if (!is_digit(peek())) {
        fail(ErrorCode::InvalidFormat, "number has a sign with no digits");
        return false;
      }
    }
    if (consume('0')) {
      if (is_digit(peek())) {
        fail(ErrorCode::InvalidFormat, "number has a leading zero");
        return false;
      }
    } else {
      if (!is_digit(peek())) {
        fail(ErrorCode::InvalidFormat, "number has no integer part");
        return false;
      }
      while (is_digit(peek())) {
        ++position_;
      }
    }
    if (consume('.')) {
      if (!is_digit(peek())) {
        fail(ErrorCode::InvalidFormat, "number has a fraction with no digits");
        return false;
      }
      while (is_digit(peek())) {
        ++position_;
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      ++position_;
      if (peek() == '+' || peek() == '-') {
        ++position_;
      }
      if (!is_digit(peek())) {
        fail(ErrorCode::InvalidFormat, "number has an exponent with no digits");
        return false;
      }
      while (is_digit(peek())) {
        ++position_;
      }
    }
    out.assign(text_.substr(start, position_ - start));
    return true;
  }

  std::string_view text_;
  const Limits& limits_;
  std::size_t position_{0};
  std::size_t nodes_{0};
  Error error_{};
  bool failed_{false};
};

} // namespace

JsonValue JsonValue::make_number_lexeme(std::string lexeme) {
  JsonValue out;
  out.kind_ = Kind::Number;
  out.text_ = std::move(lexeme);
  return out;
}


JsonValue JsonValue::make_null() { return JsonValue{}; }

JsonValue JsonValue::make_boolean(bool value) {
  JsonValue out;
  out.kind_ = Kind::Boolean;
  out.boolean_ = value;
  return out;
}

JsonValue JsonValue::make_number(std::int64_t value) {
  JsonValue out;
  out.kind_ = Kind::Number;
  char buffer[24]{};
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  out.text_.assign(buffer, static_cast<std::size_t>(result.ptr - buffer));
  return out;
}

JsonValue JsonValue::make_number(std::uint64_t value) {
  JsonValue out;
  out.kind_ = Kind::Number;
  char buffer[24]{};
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  out.text_.assign(buffer, static_cast<std::size_t>(result.ptr - buffer));
  return out;
}

JsonValue JsonValue::make_number(double value) {
  JsonValue out;
  out.kind_ = Kind::Number;
  char buffer[48]{};
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec == std::errc{}) {
    out.text_.assign(buffer, static_cast<std::size_t>(result.ptr - buffer));
  } else {
    out.text_ = "0";
  }
  return out;
}

JsonValue JsonValue::make_string(std::string value) {
  JsonValue out;
  out.kind_ = Kind::String;
  out.text_ = std::move(value);
  return out;
}

JsonValue JsonValue::make_array() {
  JsonValue out;
  out.kind_ = Kind::Array;
  return out;
}

JsonValue JsonValue::make_object() {
  JsonValue out;
  out.kind_ = Kind::Object;
  return out;
}

Result<std::uint64_t> JsonValue::as_u64() const {
  if (kind_ != Kind::Number) {
    return Error{ErrorCode::InvalidFormat, "value is not a number"};
  }
  for (const char c : text_) {
    if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
      return Error{ErrorCode::InvalidFormat, "number is not a non negative integer", text_};
    }
  }
  std::uint64_t value = 0;
  const auto result = std::from_chars(text_.data(), text_.data() + text_.size(), value);
  if (result.ec != std::errc{} || result.ptr != text_.data() + text_.size()) {
    return Error{ErrorCode::OutOfRange, "integer does not fit in 64 unsigned bits", text_};
  }
  return value;
}

Result<std::int64_t> JsonValue::as_i64() const {
  if (kind_ != Kind::Number) {
    return Error{ErrorCode::InvalidFormat, "value is not a number"};
  }
  for (const char c : text_) {
    if (c == '.' || c == 'e' || c == 'E') {
      return Error{ErrorCode::InvalidFormat, "number is not an integer", text_};
    }
  }
  std::int64_t value = 0;
  const auto result = std::from_chars(text_.data(), text_.data() + text_.size(), value);
  if (result.ec != std::errc{} || result.ptr != text_.data() + text_.size()) {
    return Error{ErrorCode::OutOfRange, "integer does not fit in 64 signed bits", text_};
  }
  return value;
}

Result<double> JsonValue::as_double() const {
  if (kind_ != Kind::Number) {
    return Error{ErrorCode::InvalidFormat, "value is not a number"};
  }
  double value = 0;
  const auto result = std::from_chars(text_.data(), text_.data() + text_.size(), value);
  if (result.ec != std::errc{} || result.ptr != text_.data() + text_.size()) {
    return Error{ErrorCode::OutOfRange, "number is not representable as a double", text_};
  }
  if (std::isinf(value) || std::isnan(value)) {
    return Error{ErrorCode::OutOfRange, "number is not finite", text_};
  }
  return value;
}

Result<bool> JsonValue::as_bool_checked() const {
  if (kind_ != Kind::Boolean) {
    return Error{ErrorCode::InvalidFormat, "value is not a boolean"};
  }
  return boolean_;
}

std::size_t JsonValue::size() const noexcept {
  if (kind_ == Kind::Array) {
    return items_.size();
  }
  if (kind_ == Kind::Object) {
    return fields_.size();
  }
  return 0;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
  if (kind_ != Kind::Object) {
    return nullptr;
  }
  for (const auto& field : fields_) {
    if (field.first == key) {
      return &field.second;
    }
  }
  return nullptr;
}

void JsonValue::push_back(JsonValue value) {
  if (kind_ != Kind::Array) {
    kind_ = Kind::Array;
    fields_.clear();
    text_.clear();
  }
  items_.push_back(std::move(value));
}

void JsonValue::set(std::string key, JsonValue value) {
  if (kind_ != Kind::Object) {
    kind_ = Kind::Object;
    items_.clear();
    text_.clear();
  }
  for (auto& field : fields_) {
    if (field.first == key) {
      field.second = std::move(value);
      return;
    }
  }
  fields_.emplace_back(std::move(key), std::move(value));
}

void JsonValue::dump_into(std::string& out, bool pretty, int indent) const {
  switch (kind_) {
    case Kind::Null:
      out += "null";
      return;
    case Kind::Boolean:
      out += boolean_ ? "true" : "false";
      return;
    case Kind::Number:
      out += text_.empty() ? std::string("0") : text_;
      return;
    case Kind::String:
      escape_into(out, text_);
      return;
    case Kind::Array: {
      if (items_.empty()) {
        out += "[]";
        return;
      }
      out.push_back('[');
      for (std::size_t i = 0; i < items_.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        if (pretty) {
          out.push_back(0x0A);
          out.append(static_cast<std::size_t>(indent + 2), ' ');
        }
        items_[i].dump_into(out, pretty, indent + 2);
      }
      if (pretty) {
        out.push_back(0x0A);
        out.append(static_cast<std::size_t>(indent), ' ');
      }
      out.push_back(']');
      return;
    }
    case Kind::Object: {
      if (fields_.empty()) {
        out += "{}";
        return;
      }
      out.push_back('{');
      for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        if (pretty) {
          out.push_back(0x0A);
          out.append(static_cast<std::size_t>(indent + 2), ' ');
        }
        escape_into(out, fields_[i].first);
        out.push_back(':');
        if (pretty) {
          out.push_back(' ');
        }
        fields_[i].second.dump_into(out, pretty, indent + 2);
      }
      if (pretty) {
        out.push_back(0x0A);
        out.append(static_cast<std::size_t>(indent), ' ');
      }
      out.push_back('}');
      return;
    }
  }
}

void JsonValue::dump_to(std::string& out, bool pretty) const { dump_into(out, pretty, 0); }

std::string JsonValue::dump(bool pretty) const {
  std::string out;
  dump_into(out, pretty, 0);
  return out;
}

Result<JsonValue> JsonValue::parse(std::string_view text, const Limits& limits) {
  Parser parser(text, limits);
  return parser.run();
}

} // namespace pathobs
