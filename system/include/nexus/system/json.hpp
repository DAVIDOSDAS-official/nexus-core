#pragma once

// A small JSON reader: enough to read what web services answer
// (ProtonDB, AreWeAntiCheatYet, the Steam store search) without
// pulling a library into an image that has none. Reads, never writes.

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nexus::system {

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;                       // String, and Number as written
    std::vector<JsonValue> items;           // Array
    std::map<std::string, JsonValue> fields;  // Object

    bool isNull() const { return kind == Kind::Null; }
    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    bool isString() const { return kind == Kind::String; }
    bool isNumber() const { return kind == Kind::Number; }

    // A missing field is a Null value, so lookups chain safely.
    const JsonValue& operator[](const std::string& key) const;

    // String as is; a number as written; anything else empty.
    std::string asString() const;
};

struct JsonResult {
    JsonValue value;
    std::string error;   // empty when the text parsed
};

JsonResult parseJson(const std::string& text);

}  // namespace nexus::system
