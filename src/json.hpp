#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// Minimal JSON value type for LSP protocol messages.
struct JsonValue {
    enum Type { Null, Bool, Int, Float, String, Array, Object };

    struct ObjectType : std::map<std::string, JsonValue> {};
    struct ArrayType  : std::vector<JsonValue> {};

    using Variant = std::variant<
        std::nullptr_t, bool, int64_t, double,
        std::string, ArrayType, ObjectType
    >;

    Variant var = nullptr;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : var(nullptr) {}
    JsonValue(bool v)         : var(v) {}
    JsonValue(int64_t v)      : var(v) {}
    JsonValue(int v)          : var(static_cast<int64_t>(v)) {}
    JsonValue(double v)       : var(v) {}
    JsonValue(const char* v)  : var(std::string(v)) {}
    JsonValue(const std::string& v) : var(v) {}
    JsonValue(ArrayType&& v)  : var(std::move(v)) {}
    JsonValue(ObjectType&& v) : var(std::move(v)) {}

    Type type() const {
        return static_cast<Type>(var.index());
    }

    bool is_null()   const { return type() == Null; }
    bool is_bool()   const { return type() == Bool; }
    bool is_int()    const { return type() == Int; }
    bool is_float()  const { return type() == Float; }
    bool is_string() const { return type() == String; }
    bool is_array()  const { return type() == Array; }
    bool is_object() const { return type() == Object; }

    // Accessors with checks
    bool as_bool()               const { return std::get<bool>(var); }
    int64_t as_int()             const { return std::get<int64_t>(var); }
    double as_float()            const { return std::get<double>(var); }
    const std::string& as_string() const { return std::get<std::string>(var); }

    const ArrayType&  as_array()  const { return std::get<ArrayType>(var); }
    ArrayType&        as_array()        { return std::get<ArrayType>(var); }
    const ObjectType& as_object() const { return std::get<ObjectType>(var); }
    ObjectType&       as_object()       { return std::get<ObjectType>(var); }

    // Convenience: get object member
    const JsonValue* get(const std::string& key) const {
        if (!is_object()) return nullptr;
        auto it = as_object().find(key);
        return it != as_object().end() ? &it->second : nullptr;
    }
};

namespace Json {

// Parse a JSON string. Throws std::runtime_error on malformed input.
JsonValue parse(const std::string& input);

// Serialize to compact JSON string.
std::string serialize(const JsonValue& val);

// Serialize to pretty-printed JSON string (2-space indent).
std::string serialize_pretty(const JsonValue& val);

// -----------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------

namespace detail {

class Parser {
    const std::string& s;
    size_t pos = 0;

    void skip_ws() {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            pos++;
    }

    char peek() {
        skip_ws();
        if (pos >= s.size()) throw std::runtime_error("Unexpected end of JSON");
        return s[pos];
    }

    char consume() {
        skip_ws();
        if (pos >= s.size()) throw std::runtime_error("Unexpected end of JSON");
        return s[pos++];
    }

    std::string parse_string() {
        if (consume() != '"') throw std::runtime_error("Expected '\"'");
        std::string result;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\') {
                pos++;
                if (pos >= s.size()) throw std::runtime_error("Unterminated string escape");
                switch (s[pos]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        if (pos + 4 >= s.size()) throw std::runtime_error("Invalid \\u escape");
                        std::string hex = s.substr(pos+1, 4);
                        pos += 4;
                        result += static_cast<char>(std::stoul(hex, nullptr, 16));
                        break;
                    }
                    default: result += s[pos]; break;
                }
                pos++;
            } else {
                result += s[pos++];
            }
        }
        if (pos >= s.size() || s[pos] != '"') throw std::runtime_error("Unterminated string");
        pos++;
        return result;
    }

    JsonValue parse_number() {
        skip_ws();
        size_t start = pos;
        if (pos < s.size() && s[pos] == '-') pos++;
        while (pos < s.size() && std::isdigit(s[pos])) pos++;
        bool is_float = false;
        if (pos < s.size() && s[pos] == '.') {
            is_float = true;
            pos++;
            while (pos < s.size() && std::isdigit(s[pos])) pos++;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            is_float = true;
            pos++;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
            while (pos < s.size() && std::isdigit(s[pos])) pos++;
        }
        std::string num_str = s.substr(start, pos - start);
        if (is_float) {
            return std::stod(num_str);
        } else {
            return static_cast<int64_t>(std::stoll(num_str));
        }
    }

    JsonValue parse_value() {
        char c = peek();
        if (c == '"') return parse_string();
        if (c == '-' || std::isdigit(c)) return parse_number();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't') {
            if (s.substr(pos, 4) == "true") { pos += 4; return true; }
            throw std::runtime_error("Expected 'true'");
        }
        if (c == 'f') {
            if (s.substr(pos, 5) == "false") { pos += 5; return false; }
            throw std::runtime_error("Expected 'false'");
        }
        if (c == 'n') {
            if (s.substr(pos, 4) == "null") { pos += 4; return nullptr; }
            throw std::runtime_error("Expected 'null'");
        }
        throw std::runtime_error("Unexpected character in JSON value");
    }

    JsonValue parse_object() {
        if (consume() != '{') throw std::runtime_error("Expected '{'");
        JsonValue::ObjectType obj;
        if (peek() == '}') { pos++; return std::move(obj); }
        while (true) {
            if (peek() != '"') throw std::runtime_error("Expected string key in object");
            auto key = parse_string();
            if (consume() != ':') throw std::runtime_error("Expected ':' in object");
            obj[key] = parse_value();
            skip_ws();
            if (pos >= s.size()) throw std::runtime_error("Unexpected end of object");
            if (s[pos] == '}') { pos++; return std::move(obj); }
            if (s[pos] != ',') throw std::runtime_error("Expected ',' or '}' in object");
            pos++; // skip ','
        }
    }

    JsonValue parse_array() {
        if (consume() != '[') throw std::runtime_error("Expected '['");
        JsonValue::ArrayType arr;
        if (peek() == ']') { pos++; return std::move(arr); }
        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            if (pos >= s.size()) throw std::runtime_error("Unexpected end of array");
            if (s[pos] == ']') { pos++; return std::move(arr); }
            if (s[pos] != ',') throw std::runtime_error("Expected ',' or ']' in array");
            pos++; // skip ','
        }
    }

public:
    explicit Parser(const std::string& str) : s(str) {}

    JsonValue parse() {
        auto val = parse_value();
        skip_ws();
        if (pos != s.size()) throw std::runtime_error("Trailing characters in JSON");
        return val;
    }
};

// Serializer
struct Serializer {
    std::string out;

    void write(const JsonValue& val) {
        switch (val.type()) {
            case JsonValue::Null:   out += "null"; break;
            case JsonValue::Bool:   out += val.as_bool() ? "true" : "false"; break;
            case JsonValue::Int:    out += std::to_string(val.as_int()); break;
            case JsonValue::Float:  out += std::to_string(val.as_float()); break;
            case JsonValue::String: write_string(val.as_string()); break;
            case JsonValue::Array:  write_array(val.as_array()); break;
            case JsonValue::Object: write_object(val.as_object()); break;
        }
    }

    void write_pretty(const JsonValue& val, int indent = 0) {
        std::string ind(indent, ' ');
        std::string ind2(indent + 2, ' ');
        switch (val.type()) {
            case JsonValue::Null:   out += "null"; break;
            case JsonValue::Bool:   out += val.as_bool() ? "true" : "false"; break;
            case JsonValue::Int:    out += std::to_string(val.as_int()); break;
            case JsonValue::Float:  out += std::to_string(val.as_float()); break;
            case JsonValue::String: write_string(val.as_string()); break;
            case JsonValue::Array:
                if (val.as_array().empty()) { out += "[]"; break; }
                out += "[\n";
                for (size_t i = 0; i < val.as_array().size(); i++) {
                    if (i > 0) out += ",\n";
                    out += ind2;
                    write_pretty(val.as_array()[i], indent + 2);
                }
                out += "\n" + ind + "]";
                break;
            case JsonValue::Object:
                if (val.as_object().empty()) { out += "{}"; break; }
                out += "{\n";
                bool first = true;
                for (const auto& [k, v] : val.as_object()) {
                    if (!first) out += ",\n";
                    first = false;
                    out += ind2;
                    write_string(k);
                    out += ": ";
                    write_pretty(v, indent + 2);
                }
                out += "\n" + ind + "}";
                break;
        }
    }

private:
    void write_string(const std::string& str) {
        out += '"';
        for (char c : str) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                        out += buf;
                    } else {
                        out += c;
                    }
            }
        }
        out += '"';
    }

    void write_array(const JsonValue::ArrayType& arr) {
        out += '[';
        for (size_t i = 0; i < arr.size(); i++) {
            if (i > 0) out += ',';
            write(arr[i]);
        }
        out += ']';
    }

    void write_object(const JsonValue::ObjectType& obj) {
        out += '{';
        bool first = true;
        for (const auto& [k, v] : obj) {
            if (!first) out += ',';
            first = false;
            write_string(k);
            out += ':';
            write(v);
        }
        out += '}';
    }
};

} // namespace detail

inline JsonValue parse(const std::string& str) {
    detail::Parser p(str);
    return p.parse();
}

inline std::string serialize(const JsonValue& val) {
    detail::Serializer s;
    s.write(val);
    return s.out;
}

inline std::string serialize_pretty(const JsonValue& val) {
    detail::Serializer s;
    s.write_pretty(val);
    return s.out;
}

} // namespace Json
