#include <nexus/system/update.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>

#include <nexus/system/json.hpp>
#include <nexus/system/process.hpp>
#include <nexus/system/security_audit.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace nexus::system {

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

bool startsDeployment(const std::string& line) {
    // "● image" for the booted one, "  image" (two spaces) for others.
    return line.rfind("\xe2\x97\x8f ", 0) == 0 ||
           (line.size() > 2 && line[0] == ' ' && line[1] == ' ' &&
            line[2] != ' ');
}

}

SystemUpdate parseSystemUpdate(const std::string& statusText) {
    SystemUpdate out;
    std::istringstream lines(statusText);
    std::string line;
    bool inUpdate = false;
    bool inDeployments = false;
    bool booted = false;
    std::string deploymentVersion;
    std::string deploymentDigest;
    bool deploymentStaged = false;
    // The first deployment listed, when it is not the booted one, is
    // the one that starts next (0.1.95): rpm-ostree lists them in boot
    // order.
    bool pending = false;
    bool seenAny = false;

    while (std::getline(lines, line)) {
        const std::string text = trim(line);

        if (line.rfind("AutomaticUpdates:", 0) == 0) {
            const std::string value = trim(line.substr(17));
            const auto semicolon = value.find(';');
            out.policy = trim(value.substr(0, semicolon));
            if (semicolon != std::string::npos) {
                out.lastCheck = trim(value.substr(semicolon + 1));
            }
            continue;
        }
        if (line.rfind("Deployments:", 0) == 0) {
            inDeployments = true;
            continue;
        }
        if (!inDeployments) {
            continue;
        }

        if (startsDeployment(line)) {
            inUpdate = false;
            deploymentVersion.clear();
            deploymentDigest.clear();
            deploymentStaged = false;
            booted = line.rfind("\xe2\x97\x8f", 0) == 0;
            if (booted) {
                out.origin = imageReference(trim(line.substr(3)));
            }
            pending = !seenAny && !booted;
            seenAny = true;
            continue;
        }

        if (text == "AvailableUpdate:") {
            inUpdate = true;
            out.available = true;
            continue;
        }

        const auto colon = text.find(": ");
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = text.substr(0, colon);
        const std::string value = trim(text.substr(colon + 2));

        if (inUpdate) {
            if (key == "Version") {
                const auto paren = value.find(" (");
                out.version = value.substr(0, paren);
                if (paren != std::string::npos) {
                    out.date = value.substr(paren + 2);
                    if (!out.date.empty() && out.date.back() == ')') {
                        out.date.pop_back();
                    }
                }
            } else if (key == "Diff") {
                out.diff = value;
            }
            continue;
        }

        if (key == "Version") {
            deploymentVersion = value.substr(0, value.find(" ("));
            // "44.20261004.0 (2026-10-04T10:00:00Z)": when it was built.
            std::string built;
            const auto paren = value.find(" (");
            if (paren != std::string::npos) {
                built = value.substr(paren + 2);
                if (!built.empty() && built.back() == ')') {
                    built.pop_back();
                }
            }
            if (booted) {
                out.built = built;
            }
            if (pending) {
                out.pendingBuilt = built;
            }
        } else if (key == "Digest") {
            deploymentDigest = value;
            if (booted) {
                out.digest = value;
            }
            if (pending) {
                out.pendingDigest = value;
            }
            if (deploymentStaged) {
                out.stagedDigest = value;
            }
        } else if (key == "Staged" && value == "yes") {
            out.staged = true;
            deploymentStaged = true;
            out.stagedVersion = deploymentVersion;
            out.stagedDigest = deploymentDigest;
        }
    }

    return out;
}

std::string imageReference(const std::string& origin) {
    const std::string marker = "docker://";
    const auto at = origin.find(marker);
    if (at == std::string::npos) {
        return {};
    }
    return trim(origin.substr(at + marker.size()));
}

namespace {

// What a registry label may say before Nexus repeats it (0.1.85,
// outside review N1): skopeo reads labels before any signature check,
// so whoever can push to the registry without the key would otherwise
// write the text of every machine's "Update available" notification.
// A version: a digit, then up to 31 of [0-9A-Za-z.+~-]. A date: the
// characters of an ISO timestamp. Anything else is dropped.
bool plainVersion(const std::string& text) {
    if (text.empty() || text.size() > 32 || text[0] < '0' || text[0] > '9') return false;
    for (unsigned char c : text) {
        if (!std::isalnum(c) && c != '.' && c != '+' && c != '~' && c != '-') return false;
    }
    return true;
}

bool plainTimestamp(const std::string& text) {
    if (text.size() > 40) return false;
    for (unsigned char c : text) {
        if (!std::isdigit(c) && c != '-' && c != ':' && c != 'T' && c != 'Z' && c != '.' && c != '+') return false;
    }
    return true;
}

}  // namespace

RemoteImage parseImageInspect(const std::string& json) {
    RemoteImage out;
    const auto parsed = parseJson(json);
    if (!parsed.error.empty()) {
        return out;
    }
    out.digest = parsed.value["Digest"].asString();
    out.created = parsed.value["Created"].asString();
    if (!plainTimestamp(out.created)) out.created.clear();
    out.nexusVersion = parsed.value["Labels"]["nexus.version"].asString();
    if (!plainVersion(out.nexusVersion)) out.nexusVersion.clear();
    out.ok = out.digest.rfind("sha256:", 0) == 0;
    return out;
}

void compareWithRegistry(SystemUpdate& system, const RemoteImage& remote) {
    if (!remote.ok || system.digest.empty()) {
        return;
    }
    if (remote.digest == system.stagedDigest) {
        return;   // already downloaded; waiting for a restart
    }
    if (remote.digest != system.digest) {
        system.available = true;
        system.version = remote.nexusVersion.empty()
            ? std::string("a new image")
            : remote.nexusVersion;
        system.date = remote.created;
    }
}

// ---------------------------------------------------------------------
// An update never goes backwards (0.1.95, round-4 audit M5). What and
// why is in update.hpp.

namespace {

// Days since 1970-01-01 for a date of the calendar in use (the
// well-known days-from-civil arithmetic; no time zone, no locale).
long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

bool realDay(int year, int month, int day) {
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1) return false;
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return day <= lengths[month - 1] + (month == 2 && leap ? 1 : 0);
}

// Exactly `count` digits at `at`, as a number; -1 otherwise.
int digitsAt(const std::string& text, std::size_t at, std::size_t count) {
    if (at + count > text.size()) return -1;
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[at + i];
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

// "2026-11-14 03:41 UTC", for a sentence.
std::string sayTime(long long seconds) {
    const long long days = seconds >= 0 ? seconds / 86400 : -1;
    if (days < 0) return "?";
    // civil-from-days, the inverse of daysFromCivil.
    const long long z = days + 719468;
    const long long era = z / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const long long year = y + (m <= 2);
    const long long rest = seconds % 86400;
    char text[40];
    std::snprintf(text, sizeof text, "%04lld-%02u-%02u %02lld:%02lld UTC",
                  year, m, d, rest / 3600, (rest % 3600) / 60);
    return text;
}

// "0.1.95": digits and dots only, starting and ending with a digit.
// The only shape a Nexus version has (image/promote.sh refuses any
// other), and the only one compared here.
bool dottedNumber(const std::string& text) {
    if (text.empty() || text.front() == '.' || text.back() == '.') return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.') {
            if (text[i - 1] == '.') return false;
        } else if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// -1, 0, 1. Parts compared as numbers of any length (as digit strings,
// so nothing overflows); a missing part counts as 0: 0.1 == 0.1.0.
int compareDotted(const std::string& left, const std::string& right) {
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() || b < right.size()) {
        auto part = [](const std::string& text, std::size_t& at) {
            std::string digits;
            while (at < text.size() && text[at] != '.') digits += text[at++];
            if (at < text.size()) ++at;   // the dot
            const auto first = digits.find_first_not_of('0');
            return first == std::string::npos ? std::string() : digits.substr(first);
        };
        const std::string x = part(left, a);
        const std::string y = part(right, b);
        if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

}  // namespace

std::optional<long long> parseBuildTime(const std::string& iso) {
    // YYYY-MM-DDTHH:MM:SS[.fraction][Z|+hh:mm|-hh:mm|+hhmm|-hhmm]
    const int year = digitsAt(iso, 0, 4);
    const int month = digitsAt(iso, 5, 2);
    const int day = digitsAt(iso, 8, 2);
    const int hour = digitsAt(iso, 11, 2);
    const int minute = digitsAt(iso, 14, 2);
    const int second = digitsAt(iso, 17, 2);
    if (year < 0 || month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0 ||
        iso[4] != '-' || iso[7] != '-' || (iso[10] != 'T' && iso[10] != ' ') ||
        iso[13] != ':' || iso[16] != ':') {
        return std::nullopt;
    }
    if (!realDay(year, month, day) || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }
    std::size_t at = 19;
    if (at < iso.size() && iso[at] == '.') {
        ++at;
        const std::size_t start = at;
        while (at < iso.size() && iso[at] >= '0' && iso[at] <= '9') ++at;
        if (at == start) return std::nullopt;
    }
    long long offset = 0;
    if (at < iso.size()) {
        const char sign = iso[at];
        if (sign == 'Z') {
            ++at;
        } else if (sign == '+' || sign == '-') {
            const int oh = digitsAt(iso, at + 1, 2);
            std::size_t next = at + 3;
            if (next < iso.size() && iso[next] == ':') ++next;
            const int om = digitsAt(iso, next, 2);
            if (oh < 0 || om < 0 || oh > 14 || om > 59) return std::nullopt;
            offset = (oh * 3600LL + om * 60LL) * (sign == '+' ? 1 : -1);
            at = next + 2;
        }
        if (at != iso.size()) return std::nullopt;
    }
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           hour * 3600LL + minute * 60LL + second - offset;
}

std::optional<long long> parseBuildId(const std::string& buildId) {
    if (buildId.size() != 8) return std::nullopt;
    const int year = digitsAt(buildId, 0, 4);
    const int month = digitsAt(buildId, 4, 2);
    const int day = digitsAt(buildId, 6, 2);
    if (year < 0 || month < 0 || day < 0 || !realDay(year, month, day)) return std::nullopt;
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400;
}

OfferVerdict judgeOffer(const RunningBuild& running, const RemoteImage& offered) {
    OfferVerdict verdict;

    // The version first: it needs nothing but the tool's own number.
    if (dottedNumber(running.version) && dottedNumber(offered.nexusVersion) &&
        compareDotted(offered.nexusVersion, running.version) < 0) {
        verdict.order = OfferOrder::OlderVersion;
        verdict.reason = "The registry offers Nexus " + offered.nexusVersion +
                         ", which is older than Nexus " + running.version +
                         " running here.";
        return verdict;
    }

    // Then when each was built. rpm-ostree's timestamp is the build's
    // own time, to the second; BUILD_ID is the day the same build wrote
    // into itself. The two name the same day, or the timestamp the day
    // after (a build running over midnight). If they do not, one of
    // them is not what it is taken for here -- a local build that
    // reused layers carries an older BUILD_ID; an rpm-ostree that meant
    // something else by its timestamp would show a later one -- and the
    // earlier of the two is used: a weaker floor, but one that cannot
    // turn away a real update. Refusing every update by mistake would
    // be worse than the hole this closes.
    auto floor = parseBuildTime(running.built);
    const auto day = parseBuildId(running.buildId);
    if (day) {
        if (!floor) {
            floor = day;
        } else if (*floor < *day || *floor >= *day + 2 * 86400) {
            floor = std::min(*floor, *day);
        }
    }
    if (!floor) {
        // The machine cannot say when its own build was made (not a
        // Nexus image, or rpm-ostree did not answer): nothing to hold
        // the offer's date against. The version rule above still held.
        return verdict;
    }

    const auto offeredAt = parseBuildTime(offered.created);
    if (!offeredAt) {
        // Every image has a build time, and it cannot be taken out of
        // a signed one. An answer without it is not shown to be newer,
        // and is not taken.
        verdict.order = OfferOrder::Undated;
        verdict.reason = "The registry's build does not say when it was made, so it "
                         "cannot be shown to be newer than the one running here (made " +
                         sayTime(*floor) + ").";
        return verdict;
    }
    if (*offeredAt < *floor) {
        verdict.order = OfferOrder::OlderBuild;
        verdict.reason = "The registry offers a build made " + sayTime(*offeredAt) +
                         ", before the one running here (made " + sayTime(*floor) + ").";
    }
    return verdict;
}

void compareWithRegistry(SystemUpdate& system, const RemoteImage& remote,
                         const RunningBuild& running) {
    if (!remote.ok || system.digest.empty()) {
        return;
    }
    system.offeredDigest = remote.digest;
    // The build that runs, or the one already waiting: nothing offered.
    if (remote.digest == system.digest || remote.digest == system.stagedDigest) {
        compareWithRegistry(system, remote);
        return;
    }
    const OfferVerdict verdict = judgeOffer(running, remote);
    if (verdict.refused()) {
        // Not available, whatever rpm-ostree's own earlier check had
        // noted: what the registry answers now is what an upgrade
        // would pull.
        system.available = false;
        system.version.clear();
        system.date.clear();
        system.diff.clear();
        system.refused = true;
        system.refusedWhy = verdict.reason;
        system.refusedVersion = remote.nexusVersion;
        system.refusedDate = remote.created;
        return;
    }
    compareWithRegistry(system, remote);
}

// What was shown is what is installed (0.1.95, round-4 audit L9).
StagedResult judgeStaged(const std::string& confirmed,
                         const SystemUpdate& before,
                         const SystemUpdate& after) {
    if (after.pendingDigest.empty()) {
        return StagedResult::NothingNew;
    }
    if (!confirmed.empty() && after.pendingDigest != confirmed) {
        // What waited before this run and still waits is not this
        // run's doing, and not this run's to remove.
        return after.pendingDigest == before.pendingDigest ? StagedResult::NothingNew
                                                           : StagedResult::Different;
    }
    if (confirmed.empty() && after.pendingDigest == before.pendingDigest) {
        return StagedResult::NothingNew;
    }
    // rpm-ostree's own account of both builds, read after it checked
    // the signature: no registry answer is trusted for this one.
    const auto pendingAt = parseBuildTime(after.pendingBuilt);
    const auto runningAt = parseBuildTime(after.built);
    if (pendingAt && runningAt && *pendingAt < *runningAt) {
        return StagedResult::Older;
    }
    return StagedResult::AsConfirmed;
}

// One answer to "is this machine's update signature-checked?" (0.1.95,
// round-4 audit L1).
UpdateSigning judgeUpdateSigning(const std::string& origin,
                                 const std::string& policyJson,
                                 bool keyPresent) {
    if (origin.empty()) {
        return UpdateSigning::NotAnImage;
    }
    if (origin.rfind("ostree-image-signed:", 0) != 0) {
        return UpdateSigning::NotChecked;
    }
    // The prefix asks for a check; the policy says against which key
    // (0.1.76). Without the rule, signed: checks nothing of Nexus's own.
    // The repository must start with Nexus's scope, right after the
    // transport (0.2.6, audit NX-04): `find` anywhere said "Enforced" for
    // docker://evil.example/ghcr.io/davidosdas-official/..., a registry
    // the policy does not check at all.
    std::string repository = origin.substr(std::string("ostree-image-signed:").size());
    for (const char* transport : {"docker://", "registry:"}) {
        if (repository.rfind(transport, 0) == 0) {
            repository = repository.substr(std::string(transport).size());
            break;
        }
    }
    if (repository.rfind(std::string(kNexusImageScope) + "/", 0) != 0) {
        return UpdateSigning::OtherSigned;
    }
    return policyRequiresKey(policyJson, kNexusImageScope, kNexusPublicKey) && keyPresent
        ? UpdateSigning::Enforced
        : UpdateSigning::PolicyMissing;
}

std::vector<AppUpdate> parseFlatpakUpdates(const std::string& text) {
    std::vector<AppUpdate> out;
    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line)) {
        std::istringstream words(line);
        AppUpdate app;
        words >> app.id;
        words >> app.version;
        // An application id has at least two dots (org.example.App);
        // anything else is a header or a message, not an update. And
        // only the characters an id may have: it is printed and handed
        // to the Shop as it stands.
        int dots = 0;
        bool plain = !app.id.empty() && app.id.size() <= 255;
        for (unsigned char c : app.id) {
            dots += c == '.';
            plain = plain && (std::isalnum(c) || c == '.' || c == '_' || c == '-');
        }
        // The version is whatever the app's developer wrote, and it
        // reaches the notification and the Shop: filtered like any
        // outside text, and kept short (0.1.86, outside review R1).
        app.version = printable(app.version.substr(0, 64));
        if (dots >= 2 && plain) {
            out.push_back(app);
        }
    }

    return out;
}

std::string updateSummary(const SystemUpdate& system,
                          const std::vector<AppUpdate>& apps) {
    std::string out;
    if (system.available) {
        const bool numbered = !system.version.empty() &&
            system.version[0] >= '0' && system.version[0] <= '9';
        out = numbered ? "Nexus " + system.version
                       : std::string("A new Nexus version");
    }
    if (!apps.empty()) {
        if (!out.empty()) {
            out += " and ";
        }
        out += std::to_string(apps.size()) +
               (apps.size() == 1 ? " app update" : " app updates");
    }
    return out;
}

UpdateLines parseUpdateLines(const std::string& text) {
    UpdateLines result;
    std::size_t start = 0;

    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(start, end - start);
        start = end + 1;

        std::vector<std::string> fields;
        std::size_t from = 0;
        for (;;) {
            const std::size_t tab = line.find('\t', from);
            fields.push_back(line.substr(from, tab == std::string::npos
                ? std::string::npos : tab - from));
            if (tab == std::string::npos) {
                break;
            }
            from = tab + 1;
        }
        auto field = [&](std::size_t index) {
            return index < fields.size() ? fields[index] : std::string();
        };

        if (fields[0] == "running") {
            result.running = field(1);
            result.runningDay = field(2);
        } else if (fields[0] == "staged") {
            result.staged = field(1);
        } else if (fields[0] == "system") {
            result.system = field(1);
            result.version = field(2);
            result.day = field(3);
            result.diff = field(4);
        } else if (fields[0] == "app" && !field(1).empty()) {
            result.apps.push_back(AppUpdate{field(1), field(2)});
        }
    }
    return result;
}


std::string updateNotifyOffFile(const std::string& home) {
    return home + "/.config/nexus/update-notify-off";
}

bool updateNotifyIsOff(const std::string& home) {
    std::error_code ec;
    return !home.empty() &&
           std::filesystem::exists(updateNotifyOffFile(home), ec);
}

bool setUpdateNotify(const std::string& home, bool on, std::string& error) {
    if (home.empty() || home[0] != '/') {
        error = "no home directory";
        return false;
    }
    const std::filesystem::path flag = updateNotifyOffFile(home);
    std::error_code ec;
    if (on) {
        std::filesystem::remove(flag, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        return true;
    }
    std::filesystem::create_directories(flag.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    std::ofstream out(flag);
    out << "Update notifications are off for this account.\n"
           "Turn them on again: nexus update notify on --apply\n";
    if (!out) {
        error = "could not write " + flag.string();
        return false;
    }
    return true;
}

}
