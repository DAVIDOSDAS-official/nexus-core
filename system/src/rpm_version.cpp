#include <nexus/system/rpm_version.hpp>

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

bool isAlphanumeric(char character) {
    return isDigit(character) || isAlpha(character);
}

char at(const std::string& text, std::size_t index) {
    return index < text.size() ? text[index] : '\0';
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

int compareRpmSegments(
    const std::string& left,
    const std::string& right
) {
    if (left == right) {
        return 0;
    }

    std::size_t i = 0;
    std::size_t j = 0;

    while (at(left, i) != '\0' || at(right, j) != '\0') {
        // Separators carry no meaning at all in rpm: they are skipped
        // rather than compared, so 1.0.1 and 1_0-1 are the same
        // sequence of segments.
        while (at(left, i) != '\0' &&
               !isAlphanumeric(at(left, i)) &&
               at(left, i) != '~' &&
               at(left, i) != '^') {
            i += 1;
        }

        while (at(right, j) != '\0' &&
               !isAlphanumeric(at(right, j)) &&
               at(right, j) != '~' &&
               at(right, j) != '^') {
            j += 1;
        }

        // '~' sorts before everything, including the end of a string.
        if (at(left, i) == '~' || at(right, j) == '~') {
            if (at(left, i) != '~') {
                return 1;
            }

            if (at(right, j) != '~') {
                return -1;
            }

            i += 1;
            j += 1;
            continue;
        }

        // '^' sorts after the end of a string but before anything
        // else, which is how rpm expresses a post-release snapshot.
        if (at(left, i) == '^' || at(right, j) == '^') {
            if (at(left, i) == '\0') {
                return -1;
            }

            if (at(right, j) == '\0') {
                return 1;
            }

            if (at(left, i) != '^') {
                return 1;
            }

            if (at(right, j) != '^') {
                return -1;
            }

            i += 1;
            j += 1;
            continue;
        }

        if (at(left, i) == '\0' || at(right, j) == '\0') {
            break;
        }

        const std::size_t leftStart = i;
        const std::size_t rightStart = j;

        bool numeric = false;

        if (isDigit(at(left, i))) {
            numeric = true;

            while (isDigit(at(left, i))) {
                i += 1;
            }

            while (isDigit(at(right, j))) {
                j += 1;
            }
        } else {
            while (isAlpha(at(left, i))) {
                i += 1;
            }

            while (isAlpha(at(right, j))) {
                j += 1;
            }
        }

        // The segments are of different kinds. A numeric segment is
        // always newer than an alphabetic one.
        if (i == leftStart) {
            return -1;
        }

        if (j == rightStart) {
            return numeric ? 1 : -1;
        }

        std::size_t leftFrom = leftStart;
        std::size_t rightFrom = rightStart;

        if (numeric) {
            while (at(left, leftFrom) == '0') {
                leftFrom += 1;
            }

            while (at(right, rightFrom) == '0') {
                rightFrom += 1;
            }

            const std::size_t leftLength = i - leftFrom;
            const std::size_t rightLength = j - rightFrom;

            // More digits means a larger number.
            if (leftLength > rightLength) {
                return 1;
            }

            if (rightLength > leftLength) {
                return -1;
            }
        }

        const std::string leftSegment =
            left.substr(leftFrom, i - leftFrom);
        const std::string rightSegment =
            right.substr(rightFrom, j - rightFrom);

        const int comparison = leftSegment.compare(rightSegment);

        if (comparison != 0) {
            return sign(comparison);
        }
    }

    // Whichever still has characters left is the newer one.
    if (at(left, i) == '\0' && at(right, j) == '\0') {
        return 0;
    }

    return at(left, i) == '\0' ? -1 : 1;
}

RpmVersion parseRpmVersion(const std::string& text) {
    RpmVersion version;

    std::string body = text;

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

    const std::size_t hyphen = body.rfind('-');

    if (hyphen != std::string::npos) {
        version.release = body.substr(hyphen + 1);
        version.version = body.substr(0, hyphen);
    } else {
        version.version = body;
    }

    return version;
}

std::string toString(const RpmVersion& version) {
    std::string text;

    if (version.epoch != 0) {
        text += std::to_string(version.epoch);
        text += ":";
    }

    text += version.version;

    if (!version.release.empty()) {
        text += "-";
        text += version.release;
    }

    return text;
}

int compareRpmVersions(
    const RpmVersion& left,
    const RpmVersion& right
) {
    if (left.epoch != right.epoch) {
        return left.epoch < right.epoch ? -1 : 1;
    }

    const int version =
        compareRpmSegments(left.version, right.version);

    if (version != 0) {
        return version;
    }

    return compareRpmSegments(left.release, right.release);
}

int compareRpmConstraint(
    const std::string& provided,
    const std::string& required
) {
    RpmVersion left = parseRpmVersion(provided);
    const RpmVersion right = parseRpmVersion(required);

    // A constraint that names no epoch does not care about epoch.
    if (required.find(':') == std::string::npos) {
        left.epoch = 0;
    }

    // A constraint that names no release does not care about release.
    if (right.release.empty()) {
        left.release.clear();
    }

    return compareRpmVersions(left, right);
}

int compareRpmVersions(
    const std::string& left,
    const std::string& right
) {
    return compareRpmVersions(
        parseRpmVersion(left),
        parseRpmVersion(right)
    );
}

}
