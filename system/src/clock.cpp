#include <nexus/system/clock.hpp>

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace nexus::system {

std::optional<double> parseChronyOffset(const std::string& tracking) {
    std::istringstream in(tracking);
    std::string line;
    while (std::getline(in, line)) {
        // "System time" is the key; the colon may be padded.
        if (line.rfind("System time", 0) != 0) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) return std::nullopt;

        std::istringstream rest(line.substr(colon + 1));
        std::string number, unit, direction;
        rest >> number >> unit >> direction;
        if (unit != "seconds" && unit != "second") return std::nullopt;

        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        if (number.empty() || end == nullptr || *end != '\0' ||
            !std::isfinite(value)) {
            return std::nullopt;
        }
        if (direction == "slow") return value;
        if (direction == "fast") return -value;
        return std::nullopt;
    }
    return std::nullopt;
}

std::string describeOffset(double seconds) {
    const double s = std::fabs(seconds);
    auto words = [](long n, const char* one) {
        std::string out = std::to_string(n) + " " + one;
        if (n != 1) out += "s";
        return out;
    };
    if (s >= 86400) return words(static_cast<long>(s / 86400), "day");
    if (s >= 3600) return words(static_cast<long>(s / 3600), "hour");
    if (s >= 60) return words(static_cast<long>(s / 60), "minute");
    return words(static_cast<long>(s), "second");
}

std::optional<bool> chronyFollowsServer(const std::string& tracking) {
    std::istringstream in(tracking);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Leap status", 0) != 0) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) return std::nullopt;
        const std::string value = line.substr(colon + 1);
        if (value.find("Not synchronised") != std::string::npos) return false;
        if (value.find("Normal") != std::string::npos ||
            value.find("Insert second") != std::string::npos ||
            value.find("Delete second") != std::string::npos) {
            return true;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<long long> parseRtcSinceEpoch(const std::string& text) {
    std::string digits = text;
    while (!digits.empty() &&
           (digits.back() == '\n' || digits.back() == ' ' || digits.back() == '\r')) {
        digits.pop_back();
    }
    if (digits.empty() || digits.size() > 18) return std::nullopt;
    for (const char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
    }
    return std::stoll(digits);
}

bool timezoneNotChosen(const std::string& zone, bool chosenMarkerExists) {
    return !chosenMarkerExists && zone == kInstallerDefaultTimezone;
}

}  // namespace nexus::system
