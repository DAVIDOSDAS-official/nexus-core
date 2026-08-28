#include <nexus/system/control_file.hpp>

#include <cctype>
#include <fstream>
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

std::vector<ControlStanza> parseControlStream(std::istream& input) {
    std::vector<ControlStanza> stanzas;

    ControlStanza current;
    std::string currentField;
    std::string currentValue;

    const auto flushField = [&]() {
        if (!currentField.empty()) {
            current.set(currentField, trim(currentValue));
        }

        currentField.clear();
        currentValue.clear();
    };

    const auto flushStanza = [&]() {
        flushField();

        if (!current.empty()) {
            stanzas.push_back(current);
        }

        current = ControlStanza{};
    };

    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (trim(line).empty() && !isContinuation(line)) {
            flushStanza();
            continue;
        }

        if (isContinuation(line)) {
            if (!currentField.empty()) {
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
        currentValue = trim(line.substr(colon + 1));
    }

    flushStanza();

    return stanzas;
}

std::vector<ControlStanza> parseControlFile(const std::string& path) {
    std::ifstream input(path);

    if (!input) {
        throw std::runtime_error(
            "Cannot open control file: " + path
        );
    }

    return parseControlStream(input);
}

}
