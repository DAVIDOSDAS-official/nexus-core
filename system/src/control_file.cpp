#include <nexus/system/control_file.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <istream>
#include <stdexcept>
#include <utility>

namespace nexus::system {

namespace {

std::string lowercase(const std::string& text) {
    std::string output;
    output.reserve(text.size());

    for (char character : text) {
        output.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        ));
    }

    return output;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

bool isContinuation(const std::string& line) {
    return !line.empty() && (line[0] == ' ' || line[0] == '\t');
}

// A comment, and nothing else.
//
// Debian's control format has no comments and this parser never had
// any -- but the profiles and alias tables are written in the same
// format by hand and are full of them. They survive by accident: a
// line with no colon is dropped as malformed, and dropping it does
// not end the field being read, so a colonless comment is harmless
// anywhere.
//
// A comment with a colon in it is not. It is parsed as a field, which
// ends the field above it:
//
//     Requires: init,        The comment is read as a field named
//      c-library,            "# and one more thing", so Requires
//     # and one more thing:  ends at c-library and terminal-emulator
//      terminal-emulator     joins the comment instead.
//
// The profile then reports itself complete while asking for one
// package fewer than it says, and the generated image is missing it.
// Nothing warns, because from the parser's side nothing went wrong.
//
// No table triggers this today. Nine comments across the profiles and
// alias tables already contain a colon and are already parsed as
// fields; none of them happens to sit above a continuation line. That
// is a property of where the prose broke across lines, not of
// anything anybody decided, and it stops being true the first time
// somebody adds a note in the middle of a list.
//
// Only at column zero. A '#' inside a value, or on a continuation
// line, is part of the value.
bool isComment(const std::string& line) {
    return !line.empty() && line[0] == '#';
}

}

void ControlStanza::set(const std::string& field, std::string value) {
    fields_[lowercase(field)] = std::move(value);
}

std::optional<std::string> ControlStanza::get(
    const std::string& field
) const {
    const auto entry = fields_.find(lowercase(field));

    if (entry == fields_.end()) {
        return std::nullopt;
    }

    return entry->second;
}

std::string ControlStanza::value(const std::string& field) const {
    return get(field).value_or("");
}

bool ControlStanza::has(const std::string& field) const {
    return fields_.count(lowercase(field)) > 0;
}

bool ControlStanza::empty() const {
    return fields_.empty();
}

const std::map<std::string, std::string>& ControlStanza::fields() const {
    return fields_;
}

std::vector<ControlStanza> parseControlStream(
    std::istream& input,
    const std::set<std::string>& wantedFields
) {
    const std::set<std::string>* wanted =
        wantedFields.empty() ? nullptr : &wantedFields;

    std::vector<ControlStanza> stanzas;

    ControlStanza current;
    std::string currentField;
    std::string currentValue;

    bool keepingField = true;

    const auto flushField = [&]() {
        if (!currentField.empty() && keepingField) {
            current.set(currentField, trim(currentValue));
        }

        currentField.clear();
        currentValue.clear();
        keepingField = true;
    };

    const auto flushStanza = [&]() {
        flushField();

        if (!current.empty()) {
            stanzas.push_back(std::move(current));
        }

        current = ControlStanza{};
    };

    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Before everything else, so a comment neither ends a stanza
        // nor interrupts the field it was written inside.
        if (isComment(line)) {
            continue;
        }

        if (trim(line).empty() && !isContinuation(line)) {
            flushStanza();
            continue;
        }

        if (isContinuation(line)) {
            if (!currentField.empty() && keepingField) {
                currentValue += "\n";
                currentValue += trim(line);
            }
            continue;
        }

        const std::size_t colon = line.find(':');

        if (colon == std::string::npos) {
            // Malformed line: ignore rather than abort the whole file.
            continue;
        }

        flushField();

        currentField = trim(line.substr(0, colon));

        keepingField =
            wanted == nullptr ||
            wanted->count(lowercase(currentField)) > 0;

        if (!keepingField) {
            currentValue.clear();
            continue;
        }

        currentValue = trim(line.substr(colon + 1));
    }

    flushStanza();

    return stanzas;
}

std::vector<ControlStanza> parseControlStream(std::istream& input) {
    static const std::set<std::string> everything;

    return parseControlStream(input, everything);
}

std::vector<ControlStanza> parseControlFile(const std::string& path) {
    // A directory opens successfully and reads as nothing, so a
    // system with a broken path reports zero packages rather than an
    // error -- and everything downstream reasons happily from that.
    // An empty result from broken input is the dangerous failure.
    std::error_code error;

    if (std::filesystem::is_directory(path, error)) {
        throw std::runtime_error(
            "Not a control file, it is a directory: " + path);
    }

    std::ifstream input(path);

    if (!input) {
        throw std::runtime_error(
            "Cannot open control file: " + path
        );
    }

    return parseControlStream(input);
}

}
