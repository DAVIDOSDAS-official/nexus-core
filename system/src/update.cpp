#include <nexus/system/update.hpp>
#include <cctype>

#include <nexus/system/json.hpp>
#include <nexus/system/process.hpp>

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
        } else if (key == "Digest") {
            deploymentDigest = value;
            if (booted) {
                out.digest = value;
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
