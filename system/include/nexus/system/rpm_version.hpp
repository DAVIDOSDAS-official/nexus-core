#pragma once

#include <string>

namespace nexus::system {

// An RPM version: [epoch:]version[-release]
//
// The shape looks like Debian's and the rules are not. Keeping them in
// separate types stops one algorithm being quietly used for the other,
// which would be wrong in ways that only show up on odd versions.
struct RpmVersion {
    unsigned long epoch = 0;
    std::string version;
    std::string release;
};

RpmVersion parseRpmVersion(const std::string& text);

std::string toString(const RpmVersion& version);

// Compare one version segment, following rpm's rpmvercmp.
//
// The rules, and how they differ from dpkg:
//   - separators (anything not alphanumeric, ~ or ^) are skipped
//     entirely rather than compared
//   - runs of digits compare numerically, runs of letters compare
//     alphabetically, and a digit run always outranks a letter run
//   - '~' sorts before everything, including the end of the string,
//     so 1.0~rc1 < 1.0                       (same as dpkg)
//   - '^' sorts after the end of the string but before anything else,
//     so 1.0 < 1.0^ < 1.0.1                  (dpkg has no equivalent)
int compareRpmSegments(const std::string& left, const std::string& right);

// Compare complete versions: epoch first, then version, then release.
int compareRpmVersions(const RpmVersion& left, const RpmVersion& right);
int compareRpmVersions(
    const std::string& left,
    const std::string& right
);

}
