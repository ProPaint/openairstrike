// Expected counts and known exceptions of the game under test: testdata/golden/<key>/expected.json
// (documented in testdata/golden/README.md; the Python tests read the same file through
// tools/ref/gamesel.py). A small JSON reader and dotted-path accessors, e.g.
//   testdata::expectedInt("definitions.weapons"), testdata::expectedStrings("models.empty").
// A missing file or key is a broken golden set, not a skipped test: it stops the run loudly.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "test_data.h"

namespace testdata {

struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json* find(const std::string& key) const {
        auto it = o.find(key);
        return it == o.end() ? nullptr : &it->second;
    }
    long long asInt() const { return static_cast<long long>(n); }
};

namespace detail {

struct JsonReader {
    const std::string& t;
    size_t i = 0;
    explicit JsonReader(const std::string& text) : t(text) {}

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\t' || t[i] == '\r')) ++i;
    }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (t.compare(i, n, w) != 0) return false;
        i += n;
        return true;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool str(std::string& out) {
        if (i >= t.size() || t[i] != '"') return false;
        ++i;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > t.size()) return false;
                unsigned cp = static_cast<unsigned>(std::strtoul(t.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
                utf8(out, cp);
                break;
            }
            default: out += e; break; // \\ \" \/
            }
        }
        if (i >= t.size()) return false;
        ++i;
        return true;
    }
    bool value(Json& v, int depth = 0) {
        if (depth > 32) return false;
        ws();
        if (i >= t.size()) return false;
        char c = t[i];
        if (c == '{') {
            ++i;
            v.kind = Json::Obj;
            ws();
            if (i < t.size() && t[i] == '}') return ++i, true;
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= t.size() || t[i] != ':') return false;
                ++i;
                if (!value(v.o[k], depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') return ++i, true;
                return false;
            }
        }
        if (c == '[') {
            ++i;
            v.kind = Json::Arr;
            ws();
            if (i < t.size() && t[i] == ']') return ++i, true;
            for (;;) {
                v.a.emplace_back();
                if (!value(v.a.back(), depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') return ++i, true;
                return false;
            }
        }
        if (c == '"') {
            v.kind = Json::Str;
            return str(v.s);
        }
        if (lit("true")) { v.kind = Json::Bool; v.b = true; return true; }
        if (lit("false")) { v.kind = Json::Bool; v.b = false; return true; }
        if (lit("null")) { v.kind = Json::Null; return true; }
        char* end = nullptr;
        v.n = std::strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return false;
        i = static_cast<size_t>(end - t.c_str());
        v.kind = Json::Num;
        return true;
    }
};

[[noreturn]] inline void expectedFail(const std::string& what) {
    std::fprintf(stderr, "expected.json (%s): %s\n", goldenDir().c_str(), what.c_str());
    std::exit(2);
}

} // namespace detail

inline bool parseJson(const std::string& text, Json& out) {
    detail::JsonReader r(text);
    if (!r.value(out)) return false;
    r.ws();
    return r.i == text.size();
}

// The parsed expected.json of the game under test.
inline const Json& expected() {
    static const Json j = [] {
        std::ifstream f(goldenDir() + "/expected.json", std::ios::binary);
        if (!f) detail::expectedFail("missing file");
        std::ostringstream ss;
        ss << f.rdbuf();
        Json v;
        if (!parseJson(ss.str(), v) || v.kind != Json::Obj) detail::expectedFail("not valid JSON");
        return v;
    }();
    return j;
}

// Value at a dotted path ("definitions.weapons"); nullptr if a step is missing.
inline const Json* expectedAt(const std::string& path) {
    const Json* cur = &expected();
    size_t i = 0;
    while (cur && i <= path.size()) {
        size_t dot = path.find('.', i);
        std::string key = path.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
        cur = cur->find(key);
        if (dot == std::string::npos) break;
        i = dot + 1;
    }
    return cur;
}

inline const Json& expectedNode(const std::string& path) {
    const Json* v = expectedAt(path);
    if (!v) detail::expectedFail("missing key '" + path + "'");
    return *v;
}

inline long long expectedInt(const std::string& path) {
    const Json& v = expectedNode(path);
    if (v.kind != Json::Num) detail::expectedFail("'" + path + "' is not a number");
    return v.asInt();
}

inline double expectedNumber(const std::string& path) {
    const Json& v = expectedNode(path);
    if (v.kind != Json::Num) detail::expectedFail("'" + path + "' is not a number");
    return v.n;
}

inline std::vector<std::string> expectedStrings(const std::string& path) {
    const Json& v = expectedNode(path);
    if (v.kind != Json::Arr) detail::expectedFail("'" + path + "' is not an array");
    std::vector<std::string> out;
    for (const Json& e : v.a) out.push_back(e.s);
    return out;
}

inline bool playable() {
    const Json* v = expectedAt("playable");
    return v && v->kind == Json::Bool && v->b;
}

} // namespace testdata
