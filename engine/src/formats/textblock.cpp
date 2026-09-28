// Generic parser for the brace-block text files (.obj, .wpn, .ps, levels.txt).
// See docs/spec/text-blocks.md. Mirrors tools/ref/textblock.py exactly so the
// two report identical block/statement/token counts on every shipped file.
#include "as3d/textblock.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace as3d {

namespace {

bool isNumberText(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '-') i++;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
    if (i < s.size() && s[i] == '.') {
        i++;
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
    }
    return i == s.size();
}

bool isSpaceByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

enum class TokKind { Tok, Open, Close };

struct RawToken {
    TokKind kind;
    std::string text;
    bool quoted = false;
};

// Tokenizes one physical line (no trailing '\n'/'\r'). `//` starts a line
// comment anywhere outside a quoted string. Sets *unterminated if a '"' is
// never closed on this line.
std::vector<RawToken> tokenizeLine(const std::string& line, bool* unterminated) {
    std::vector<RawToken> out;
    *unterminated = false;
    size_t i = 0, n = line.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if (isSpaceByte(c)) {
            i++;
            continue;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '/') break;
        if (c == '{') {
            out.push_back({TokKind::Open, "{", false});
            i++;
            continue;
        }
        if (c == '}') {
            out.push_back({TokKind::Close, "}", false});
            i++;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            while (j < n && line[j] != '"') j++;
            if (j < n) {
                out.push_back({TokKind::Tok, line.substr(i + 1, j - i - 1), true});
                i = j + 1;
            } else {
                out.push_back({TokKind::Tok, line.substr(i + 1, n - i - 1), true});
                *unterminated = true;
                i = n;
            }
            continue;
        }
        size_t j = i;
        while (j < n) {
            unsigned char bj = static_cast<unsigned char>(line[j]);
            if (isSpaceByte(bj) || bj == '{' || bj == '}' || bj == '"') break;
            if (bj == '/' && j + 1 < n && line[j + 1] == '/') break;
            j++;
        }
        out.push_back({TokKind::Tok, line.substr(i, j - i), false});
        i = j;
    }
    return out;
}

} // namespace

bool TextToken::isNumber() const { return !quoted && isNumberText(text); }

float TextToken::asFloat() const {
    if (!isNumber()) return 0.0f;
    return std::strtof(text.c_str(), nullptr);
}

int TextToken::asInt() const {
    if (!isNumber()) return 0;
    return static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
}

const TextStatement* TextBlock::find(const char* key) const {
    for (const auto& s : statements) {
        if (s.key.size() != std::strlen(key)) continue;
        bool eq = true;
        for (size_t i = 0; i < s.key.size(); i++) {
            if (std::tolower(static_cast<unsigned char>(s.key[i])) !=
                std::tolower(static_cast<unsigned char>(key[i]))) {
                eq = false;
                break;
            }
        }
        if (eq) return &s;
    }
    return nullptr;
}

TextFile parseTextBlocks(const u8* data, size_t size) {
    TextFile tf;

    int depth = 0;       // 0 = outside any block, 1 = inside the current block
    int extraDepth = 0;  // unsupported nested '{' inside the current block
    bool havePendingName = false;
    std::string pendingName;
    int pendingNameLine = 0;
    bool haveCurrent = false;
    TextBlock current;

    // Split into physical lines on '\n'; a trailing '\r' is stripped per line
    // so both CRLF and bare LF files parse identically. Embedded NUL bytes
    // are ordinary token bytes, never a terminator.
    std::string all(reinterpret_cast<const char*>(data), size);
    size_t pos = 0;
    int lineNo = 0;
    bool more = true;
    while (more) {
        size_t nl = all.find('\n', pos);
        std::string raw;
        if (nl == std::string::npos) {
            raw = all.substr(pos);
            more = false;
        } else {
            raw = all.substr(pos, nl - pos);
            pos = nl + 1;
        }
        lineNo++;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        bool unterminated = false;
        std::vector<RawToken> tokens = tokenizeLine(raw, &unterminated);
        if (unterminated) {
            tf.errors.push_back("line " + std::to_string(lineNo) + ": unterminated string");
        }
        if (!tokens.empty()) {
            size_t i = 0, n = tokens.size();
            while (i < n) {
                const RawToken& t = tokens[i];
                if (depth == 0) {
                    if (havePendingName) {
                        if (t.kind == TokKind::Open) {
                            current = TextBlock{};
                            current.name = pendingName;
                            current.line = pendingNameLine;
                            haveCurrent = true;
                            depth = 1;
                            havePendingName = false;
                            i++;
                            continue;
                        }
                        tf.errors.push_back("line " + std::to_string(pendingNameLine) +
                                             ": expected '{' after block name '" + pendingName +
                                             "'");
                        havePendingName = false;
                        continue; // re-examine the same token now that the name is cleared
                    }
                    if (t.kind == TokKind::Open) {
                        current = TextBlock{};
                        current.name.clear();
                        current.line = lineNo;
                        haveCurrent = true;
                        depth = 1;
                        i++;
                        continue;
                    }
                    if (t.kind == TokKind::Close) {
                        tf.errors.push_back("line " + std::to_string(lineNo) +
                                             ": unexpected '}' with no open block");
                        i++;
                        continue;
                    }
                    // Bare or quoted token outside any block: a candidate block name.
                    havePendingName = true;
                    pendingName = t.text;
                    pendingNameLine = lineNo;
                    i++;
                    continue;
                } else {
                    if (t.kind == TokKind::Open) {
                        tf.errors.push_back("line " + std::to_string(lineNo) +
                                             ": nested '{' inside block '" + current.name +
                                             "', flattening into the parent block");
                        extraDepth++;
                        i++;
                        continue;
                    }
                    if (t.kind == TokKind::Close) {
                        if (extraDepth > 0) {
                            extraDepth--;
                            i++;
                            continue;
                        }
                        tf.blocks.push_back(current);
                        haveCurrent = false;
                        depth = 0;
                        i++;
                        continue;
                    }
                    // Statement: key followed by zero or more argument tokens, up to
                    // the next brace (real statements never span more than one line).
                    TextStatement stmt;
                    stmt.key = t.text;
                    stmt.line = lineNo;
                    i++;
                    while (i < n && tokens[i].kind == TokKind::Tok) {
                        TextToken arg;
                        arg.text = tokens[i].text;
                        arg.quoted = tokens[i].quoted;
                        stmt.args.push_back(arg);
                        i++;
                    }
                    current.statements.push_back(std::move(stmt));
                }
            }
        }
    }

    if (havePendingName) {
        tf.errors.push_back("line " + std::to_string(pendingNameLine) + ": block name '" +
                             pendingName + "' at end of file with no '{'");
    }
    if (depth == 1 && haveCurrent) {
        tf.errors.push_back("line " + std::to_string(current.line) + ": unterminated block '" +
                             current.name + "' (missing '}')");
        tf.blocks.push_back(current);
    }

    return tf;
}

} // namespace as3d
