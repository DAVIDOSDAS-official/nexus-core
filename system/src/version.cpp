#include <nexus/system/version.hpp>

#include <cctype>
#include <cstdlib>

namespace nexus::system {

namespace {

bool isDigit(char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool isAlpha(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0;
}

// Sort weight of a single character within a version fragment.
//
// The three rules that matter:
//   - '~' sorts before everything, including the end of the string,
//     which is why 1.0~rc1 comes before 1.0
//   - letters sort before any other non-digit character
//   - the end of the string is weight 0, the same as a digit, which
//     is what lets the digit loop take over
int order(char character) {
    if (isDigit(character)) {
        return 0;
    }

    if (isAlpha(character)) {
        return static_cast<unsigned char>(character);
    }

    if (character == '~') {
        return -1;
    }

    if (character != '\0') {
        return static_cast<unsigned char>(character) + 256;
    }

    return 0;
}

char at(const std::string& text, std::size_t index) {
    return index < text.size() ? text[index] : '\0';
}

// Compare one fragment (an upstream version or a revision).
//
// Alternates between runs of non-digits, compared character by
// character using order(), and runs of digits, compared numerically
// with leading zeros ignored.
int compareFragment(const std::string& left, const std::string& right) {
    std::size_t i = 0;
    std::size_t j = 0;

    while (i < left.size() || j < right.size()) {
        int firstDifference = 0;

        while ((at(left, i) != '\0' && !isDigit(at(left, i))) ||
               (at(right, j) != '\0' && !isDigit(at(right, j)))) {

            const int leftOrder = order(at(left, i));
            const int rightOrder = order(at(right, j));

            if (leftOrder != rightOrder) {
                return leftOrder - rightOrder;
            }

            i += 1;
            j += 1;
        }

        while (at(left, i) == '0') {
            i += 1;
        }

        while (at(right, j) == '0') {
            j += 1;
        }

        while (isDigit(at(left, i)) && isDigit(at(right, j))) {
            if (firstDifference == 0) {
                firstDifference = at(left, i) - at(right, j);
            }

            i += 1;
            j += 1;
        }

        // A longer run of digits is a larger number.
        if (isDigit(at(left, i))) {
            return 1;
        }

        if (isDigit(at(right, j))) {
            return -1;
        }

        if (firstDifference != 0) {
            return firstDifference;
        }
    }

    return 0;
}

int sign(int value) {
    if (value < 0) {
        return -1;
    }

    if (value > 0) {
        return 1;
    }

    return 0;
}

}

Version parseVersion(const std::string& text) {
    Version version;

    std::string body = text;

    // Leading and trailing whitespace is not part of the version.
    const std::size_t begin = body.find_first_not_of(" \t");

    if (begin == std::string::npos) {
        return version;
    }

    const std::size_t end = body.find_last_not_of(" \t");

    body = body.substr(begin, end - begin + 1);

    // Epoch: digits before the first colon. Anything else means there
    // is no epoch and the colon belongs to the upstream version.
    const std::size_t colon = body.find(':');

    if (colon != std::string::npos) {
        bool digitsOnly = colon > 0;

        for (std::size_t index = 0; index < colon; ++index) {
            if (!isDigit(body[index])) {
                digitsOnly = false;
                break;
            }
        }

        if (digitsOnly) {
            version.epoch = std::strtoul(
                body.substr(0, colon).c_str(),
                nullptr,
                10
            );

            body = body.substr(colon + 1);
        }
    }

    // Revision: everything after the last hyphen.
    const std::size_t hyphen = body.rfind('-');

    if (hyphen != std::string::npos) {
        version.revision = body.substr(hyphen + 1);
        version.upstream = body.substr(0, hyphen);
    } else {
        version.upstream = body;
    }

    return version;
}

std::string validateVersion(const std::string& text) {
    if (text.empty()) {
        return "version is empty";
    }

    const std::size_t colon = text.find(':');

    if (colon != std::string::npos) {
        if (colon == 0) {
            return "epoch is empty";
        }

        for (std::size_t index = 0; index < colon; ++index) {
            if (!isDigit(text[index])) {
                return "epoch is not a number";
            }
        }
    }

    const Version version = parseVersion(text);

    if (version.upstream.empty()) {
        return "upstream version is empty";
    }

    // A hyphen introduces a revision, and dpkg requires that revision
    // to be non-empty: "1.0-" is bad syntax, not version 1.0.
    if (text.find('-') != std::string::npos &&
        version.revision.empty()) {
        return "revision is empty";
    }

    if (!isDigit(version.upstream.front())) {
        return "upstream version does not start with a digit";
    }

    for (char character : version.upstream) {
        const bool allowed =
            isDigit(character) || isAlpha(character) ||
            character == '.' || character == '+' ||
            character == '-' || character == ':' ||
            character == '~';

        if (!allowed) {
            return "upstream version contains an invalid character";
        }
    }

    for (char character : version.revision) {
        const bool allowed =
            isDigit(character) || isAlpha(character) ||
            character == '.' || character == '+' ||
            character == '~';

        if (!allowed) {
            return "revision contains an invalid character";
        }
    }

    return "";
}

std::string toString(const Version& version) {
    std::string text;

    if (version.epoch != 0) {
        text += std::to_string(version.epoch);
        text += ":";
    }

    text += version.upstream;

    if (!version.revision.empty()) {
        text += "-";
        text += version.revision;
    }

    return text;
}

int compareVersions(const Version& left, const Version& right) {
    if (left.epoch != right.epoch) {
        return left.epoch < right.epoch ? -1 : 1;
    }

    const int upstream =
        compareFragment(left.upstream, right.upstream);

    if (upstream != 0) {
        return sign(upstream);
    }

    return sign(compareFragment(left.revision, right.revision));
}

int compareVersions(const std::string& left, const std::string& right) {
    return compareVersions(parseVersion(left), parseVersion(right));
}

bool satisfies(
    const Version& version,
    const VersionConstraint& constraint
) {
    const int result =
        compareVersions(version, parseVersion(constraint.version));

    switch (constraint.relation) {
        case VersionRelation::Earlier:
            return result < 0;
        case VersionRelation::EarlierOrEqual:
            return result <= 0;
        case VersionRelation::Exactly:
            return result == 0;
        case VersionRelation::LaterOrEqual:
            return result >= 0;
        case VersionRelation::Later:
            return result > 0;
    }

    return false;
}

bool satisfies(
    const std::string& version,
    const VersionConstraint& constraint
) {
    return satisfies(parseVersion(version), constraint);
}

}
