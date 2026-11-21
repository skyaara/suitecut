// SPDX-License-Identifier: MIT
#include "suitecut/json.hpp"
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#include <limits>

namespace suitecut {
namespace {
[[noreturn]] void wrong_type() { throw std::runtime_error("Incorrect JSON value type"); }
std::size_t utf8_width(std::string_view text, std::size_t at) {
  const auto first = static_cast<unsigned char>(text[at]);
  std::size_t width;
  std::uint32_t code;
  if (first >= 0xc2 && first <= 0xdf) { width = 2; code = first & 0x1f; }
  else if (first >= 0xe0 && first <= 0xef) { width = 3; code = first & 0x0f; }
  else if (first >= 0xf0 && first <= 0xf4) { width = 4; code = first & 7; }
  else throw std::runtime_error("Invalid JSON UTF-8");
  if (text.size() - at < width) throw std::runtime_error("Truncated JSON UTF-8");
  for (std::size_t i = 1; i < width; ++i) {
    const auto next = static_cast<unsigned char>(text[at + i]);
    if ((next & 0xc0) != 0x80) throw std::runtime_error("Invalid JSON UTF-8 continuation");
    code = (code << 6) | (next & 0x3f);
  }
  if ((width == 2 && code < 0x80) || (width == 3 && code < 0x800) ||
      (width == 4 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
    throw std::runtime_error("Invalid JSON UTF-8 scalar");
  return width;
}
class Parser {
 public:
  Parser(std::string_view text, JsonLimits limits) : text_(text), limits_(limits) {
    if (limits_.max_bytes && text_.size() > limits_.max_bytes) fail("input limit");
  }
  Json run() { auto value = parse(); whitespace(); if (at_ != text_.size()) fail("trailing input"); return value; }
 private:
  std::string_view text_;
  JsonLimits limits_;
  std::size_t at_ = 0, nodes_ = 0;
  [[noreturn]] void fail(const char* reason) const {
    throw std::runtime_error(std::string("Invalid JSON at byte ") + std::to_string(at_) + ": " + reason);
  }
  void whitespace() { while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n')) ++at_; }
  bool take(char value) { if (at_ < text_.size() && text_[at_] == value) { ++at_; return true; } return false; }
  void expect(char value) { if (!take(value)) fail("unexpected character"); }
  void literal(std::string_view value) { if (text_.substr(at_, value.size()) != value) fail("invalid literal"); at_ += value.size(); }
  unsigned hex4() {
    if (text_.size() - at_ < 4) fail("truncated unicode escape");
    unsigned result = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[at_++]; unsigned digit;
      if (c >= '0' && c <= '9') digit = c - '0';
      else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
      else fail("invalid unicode escape");
      result = result * 16 + digit;
    }
    return result;
  }
  std::string string() {
    expect('"'); std::string out;
    while (at_ < text_.size()) {
      const auto start = at_;
      while (at_ < text_.size()) {
        const auto c = static_cast<unsigned char>(text_[at_]);
        if (c < 0x20 || c >= 0x80 || c == '"' || c == '\\') break;
        ++at_;
      }
      if (at_ != start) {
        const auto length = at_ - start;
        if (limits_.max_string_bytes && length > limits_.max_string_bytes - out.size()) fail("string limit");
        out.append(text_.substr(start, length));
      }
      if (at_ == text_.size()) break;
      const auto c = static_cast<unsigned char>(text_[at_++]);
      if (c == '"') return out;
      if (c < 0x20) fail("unescaped control character");
      if (c == '\\') {
        if (at_ == text_.size()) fail("truncated escape");
        switch (text_[at_++]) {
          case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
          case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
          case 'r': out += '\r'; break; case 't': out += '\t'; break;
          case 'u': {
            unsigned code = hex4();
            if (code >= 0xd800 && code <= 0xdbff) {
              if (!take('\\') || !take('u')) fail("unpaired high surrogate");
              const unsigned low = hex4();
              if (low < 0xdc00 || low > 0xdfff) fail("unpaired high surrogate");
              code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
            } else if (code >= 0xdc00 && code <= 0xdfff) fail("unpaired low surrogate");
            if (code < 0x80) out += static_cast<char>(code);
            else if (code < 0x800) { out += static_cast<char>(0xc0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 63)); }
            else if (code < 0x10000) { out += static_cast<char>(0xe0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 63)); out += static_cast<char>(0x80 | (code & 63)); }
            else { out += static_cast<char>(0xf0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 63)); out += static_cast<char>(0x80 | ((code >> 6) & 63)); out += static_cast<char>(0x80 | (code & 63)); }
            break;
          }
          default: fail("invalid escape");
        }
      } else if (c >= 0x80) {
        const auto start = at_ - 1, width = utf8_width(text_, start);
        out.append(text_.substr(start, width)); at_ = start + width;
      }
      if (limits_.max_string_bytes && out.size() > limits_.max_string_bytes) fail("string limit");
    }
    fail("unterminated string");
  }
  Json number() {
    const auto start = at_; take('-');
    const auto digit = [this] { return at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9'; };
    if (take('0')) { if (digit()) fail("leading zero"); }
    else { if (!digit()) fail("expected number"); while (digit()) ++at_; }
    bool floating = false;
    if (take('.')) { floating = true; if (!digit()) fail("missing fraction"); while (digit()) ++at_; }
    if (take('e') || take('E')) { floating = true; if (!take('+')) take('-'); if (!digit()) fail("missing exponent"); while (digit()) ++at_; }
    const auto token = text_.substr(start, at_ - start);
    if (!floating) {
      std::int64_t value = 0;
      const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
      if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) return value;
      if (result.ec != std::errc::result_out_of_range) fail("invalid integer");
      // JSON permits larger integer literals. CEF/JavaScript may serialize
      // 1e20 without an exponent; retain its finite numeric value as a double.
    }
    double value = 0;
#if defined(__APPLE__)
    // libc++ parses doubles with from_chars only from macOS 26; the client
    // supports macOS 14. The token is already valid JSON number syntax, so
    // strtod in the C locale reads exactly it.
    static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
    const std::string copy(token);
    char* end = nullptr;
    errno = 0;
    value = strtod_l(copy.c_str(), &end, c_locale);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value) || (errno == ERANGE && value == 0.0))
      fail("floating point range");
#else
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || !std::isfinite(value)) fail("floating point range");
#endif
    return value;
  }
  // Containers under construction live on an explicit stack: nesting depth is bounded by memory, not the thread stack.
  struct Frame { Json container; std::string key; };
  Json parse() {
    std::vector<Frame> stack; Json value;
    for (;;) {
      ++nodes_;
      if ((limits_.max_depth && stack.size() > limits_.max_depth) || (limits_.max_nodes && nodes_ > limits_.max_nodes)) fail("nesting or node limit");
      whitespace(); if (at_ == text_.size()) fail("missing value");
      switch (text_[at_]) {
        case 'n': literal("null"); value = nullptr; break;
        case 't': literal("true"); value = true; break;
        case 'f': literal("false"); value = false; break;
        case '"': value = string(); break;
        case '[': {
          ++at_; whitespace();
          if (take(']')) { value = Json::array{}; break; }
          stack.push_back({Json::array{}, {}}); continue;
        }
        case '{': {
          ++at_; whitespace();
          if (take('}')) { value = Json::object{}; break; }
          auto key = string(); whitespace(); expect(':'); stack.push_back({Json::object{}, std::move(key)}); continue;
        }
        default: if (text_[at_] == '-' || (text_[at_] >= '0' && text_[at_] <= '9')) { value = number(); break; } fail("unexpected value");
      }
      // A value is complete: attach it to its parent, closing every container that ends here.
      for (;;) {
        if (stack.empty()) return value;
        auto& top = stack.back();
        if (top.container.is_array()) {
          top.container.as_array().push_back(std::move(value)); whitespace();
          if (take(']')) { value = std::move(top.container); stack.pop_back(); continue; }
          expect(','); break;
        }
        if (!top.container.as_object().emplace(std::move(top.key), std::move(value)).second) fail("duplicate key");
        whitespace();
        if (take('}')) { value = std::move(top.container); stack.pop_back(); continue; }
        expect(','); whitespace(); top.key = string(); whitespace(); expect(':'); break;
      }
    }
  }
};
class Dumper {
 public:
  explicit Dumper(JsonLimits limits) : limits_(limits) {}
  std::string run(const Json& value) { dump(value); return std::move(out_); }
 private:
  JsonLimits limits_;
  std::size_t nodes_ = 0;
  std::string out_;
  void append(std::string_view value) {
    if (limits_.max_bytes && value.size() > limits_.max_bytes - out_.size()) throw std::runtime_error("JSON output limit exceeded");
    out_.append(value);
  }
  void string(std::string_view value) {
    if (limits_.max_string_bytes && value.size() > limits_.max_string_bytes) throw std::runtime_error("JSON string limit exceeded");
    append("\"");
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < value.size();) {
      const auto start = i;
      while (i < value.size()) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c < 0x20 || c >= 0x80 || c == '"' || c == '\\') break;
        ++i;
      }
      if (i != start) append(value.substr(start, i - start));
      if (i == value.size()) break;
      const auto c = static_cast<unsigned char>(value[i]);
      if (c >= 0x80) { const auto width = utf8_width(value, i); append(value.substr(i, width)); i += width; continue; }
      ++i;
      if (c == '"') append("\\\""); else if (c == '\\') append("\\\\");
      else if (c < 0x20) { const char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]}; append(std::string_view(escaped, 6)); }
    }
    append("\"");
  }
  void scalar(const Json& value) {
    if (value.is_null()) append("null");
    else if (value.is_bool()) append(value.as_bool() ? "true" : "false");
    else if (value.is_number()) {
      char buffer[64];
      const auto converted = value.is_integer() ? std::to_chars(buffer, buffer + sizeof(buffer), value.as_int()) : std::to_chars(buffer, buffer + sizeof(buffer), value.as_double(), std::chars_format::general);
      if (converted.ec != std::errc{}) throw std::runtime_error("JSON number formatting failed");
      const std::string_view number(buffer, static_cast<std::size_t>(converted.ptr - buffer)); append(number);
      if (!value.is_integer() && number.find_first_of(".eE") == std::string_view::npos) append(".0");
    } else string(value.as_string());
  }
  // Open containers live on an explicit stack: nesting depth is bounded by memory, not the thread stack.
  struct Frame { const Json* value; std::size_t index; Json::object::const_iterator it; };
  void dump(const Json& root) {
    std::vector<Frame> stack; const Json* next = &root;
    for (;;) {
      if (next) {
        ++nodes_;
        if ((limits_.max_depth && stack.size() > limits_.max_depth) || (limits_.max_nodes && nodes_ > limits_.max_nodes)) throw std::runtime_error("JSON nesting or node limit exceeded");
        const Json& value = *next; next = nullptr;
        if (value.is_array()) { append("["); stack.push_back({&value, 0, {}}); }
        else if (value.is_object()) { append("{"); stack.push_back({&value, 0, value.as_object().begin()}); }
        else scalar(value);
      }
      if (stack.empty()) return;
      auto& top = stack.back();
      if (top.value->is_array()) {
        const auto& items = top.value->as_array();
        if (top.index == items.size()) { append("]"); stack.pop_back(); continue; }
        if (top.index++) append(",");
        next = &items[top.index - 1];
      } else {
        if (top.it == top.value->as_object().end()) { append("}"); stack.pop_back(); continue; }
        if (top.index++) append(",");
        string(top.it->first); append(":"); next = &top.it->second; ++top.it;
      }
    }
  }
};
} // namespace
Json::Json(double value) : value_(value) { if (!std::isfinite(value)) throw std::invalid_argument("JSON requires a finite number"); }
Json::Json(const char* value) { if (!value) throw std::invalid_argument("Null JSON string pointer"); value_ = std::string(value); }
Json::Json(std::initializer_list<object::value_type> values) : value_(object{}) {
  for (const auto& value : values) if (!as_object().insert(value).second) throw std::invalid_argument("Duplicate JSON object initializer key");
}
// Copy builds the destination breadth-first from an explicit work list; container slots are sized before their
// children are filled, so the destination pointers stay valid.
Json::Json(const Json& other) {
  std::vector<std::pair<const Json*, Json*>> work{{&other, this}};
  while (!work.empty()) {
    const auto [source, target] = work.back(); work.pop_back();
    if (const auto* items = std::get_if<array>(&source->value_)) {
      auto& copy = target->value_.emplace<array>(items->size());
      for (std::size_t i = 0; i < items->size(); ++i) work.emplace_back(&(*items)[i], &copy[i]);
    } else if (const auto* fields = std::get_if<object>(&source->value_)) {
      auto& copy = target->value_.emplace<object>();
      for (const auto& [key, item] : *fields) work.emplace_back(&item, &copy.emplace_hint(copy.end(), key, Json())->second);
    } else target->value_ = source->value_;
  }
}
Json& Json::operator=(const Json& other) { if (this != &other) { Json copy(other); *this = std::move(copy); } return *this; }
// Destruction detaches children onto a flat list before each container dies, so no destructor recurses.
Json::~Json() {
  const auto container = [](const Json& value) {
    if (const auto* items = std::get_if<array>(&value.value_)) return !items->empty();
    if (const auto* fields = std::get_if<object>(&value.value_)) return !fields->empty();
    return false;
  };
  if (!container(*this)) return;
  std::vector<Json> pending;
  const auto detach = [&](Json& value) {
    if (auto* items = std::get_if<array>(&value.value_)) {
      for (auto& item : *items) if (container(item)) pending.push_back(std::move(item));
      items->clear();
    } else if (auto* fields = std::get_if<object>(&value.value_)) {
      for (auto& [key, item] : *fields) if (container(item)) pending.push_back(std::move(item));
      fields->clear();
    }
  };
  detach(*this);
  while (!pending.empty()) { Json value = std::move(pending.back()); pending.pop_back(); detach(value); }
}
bool Json::operator==(const Json& other) const {
  std::vector<std::pair<const Json*, const Json*>> work{{this, &other}};
  while (!work.empty()) {
    const auto [left, right] = work.back(); work.pop_back();
    if (left->value_.index() != right->value_.index()) return false;
    if (const auto* items = std::get_if<array>(&left->value_)) {
      const auto& others = std::get<array>(right->value_);
      if (items->size() != others.size()) return false;
      for (std::size_t i = 0; i < items->size(); ++i) work.emplace_back(&(*items)[i], &others[i]);
    } else if (const auto* fields = std::get_if<object>(&left->value_)) {
      const auto& others = std::get<object>(right->value_);
      if (fields->size() != others.size()) return false;
      for (auto a = fields->begin(), b = others.begin(); a != fields->end(); ++a, ++b) {
        if (a->first != b->first) return false;
        work.emplace_back(&a->second, &b->second);
      }
    } else if (left->value_ != right->value_) return false;
  }
  return true;
}
Json Json::parse(std::string_view input, JsonLimits limits) { return Parser(input, limits).run(); }
std::string Json::dump(JsonLimits limits) const { return Dumper(limits).run(*this); }
bool Json::as_bool() const { if (!is_bool()) wrong_type(); return std::get<bool>(value_); }
std::int64_t Json::as_int() const { if (!is_integer()) wrong_type(); return std::get<std::int64_t>(value_); }
double Json::as_double() const { if (is_integer()) return static_cast<double>(as_int()); if (!is_number()) wrong_type(); return std::get<double>(value_); }
const std::string& Json::as_string() const { if (!is_string()) wrong_type(); return std::get<std::string>(value_); }
std::string& Json::as_string() { if (!is_string()) wrong_type(); return std::get<std::string>(value_); }
const Json::array& Json::as_array() const { if (!is_array()) wrong_type(); return std::get<array>(value_); }
Json::array& Json::as_array() { if (!is_array()) wrong_type(); return std::get<array>(value_); }
const Json::object& Json::as_object() const { if (!is_object()) wrong_type(); return std::get<object>(value_); }
Json::object& Json::as_object() { if (!is_object()) wrong_type(); return std::get<object>(value_); }
bool Json::contains(std::string_view key) const noexcept { return is_object() && std::get<object>(value_).contains(key); }
const Json& Json::at(std::string_view key) const { const auto& values = as_object(); const auto found = values.find(key); if (found == values.end()) throw std::out_of_range("Missing JSON key"); return found->second; }
Json& Json::at(std::string_view key) { auto& values = as_object(); const auto found = values.find(key); if (found == values.end()) throw std::out_of_range("Missing JSON key"); return found->second; }
const Json& Json::at(std::size_t index) const { return as_array().at(index); }
Json& Json::at(std::size_t index) { return as_array().at(index); }
Json& Json::operator[](std::string_view key) { if (is_null()) value_ = object{}; return as_object()[std::string(key)]; }
} // namespace suitecut
