#include "volcano/stats.hpp"

#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace volcano {
namespace {

struct JsonArray;
struct JsonObject;

struct JsonValue {
  enum class Kind { Null, Bool, Number, String, Object, Array };

  Kind kind = Kind::Null;
  bool bool_value = false;
  double number_value = 0.0;
  std::string string_value;
  std::shared_ptr<JsonObject> object_value;
  std::shared_ptr<JsonArray> array_value;

  const JsonValue &At(const std::string &key) const;
};

struct JsonObject {
  std::unordered_map<std::string, JsonValue> fields;
};

struct JsonArray {
  std::vector<JsonValue> values;
};

const JsonValue &JsonValue::At(const std::string &key) const {
  if (kind != Kind::Object) {
    throw std::runtime_error("expected JSON object");
  }
  const auto found = object_value->fields.find(key);
  if (found == object_value->fields.end()) {
    throw std::runtime_error("missing JSON key: " + key);
  }
  return found->second;
}

class JsonParser {
public:
  explicit JsonParser(const std::string &input) : input_(input) {
  }

  JsonValue Parse() {
    auto value = ParseValue();
    SkipWhitespace();
    if (pos_ != input_.size()) {
      throw std::runtime_error("unexpected trailing JSON input");
    }
    return value;
  }

private:
  JsonValue ParseValue() {
    SkipWhitespace();
    if (pos_ >= input_.size()) {
      throw std::runtime_error("unexpected end of JSON input");
    }
    const char c = input_[pos_];
    if (c == '{') {
      return ParseObject();
    }
    if (c == '[') {
      return ParseArray();
    }
    if (c == '"') {
      JsonValue value;
      value.kind = JsonValue::Kind::String;
      value.string_value = ParseString();
      return value;
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
      JsonValue value;
      value.kind = JsonValue::Kind::Number;
      value.number_value = ParseNumber();
      return value;
    }
    if (ConsumeLiteral("true")) {
      JsonValue value;
      value.kind = JsonValue::Kind::Bool;
      value.bool_value = true;
      return value;
    }
    if (ConsumeLiteral("false")) {
      JsonValue value;
      value.kind = JsonValue::Kind::Bool;
      value.bool_value = false;
      return value;
    }
    if (ConsumeLiteral("null")) {
      return JsonValue{};
    }
    throw std::runtime_error("invalid JSON value");
  }

  JsonValue ParseObject() {
    Expect('{');
    JsonValue value;
    value.kind = JsonValue::Kind::Object;
    value.object_value = std::make_shared<JsonObject>();
    SkipWhitespace();
    if (Consume('}')) {
      return value;
    }
    while (true) {
      const auto key = ParseString();
      SkipWhitespace();
      Expect(':');
      value.object_value->fields.emplace(key, ParseValue());
      SkipWhitespace();
      if (Consume('}')) {
        return value;
      }
      Expect(',');
    }
  }

  JsonValue ParseArray() {
    Expect('[');
    JsonValue value;
    value.kind = JsonValue::Kind::Array;
    value.array_value = std::make_shared<JsonArray>();
    SkipWhitespace();
    if (Consume(']')) {
      return value;
    }
    while (true) {
      value.array_value->values.push_back(ParseValue());
      SkipWhitespace();
      if (Consume(']')) {
        return value;
      }
      Expect(',');
    }
  }

  std::string ParseString() {
    Expect('"');
    std::string result;
    while (pos_ < input_.size()) {
      const char c = input_[pos_++];
      if (c == '"') {
        return result;
      }
      if (c == '\\') {
        if (pos_ >= input_.size()) {
          throw std::runtime_error("unterminated JSON escape");
        }
        const char escaped = input_[pos_++];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        default:
          throw std::runtime_error("unsupported JSON escape");
        }
      } else {
        result.push_back(c);
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }

  double ParseNumber() {
    const auto start = pos_;
    if (input_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) {
      ++pos_;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) {
        ++pos_;
      }
    }
    if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
        ++pos_;
      }
      while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_])) != 0) {
        ++pos_;
      }
    }
    return std::stod(input_.substr(start, pos_ - start));
  }

  void SkipWhitespace() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_])) != 0) {
      ++pos_;
    }
  }

  bool Consume(char c) {
    SkipWhitespace();
    if (pos_ < input_.size() && input_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool ConsumeLiteral(const std::string &literal) {
    if (input_.compare(pos_, literal.size(), literal) == 0) {
      pos_ += literal.size();
      return true;
    }
    return false;
  }

  void Expect(char c) {
    SkipWhitespace();
    if (pos_ >= input_.size() || input_[pos_] != c) {
      throw std::runtime_error(std::string("expected JSON character: ") + c);
    }
    ++pos_;
  }

  const std::string &input_;
  std::size_t pos_ = 0;
};

ColumnRef ParseColumnRef(const std::string &text) {
  const auto dot = text.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= text.size()) {
    throw std::runtime_error("expected qualified column reference alias.column: " + text);
  }
  return ColumnRef{text.substr(0, dot), text.substr(dot + 1)};
}

double NumberAt(const JsonValue &object, const std::string &key) {
  const auto &value = object.At(key);
  if (value.kind != JsonValue::Kind::Number) {
    throw std::runtime_error("expected numeric JSON key: " + key);
  }
  return value.number_value;
}

std::string StringAt(const JsonValue &object, const std::string &key) {
  const auto &value = object.At(key);
  if (value.kind != JsonValue::Kind::String) {
    throw std::runtime_error("expected string JSON key: " + key);
  }
  return value.string_value;
}

} // namespace

StatsCatalog StatsCatalog::FromJson(const std::string &json) {
  const auto root = JsonParser(json).Parse();
  if (root.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("stats JSON root must be an object");
  }

  StatsCatalog catalog;
  const auto &relations = root.At("relations");
  if (relations.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("stats.relations must be an object");
  }
  for (const auto &[alias, relation_value] : relations.object_value->fields) {
    if (relation_value.kind != JsonValue::Kind::Object) {
      throw std::runtime_error("relation stats must be an object for alias: " + alias);
    }
    catalog.relations_.emplace(alias, RelationStats{NumberAt(relation_value, "rows"),
                                                    NumberAt(relation_value, "scan_cost")});
  }

  const auto &selectivities = root.At("selectivities");
  if (selectivities.kind != JsonValue::Kind::Array) {
    throw std::runtime_error("stats.selectivities must be an array");
  }
  for (const auto &entry : selectivities.array_value->values) {
    if (entry.kind != JsonValue::Kind::Object) {
      throw std::runtime_error("selectivity entry must be an object");
    }
    const auto left = ParseColumnRef(StringAt(entry, "left"));
    const auto right = ParseColumnRef(StringAt(entry, "right"));
    const auto selectivity = NumberAt(entry, "selectivity");
    const auto key = PredicateKey(left, right);
    const auto found = catalog.selectivities_.find(key);
    if (found != catalog.selectivities_.end() && found->second != selectivity) {
      throw std::runtime_error("conflicting selectivity for predicate: " + left.ToString() + " = " + right.ToString());
    }
    catalog.selectivities_[key] = selectivity;
  }

  return catalog;
}

StatsCatalog StatsCatalog::FromFile(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open stats file: " + path);
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  return FromJson(buffer.str());
}

const RelationStats &StatsCatalog::LookupRelation(const std::string &alias) const {
  const auto found = relations_.find(alias);
  if (found == relations_.end()) {
    throw std::runtime_error("missing stats for relation alias: " + alias);
  }
  return found->second;
}

double StatsCatalog::LookupSelectivity(const ColumnRef &left, const ColumnRef &right) const {
  const auto found = selectivities_.find(PredicateKey(left, right));
  if (found == selectivities_.end()) {
    throw std::runtime_error("missing selectivity for predicate: " + left.ToString() + " = " + right.ToString());
  }
  return found->second;
}

std::string StatsCatalog::PredicateKey(const ColumnRef &left, const ColumnRef &right) {
  auto left_text = left.ToString();
  auto right_text = right.ToString();
  if (right_text < left_text) {
    std::swap(left_text, right_text);
  }
  return left_text + "=" + right_text;
}

} // namespace volcano
