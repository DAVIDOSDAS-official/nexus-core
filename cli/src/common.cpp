// What more than one command uses: finding a component, the solver, the
// deployments rpm-ostree reports, os-release, small text helpers.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// True when two names differ by at most two letters (added, removed
// or changed) -- a typo, not a different word. Short names need to be
// closer: one letter off in a three-letter name is another name.
bool closeSpelling(const std::string& a, const std::string& b) {
    const std::size_t limit = std::min(a.size(), b.size()) >= 6 ? 2 : 1;

    if (a == b ||
        (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size())
            > limit) {
        return false;
    }

    std::vector<std::size_t> previous(b.size() + 1), current(b.size() + 1);

    for (std::size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }

    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t change =
                previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, change});
        }
        previous.swap(current);
    }

    return previous[b.size()] <= limit;
}

const Component* find(
    const std::vector<Component>& components,
    const std::string& id
) {
    for (const Component& component : components) {
        if (component.id() == id) {
            return &component;
        }
    }

    // On a multi-arch system ids are qualified ("libc6:amd64"), so a
    // plain name still has to resolve. If it is ambiguous, say so
    // rather than picking one silently.
    std::vector<const Component*> byName;

    for (const Component& component : components) {
        if (component.name() == id) {
            byName.push_back(&component);
        }
    }

    if (byName.size() == 1) {
        return byName.front();
    }

    if (byName.size() > 1) {
        std::cout
            << id << " is installed for more than one architecture:\n";

        for (const Component* component : byName) {
            std::cout << "    " << component->id() << "\n";
        }

        std::cout << "\nName one of them explicitly.\n";

        // Reported here; the caller must not also say "unknown".
        return nullptr;
    }

    // A name typed from memory gets close: "terminal" finds
    // qterminal, "geogabra" finds geogebra. Only installed packages
    // are searched, so say that too -- the commonest reason for no
    // answer is that it is simply not on this machine.
    std::vector<std::string> near;

    for (const Component& component : components) {
        const std::string& name = component.name();
        const bool contains =
            id.size() >= 3 && name.find(id) != std::string::npos;

        if (contains || closeSpelling(name, id)) {
            if (std::find(near.begin(), near.end(), name) == near.end()) {
                near.push_back(name);
            }
        }

        if (near.size() >= 6) {
            break;
        }
    }

    std::cout << "No installed package is called " << id << ".\n";

    if (!near.empty()) {
        std::cout << "Did you mean:";
        for (const std::string& name : near) {
            std::cout << " " << name;
        }
        std::cout << "?\n";
    }

    return nullptr;
}

// Which version algorithm to use is a property of the ecosystem, not
// a detail. dpkg and rpm order versions differently, and running one
// system's metadata through the other's comparator produces answers
// that look reasonable and are wrong.
bool gUseRpmVersions = false;

nexus::VersionComparator versionComparator() {
    if (gUseRpmVersions) {
        return [](const std::string& provided,
                  const std::string& required) {
            return nexus::system::compareRpmConstraint(
                provided, required);
        };
    }

    return [](const std::string& left, const std::string& right) {
        return nexus::system::compareVersions(left, right);
    };
}

// A detector that knows how each source orders its versions.
nexus::ConflictDetector buildDetector() {
    nexus::ConflictDetector detector(versionComparator());

    // Arch packages, reachable through a container, order versions
    // pacman's way regardless of what this machine runs.
    detector.setComparatorFor(
        nexus::Source::Container,
        [](const std::string& provided, const std::string& required) {
            return nexus::system::comparePacmanConstraint(
                provided, required);
        });

    return detector;
}

nexus::Solver buildSolver(const std::vector<Component>& components) {
    return nexus::Solver(components, buildDetector());
}

namespace {

std::vector<Deployment> parseDeployments(const std::string& text) {
    std::vector<Deployment> out;
    std::string key;
    bool inUpdate = false;
    std::istringstream lines(text);
    std::string line;

    auto words = [](const std::string& s) {
        std::vector<std::string> list;
        std::istringstream in(s);
        std::string word;
        while (in >> word) {
            if (word.size() >= 2 && word.front() == '\'' &&
                word.back() == '\'') {
                word = word.substr(1, word.size() - 2);
            }
            list.push_back(word);
        }
        return list;
    };

    while (std::getline(lines, line)) {
        if (line.rfind("\xe2\x97\x8f ", 0) == 0 ||   // "● "
            (line.size() > 2 && line[0] == ' ' && line[1] == ' ' &&
             line[2] != ' ')) {
            Deployment d;
            d.booted = line.rfind("\xe2\x97\x8f", 0) == 0;
            const auto start = line.find_first_not_of(" ", d.booted ? 4 : 2);
            d.image = start == std::string::npos ? "" : line.substr(start);
            out.push_back(std::move(d));
            key.clear();
            inUpdate = false;
            continue;
        }

        if (out.empty()) {
            continue;
        }

        // An update found by the automatic check is listed inside the
        // booted deployment, with its own Version line. That is the
        // version on offer, not the one running: skip it here (nexus
        // update reads it).
        if (line.find("AvailableUpdate:") != std::string::npos) {
            inUpdate = true;
            continue;
        }
        if (inUpdate) {
            continue;
        }

        const auto first = line.find_first_not_of(' ');
        if (first == std::string::npos || first < 4) {
            continue;
        }

        const auto colon = line.find(": ", first);
        const bool isKey =
            colon != std::string::npos &&
            line.substr(first, colon - first).find(' ') == std::string::npos;

        Deployment& d = out.back();

        if (isKey) {
            key = line.substr(first, colon - first);
            const std::string value = line.substr(colon + 2);

            if (key == "Version") {
                const auto paren = value.find(" (");
                d.version = value.substr(0, paren);
                if (paren != std::string::npos) {
                    d.date = value.substr(paren + 2);
                    if (!d.date.empty() && d.date.back() == ')') {
                        d.date.pop_back();
                    }
                }
            } else if (key == "Digest") {
                d.digest = value;
            } else if (key == "LayeredPackages") {
                for (const auto& w : words(value)) {
                    d.layered.push_back(w);
                }
            }
        } else if (key == "LayeredPackages") {
            for (const auto& w : words(line)) {
                d.layered.push_back(w);
            }
        }
    }

    return out;
}

}  // namespace

std::vector<Deployment> readDeployments() {
    if (!nexus::system::commandExists("rpm-ostree")) {
        return {};
    }
    const auto status = nexus::system::runCommand(
        "rpm-ostree status 2>/dev/null", false);
    if (!status.ok) {
        return {};
    }
    return parseDeployments(status.text);
}

std::string osReleaseValueIn(const std::string& file, const std::string& name) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(name + "=", 0) == 0) {
            std::string value = line.substr(name.size() + 1);
            if (value.size() >= 2 && value.front() == '"' &&
                value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return {};
}

std::string osReleaseValue(const std::string& name) {
    return osReleaseValueIn("/etc/os-release", name);
}

// Is this machine's update signature-checked? Asked in one place
// (0.1.95, round-4 audit L1): the summary and `nexus specs` went by the
// origin's prefix alone, `nexus security` by the prefix, the policy and
// the key, and `nexus doctor` said "signed" without looking. The rule
// itself is nexus::system::judgeUpdateSigning (update.hpp); this reads
// what it needs from the machine.
nexus::system::UpdateSigning updateSigningOf(const std::string& origin) {
    std::ifstream policy(nexus::system::kContainersPolicy);
    std::stringstream text;
    text << policy.rdbuf();
    std::error_code ec;
    return nexus::system::judgeUpdateSigning(
        origin, text.str(),
        std::filesystem::exists(nexus::system::kNexusPublicKey, ec));
}

// The same answer in the few words a row has room for.
std::string signingWords(nexus::system::UpdateSigning signing) {
    switch (signing) {
        case nexus::system::UpdateSigning::Enforced:
        case nexus::system::UpdateSigning::OtherSigned:
            return "signed";
        case nexus::system::UpdateSigning::PolicyMissing:
            return "signed: is set, but the Nexus key is not required";
        case nexus::system::UpdateSigning::NotChecked:
        case nexus::system::UpdateSigning::NotAnImage:
            break;
    }
    return "not signature-checked";
}

// "ostree-image-signed:docker://ghcr.io/x/nexus-core:minimal" ->
// "ghcr.io/x/nexus-core:minimal (signed)"
std::string describeImage(const std::string& image) {
    std::string shown = image;
    if (shown.rfind("ostree-image-signed:", 0) == 0) {
        shown = shown.substr(std::string("ostree-image-signed:").size());
    } else if (shown.rfind("ostree-unverified-registry:", 0) == 0) {
        shown = shown.substr(std::string("ostree-unverified-registry:").size());
    }
    if (shown.rfind("docker://", 0) == 0) {
        shown = shown.substr(9);
    }
    return shown + " (" + signingWords(updateSigningOf(image)) + ")";
}

std::string dayOf(const std::string& iso) {
    return iso.size() >= 10 ? iso.substr(0, 10) : iso;
}

// At most 4 MiB (0.1.84, outside review: a "grub.cfg" on another disk
// that is really a device would be read as root without end).
std::string readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text;
    char buffer[65536];
    while (in && text.size() < (4u << 20)) {
        in.read(buffer, sizeof buffer);
        text.append(buffer, static_cast<std::size_t>(in.gcount()));
    }
    if (text.size() > (4u << 20)) text.resize(4u << 20);
    return text;
}

bool confirmed(bool assumeYes) {
    if (assumeYes) {
        return true;
    }
    std::cout << "\nProceed? [Y/n] ";
    std::string answer;
    std::getline(std::cin, answer);
    return answer.empty() || answer == "y" || answer == "Y" ||
           answer == "yes";
}

std::string firstLine(const std::string& text) {
    const auto end = text.find('\n');
    return end == std::string::npos ? text : text.substr(0, end);
}

std::string shortDigest(const std::string& d) {
    const auto colon = d.find(':');
    const std::string hex = colon == std::string::npos ? d : d.substr(colon + 1);
    return hex.substr(0, 12);
}

}  // namespace nexus_cli
