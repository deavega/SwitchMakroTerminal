// json.hpp - a small, self-contained recursive-descent JSON parser.
// Written for SwitchMakroTerminal so the project has no external JSON dependency.
// Supports: object, array, string (with \uXXXX + surrogate pairs), number, bool, null.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstdlib>

namespace mj {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool   b = false;
    double n = 0.0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    Json() = default;

    bool isNull()   const { return type == Type::Null;   }
    bool isBool()   const { return type == Type::Bool;   }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray()  const { return type == Type::Array;  }
    bool isObject() const { return type == Type::Object; }

    size_t size() const {
        if (type == Type::Array)  return a.size();
        if (type == Type::Object) return o.size();
        return 0;
    }

    bool contains(const std::string& k) const {
        return type == Type::Object && o.find(k) != o.end();
    }

    const Json& operator[](const std::string& k) const {
        static const Json kNull;
        if (type != Type::Object) return kNull;
        auto it = o.find(k);
        return it == o.end() ? kNull : it->second;
    }

    const Json& operator[](size_t i) const {
        static const Json kNull;
        if (type != Type::Array || i >= a.size()) return kNull;
        return a[i];
    }

    double      asDouble(double def = 0.0) const { return type == Type::Number ? n : def; }
    std::string asString(const std::string& def = "") const { return type == Type::String ? s : def; }
    bool        asBool(bool def = false) const { return type == Type::Bool ? b : def; }

    static Json parse(const std::string& text) {
        size_t i = 0;
        return parseValue(text, i);
    }

private:
    static void skipWs(const std::string& t, size_t& i) {
        while (i < t.size()) {
            char c = t[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++;
            else break;
        }
    }

    static void encodeUtf8(uint32_t cp, std::string& out) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    static uint32_t parseHex4(const std::string& t, size_t& i) {
        uint32_t v = 0;
        for (int k = 0; k < 4 && i < t.size(); k++) {
            char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
        }
        return v;
    }

    static std::string parseString(const std::string& t, size_t& i) {
        std::string out;
        i++; // opening quote
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') break;
            if (c == '\\' && i < t.size()) {
                char e = t[i++];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parseHex4(t, i);
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < t.size()
                            && t[i] == '\\' && t[i + 1] == 'u') {
                            i += 2;
                            uint32_t lo = parseHex4(t, i);
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        encodeUtf8(cp, out);
                        break;
                    }
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    static Json parseNumber(const std::string& t, size_t& i) {
        size_t start = i;
        while (i < t.size()) {
            char c = t[i];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' ||
                c == '.' || c == 'e' || c == 'E') i++;
            else break;
        }
        Json j;
        j.type = Type::Number;
        j.n = std::strtod(t.substr(start, i - start).c_str(), nullptr);
        return j;
    }

    static Json parseArray(const std::string& t, size_t& i) {
        Json j; j.type = Type::Array;
        i++; // [
        skipWs(t, i);
        if (i < t.size() && t[i] == ']') { i++; return j; }
        while (i < t.size()) {
            j.a.push_back(parseValue(t, i));
            skipWs(t, i);
            if (i < t.size() && t[i] == ',') { i++; skipWs(t, i); continue; }
            if (i < t.size() && t[i] == ']') { i++; break; }
            break;
        }
        return j;
    }

    static Json parseObject(const std::string& t, size_t& i) {
        Json j; j.type = Type::Object;
        i++; // {
        skipWs(t, i);
        if (i < t.size() && t[i] == '}') { i++; return j; }
        while (i < t.size()) {
            skipWs(t, i);
            if (i >= t.size() || t[i] != '"') break;
            std::string key = parseString(t, i);
            skipWs(t, i);
            if (i < t.size() && t[i] == ':') i++;
            Json val = parseValue(t, i);
            j.o[key] = std::move(val);
            skipWs(t, i);
            if (i < t.size() && t[i] == ',') { i++; continue; }
            if (i < t.size() && t[i] == '}') { i++; break; }
            break;
        }
        return j;
    }

    static Json parseValue(const std::string& t, size_t& i) {
        skipWs(t, i);
        if (i >= t.size()) return Json();
        char c = t[i];
        if (c == '{') return parseObject(t, i);
        if (c == '[') return parseArray(t, i);
        if (c == '"') { Json j; j.type = Type::String; j.s = parseString(t, i); return j; }
        if (c == 't') { i += 4; Json j; j.type = Type::Bool; j.b = true;  return j; }
        if (c == 'f') { i += 5; Json j; j.type = Type::Bool; j.b = false; return j; }
        if (c == 'n') { i += 4; return Json(); }
        return parseNumber(t, i);
    }
};

} // namespace mj
