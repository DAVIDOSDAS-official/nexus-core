#pragma once

#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// One stanza of a Debian control file.
//
// Field names are case-insensitive and are stored lowercased.
class ControlStanza {
public:
    void set(const std::string& field, std::string value);

    std::optional<std::string> get(const std::string& field) const;

    // Convenience accessor: returns an empty string when absent.
    std::string value(const std::string& field) const;

    bool has(const std::string& field) const;

    bool empty() const;

    const std::map<std::string, std::string>& fields() const;

private:
    std::map<std::string, std::string> fields_;
};

std::vector<ControlStanza> parseControlStream(std::istream& input);

// Throws std::runtime_error when the file cannot be opened.
std::vector<ControlStanza> parseControlFile(const std::string& path);

}
