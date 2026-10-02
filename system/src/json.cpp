#include <nexus/system/json.hpp>

#include <cstdlib>

namespace nexus::system {

namespace {

const JsonValue& nullValue() {
    static const JsonValue none;
    return none;
}

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    JsonResult run() {
        JsonResult result;
        try {
            skip();
            result.value = value(0);
            skip();
            if (pos_ != s_.size()) {
                fail("unexpected text after the value");
            }
        } catch (const std::string& message) {
            result.value = JsonValue{};
            result.error = message + " at byte " + std::to_string(pos_);
        }
        return result;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& message) { throw message; }

    void skip() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' ||
                s_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool literal(const char* word) {
        std::size_t n = 0;
        while (word[n] != '\0') ++n;
        if (s_.compare(pos_, n, word) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    JsonValue value(int depth) {
        // Deeply nested input is refused rather than followed until the
        // stack runs out: this reads text from the network.
        if (depth > 200) {
            fail("nested too deeply");
        }
        if (pos_ >= s_.size()) {
            fail("unexpected end");
        }
        JsonValue v;
        const char c = s_[pos_];
        if (c == '{') {
            v.kind = JsonValue::Kind::Object;
            ++pos_;
            skip();
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return v;
            }
            while (true) {
                skip();
                if (pos_ >= s_.size() || s_[pos_] != '"') {
                    fail("expected a field name");
                }
                std::string key = string();
                skip();
                if (pos_ >= s_.size() || s_[pos_] != ':') {
                    fail("expected ':'");
                }
                ++pos_;
                skip();
                v.fields[key] = value(depth + 1);
                skip();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == '}') {
                    ++pos_;
                    return v;
                }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.kind = JsonValue::Kind::Array;
            ++pos_;
            skip();
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return v;
            }
            while (true) {
                skip();
                v.items.push_back(value(depth + 1));
                skip();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == ']') {
                    ++pos_;
                    return v;
                }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.kind = JsonValue::Kind::String;
            v.text = string();
            return v;
        }
        if (literal("true")) {
            v.kind = JsonValue::Kind::Bool;
            v.boolean = true;
            return v;
        }
        if (literal("false")) {
            v.kind = JsonValue::Kind::Bool;
            return v;
        }
        if (literal("null")) {
            return v;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = pos_;
            ++pos_;
            while (pos_ < s_.size() &&
                   ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' ||
                    s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' ||
                    s_[pos_] == '-')) {
                ++pos_;
            }
            v.kind = JsonValue::Kind::Number;
            v.text = s_.substr(start, pos_ - start);
            v.number = std::strtod(v.text.c_str(), nullptr);
            return v;
        }
        fail("unexpected character");
    }

    static void appendUtf8(std::string& out, unsigned long cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    unsigned long hex4() {
        if (pos_ + 4 > s_.size()) {
            fail("short \\u escape");
        }
        unsigned long cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = s_[pos_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= h - '0';
            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
            else fail("bad \\u escape");
        }
        return cp;
    }

    std::string string() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) {
                fail("unterminated string");
            }
            const char c = s_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) {
                fail("unterminated escape");
            }
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned long cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        pos_ + 6 <= s_.size() && s_[pos_] == '\\' &&
                        s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        const unsigned long low = hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) +
                                 (low - 0xDC00);
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    fail("bad escape");
            }
        }
    }
};

}  // namespace

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (kind != Kind::Object) {
        return nullValue();
    }
    const auto found = fields.find(key);
    return found == fields.end() ? nullValue() : found->second;
}

std::string JsonValue::asString() const {
    if (kind == Kind::String || kind == Kind::Number) {
        return text;
    }
    return {};
}

JsonResult parseJson(const std::string& text) {
    return Parser(text).run();
}

}  // namespace nexus::system
