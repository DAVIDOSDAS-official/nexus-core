#include <nexus/system/plan_check.hpp>

#include <nexus/system/process.hpp>
#include <nexus/system/rpm_version.hpp>

#include <algorithm>
#include <sys/utsname.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace nexus::system {

PlanCheck checkPlanWithApt(
    const std::string& requested,
    const std::set<std::string>& expected
) {
    PlanCheck check;

    if (!commandExists("apt-get")) {
        check.agreement = PlanAgreement::Unavailable;
        return check;
    }

    // A dry run changes nothing and needs no privileges. Its stderr
    // is kept: discarding it turns a refusal into a statement with no
    // content, and the tool was saying exactly what was wrong.
    //
    // The path includes the process id. A fixed name in /tmp belongs
    // to whoever created it first, and /tmp is sticky -- so running
    // once as a user and then again under sudo left root unable to
    // write to its own scratch file.
    const ScratchDirectory scratch;   // not a guessable /tmp name (0.1.78)
    const std::string errors = scratch.file("stderr");

    const std::string command =
        "apt-get install --dry-run --no-install-recommends " +
        shellQuote(requested) + " 2>" + errors;

    std::FILE* pipe = popen(inPlainLocale(command).c_str(), "r");

    if (pipe == nullptr) {
        check.agreement = PlanAgreement::Unavailable;
        return check;
    }

    char line[4096];

    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        const std::string text(line);

        if (text.rfind("Inst ", 0) != 0) {
            continue;
        }

        const std::size_t start = 5;
        const std::size_t end = text.find(' ', start);

        check.theirs.insert(text.substr(start, end - start));
    }

    const int status = pclose(pipe);
    const int code = exitCodeFromStatus(status);

    if (code != 0 && check.theirs.empty()) {
        std::ifstream why(errors);

        if (why) {
            std::string message;

            while (std::getline(why, message)) {
                if (!message.empty()) {
                    check.refusal.push_back(message);
                }
            }
        }


        // A refusal with nothing said is not a refusal. apt always
        // explains itself, so an empty message means the check never
        // ran -- and reporting that as "apt refuses this plan" blames
        // the wrong thing entirely.
        check.agreement = check.refusal.empty()
            ? PlanAgreement::Unavailable
            : PlanAgreement::Refused;

        return check;
    }


    for (const std::string& name : expected) {
        if (check.theirs.count(name) == 0) {
            check.onlyOurs.push_back(name);
        }
    }

    for (const std::string& name : check.theirs) {
        if (expected.count(name) == 0) {
            check.onlyTheirs.push_back(name);
        }
    }

    check.agreement =
        (check.onlyOurs.empty() && check.onlyTheirs.empty())
            ? PlanAgreement::Agrees
            : PlanAgreement::Differs;

    return check;
}

namespace {

bool isArchitecture(const std::string& word) {
    static const std::set<std::string> known = {
        "x86_64", "i686", "i386", "noarch", "aarch64", "armv7hl",
        "ppc64le", "s390x", "riscv64", "src", "amd64", "arm64", "all"};
    return known.count(word) > 0;
}

std::string nativeArchitecture() {
    static const std::string machine = [] {
        struct utsname info {};
        return ::uname(&info) == 0 ? std::string(info.machine) : std::string("x86_64");
    }();
    return machine;
}

std::string stripArchitecture(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos && isArchitecture(name.substr(dot + 1))) {
        return name.substr(0, dot);
    }
    return name;
}

// Which kind of change a heading introduces; nullptr for anything else
// ("Transaction Summary", "Skipping packages ...", a rule of "=").
std::set<std::string>* sectionFor(const std::string& heading,
                                  PackageChanges& changes) {
    auto starts = [&](const char* word) { return heading.rfind(word, 0) == 0; };
    if (starts("Installing")) return &changes.install;
    if (starts("Upgrading")) return &changes.upgrade;
    if (starts("Reinstalling")) return &changes.reinstall;
    if (starts("Downgrading")) return &changes.downgrade;
    if (starts("Removing")) return &changes.remove;
    if (starts("Replacing") || starts("Obsoleting")) return &changes.replaced;
    return nullptr;
}

// A "replacing" line under an upgrade names the old version of the
// same package (dnf 5); that is the upgrade itself, not a package
// being taken away. Matched by packageKey, name AND architecture
// (0.1.98, round-5 audit L3): "replacing glibc.i686" under an upgrade
// of glibc.x86_64 is a 32-bit library going, and was dropped as the
// upgrade's own old version.
void dropSelfReplacements(PackageChanges& changes, const std::set<std::string>& sameKeys) {
    for (auto it = changes.replaced.begin(); it != changes.replaced.end();) {
        if (sameKeys.count(*it)) {
            it = changes.replaced.erase(it);
        } else {
            ++it;
        }
    }
}

// The key of a "replacing" row: dnf 5 gives the architecture as the
// next column ("replacing foo x86_64 1-1"), dnf 4 on the name
// ("replacing bar.x86_64 1-1").
std::string replacedKey(const std::string& old, std::istringstream& rest) {
    const std::size_t dot = old.rfind('.');
    if (dot != std::string::npos && isArchitecture(old.substr(dot + 1))) {
        return packageKey(old.substr(0, dot), old.substr(dot + 1));
    }
    std::string arch;
    rest >> arch;
    return packageKey(old, isArchitecture(arch) ? arch : "");
}

// Headings inside a transaction table that announce no change to a
// package: dnf's "Skipping packages ..." (left out), dnf 5's "Changing
// reason" (how a package is marked, not what it contains).
bool harmlessHeading(const std::string& heading) {
    return heading.rfind("Skipping", 0) == 0 || heading.rfind("Changing reason", 0) == 0;
}

bool isRule(const std::string& line) {
    return line.find_first_not_of("=-") == std::string::npos;
}

}

std::vector<std::string> PackageChanges::describeBeyondInstalling() const {
    std::vector<std::string> lines;
    for (const auto& name : remove) lines.push_back("remove " + name);
    for (const auto& name : replaced) lines.push_back("replace " + name);
    for (const auto& name : downgrade) lines.push_back("downgrade " + name);
    for (const auto& row : unknown) lines.push_back("unknown change, " + row);
    return lines;
}

std::string evrFromNevra(const std::string& nevra) {
    std::string text = nevra;
    const std::size_t space = text.find(' ');
    if (space != std::string::npos) text = text.substr(0, space);
    text = stripArchitecture(text);
    const std::string name = packageNameFromNevra(text);
    if (name.size() + 1 >= text.size()) return "";
    return text.substr(name.size() + 1);
}


std::string packageKey(const std::string& name, const std::string& arch) {
    if (arch.empty() || arch == "noarch" || arch == nativeArchitecture()) return name;
    return name + "." + arch;
}

std::string packageKeyFromNevra(const std::string& nevra) {
    std::string text = nevra;
    const std::size_t space = text.find(' ');
    if (space != std::string::npos) text = text.substr(0, space);
    const std::size_t dot = text.rfind('.');
    const std::string arch =
        dot != std::string::npos && isArchitecture(text.substr(dot + 1)) ? text.substr(dot + 1) : "";
    return packageKey(packageNameFromNevra(text), arch);
}

std::string packageNameFromNevra(const std::string& nevra) {
    std::string text = nevra;
    const std::size_t space = text.find(' ');
    if (space != std::string::npos) text = text.substr(0, space);
    text = stripArchitecture(text);
    // name-version-release: the last two '-' parts are version and
    // release; a name may hold '-' itself, a version never does.
    const std::size_t release = text.rfind('-');
    if (release == std::string::npos || release == 0) return text;
    const std::size_t version = text.rfind('-', release - 1);
    if (version == std::string::npos || version == 0) return text;
    return text.substr(0, version);
}

// dnf prints a transaction table (dnf 5 shown; dnf 4 is the same shape
// with rules of "=" and no colon after "Transaction Summary"):
//
//     Installing:
//      gamemode        x86_64  1.8.1-3.fc42  fedora  180 k
//     Installing dependencies:
//      libgamemode      x86_64  ...
//     Upgrading:
//      foo              x86_64  2-1 ...
//        replacing foo  x86_64  1-1 ...          (dnf 5)
//          replacing  bar.x86_64 1-1             (dnf 4, an obsolete)
//     Removing dependent packages:
//      baz              x86_64  ...
//
// Package names are the first column of an indented row inside one of
// those sections; a "replacing" row names what goes.
PackageChanges parseDnfTransaction(const std::string& text) {
    PackageChanges changes;
    std::istringstream input(text);
    std::string line;
    std::set<std::string>* section = nullptr;
    std::set<std::string> sameKeys;   // install/upgrade/reinstall/downgrade rows

    // Between the first heading this reader knows and "Transaction
    // Summary". Outside it, indented rows are repositories being
    // loaded, counts and the like; inside it, a heading not known is a
    // change not understood (round-5 audit L3).
    bool inTable = false;
    std::string unknownHeading;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Section headings start at column zero.
        if (line.front() != ' ') {
            unknownHeading.clear();
            section = sectionFor(line, changes);
            if (section != nullptr) {
                inTable = true;
            } else if (line.rfind("Transaction Summary", 0) == 0) {
                inTable = false;
            } else if (inTable && !isRule(line) && !harmlessHeading(line)) {
                unknownHeading = line;
                if (unknownHeading.back() == ':') unknownHeading.pop_back();
            }
            continue;
        }

        std::istringstream columns(line);
        std::string first;
        if (!(columns >> first) || first.empty()) continue;

        if (!unknownHeading.empty()) {
            changes.unknown.insert(unknownHeading + ": " + first);
            continue;
        }
        if (section == nullptr) continue;

        if (first == "replacing" || first == "Replacing") {
            std::string old;
            if (columns >> old) changes.replaced.insert(replacedKey(old, columns));
            continue;
        }
        std::string arch;
        columns >> arch;
        const std::string key = packageKey(first, isArchitecture(arch) ? arch : "");
        section->insert(section == &changes.replaced ? key : first);
        if (section == &changes.install || section == &changes.upgrade ||
            section == &changes.reinstall) {
            changes.installKeys.insert(key);
        }
        if (section != &changes.remove && section != &changes.replaced) sameKeys.insert(key);
    }

    dropSelfReplacements(changes, sameKeys);
    return changes;
}

PackageChanges parseAptSimulation(const std::string& text) {
    PackageChanges changes;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream words(line);
        std::string action, name;
        if (!(words >> action >> name)) continue;
        name = stripArchitecture(name);
        const std::size_t colon = name.find(':');   // name:amd64
        if (colon != std::string::npos) name = name.substr(0, colon);
        if (action == "Inst") changes.install.insert(name);
        else if (action == "Remv" || action == "Purg") changes.remove.insert(name);
    }
    return changes;
}

PackageChanges parseRpmOstreeDryRun(const std::string& text) {
    PackageChanges changes;
    std::istringstream input(text);
    std::string line;
    std::set<std::string>* section = nullptr;
    std::set<std::string> sameKeys;
    std::string unknownHeading;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line.front() != ' ') {
            // "Installing 193 packages:", "Removing 1 package:" ...
            const bool list = line.back() == ':' && line.find(" package") != std::string::npos;
            section = list ? sectionFor(line, changes) : nullptr;
            // A list of packages under a heading not known (round-5
            // audit L3): shown and asked about, not skipped.
            unknownHeading = (list && section == nullptr) ? line.substr(0, line.size() - 1) : "";
            continue;
        }

        std::istringstream columns(line);
        std::string nevra;
        if (!(columns >> nevra)) continue;
        if (!unknownHeading.empty()) {
            changes.unknown.insert(unknownHeading + ": " + nevra);
            continue;
        }
        if (section == nullptr) continue;

        const std::string key = packageKeyFromNevra(nevra);
        section->insert(section == &changes.replaced ? key : packageNameFromNevra(nevra));
        if (section == &changes.install || section == &changes.upgrade ||
            section == &changes.reinstall) {
            changes.installKeys.insert(key);
        }
        if (section != &changes.remove && section != &changes.replaced) {
            sameKeys.insert(key);
            const std::string evr = evrFromNevra(nevra);
            if (!evr.empty()) changes.versions[key] = evr;
        }
    }

    dropSelfReplacements(changes, sameKeys);
    return changes;
}

PlanCheck checkPlanWithDnf(
    const std::string& requested,
    const std::set<std::string>& expected
) {
    PlanCheck check;

    if (!commandExists("dnf")) {
        check.agreement = PlanAgreement::Unavailable;
        return check;
    }

    const ScratchDirectory scratch;   // not a guessable /tmp name (0.1.78)
    const std::string errors = scratch.file("stderr");

    // --assumeno makes dnf resolve and print the transaction, then
    // decline it. Nothing is downloaded and nothing is changed.
    const std::string command =
        "dnf install --assumeno --setopt=install_weak_deps=False " +
        shellQuote(requested) + " 2>" + errors;

    std::FILE* pipe = popen(inPlainLocale(command).c_str(), "r");

    if (pipe == nullptr) {
        check.agreement = PlanAgreement::Unavailable;
        return check;
    }

    std::string output;
    char line[4096];

    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        output += line;
    }

    pclose(pipe);

    check.changes = parseDnfTransaction(output);
    check.theirs = check.changes.install;
    check.theirs.insert(check.changes.upgrade.begin(), check.changes.upgrade.end());
    check.theirs.insert(check.changes.reinstall.begin(), check.changes.reinstall.end());

    if (check.theirs.empty()) {
        std::ifstream why(errors);

        if (why) {
            std::string message;

            while (std::getline(why, message)) {
                if (!message.empty()) {
                    check.refusal.push_back(message);
                }
            }
        }


        // As with apt: a refusal that says nothing is not a refusal,
        // it is a check that never ran.
        check.agreement = check.refusal.empty()
            ? PlanAgreement::Unavailable
            : PlanAgreement::Refused;

        return check;
    }


    for (const std::string& name : expected) {
        if (check.theirs.count(name) == 0) {
            check.onlyOurs.push_back(name);
        }
    }

    for (const std::string& name : check.theirs) {
        if (expected.count(name) == 0) {
            check.onlyTheirs.push_back(name);
        }
    }

    // A plan that would also remove, replace or downgrade is not the
    // plan that was shown, whatever the names to install (0.1.91).
    check.agreement =
        (check.onlyOurs.empty() && check.onlyTheirs.empty() &&
         !check.changes.beyondInstalling())
            ? PlanAgreement::Agrees
            : PlanAgreement::Differs;

    return check;
}

namespace {

struct DryRun {
    bool ran = false;
    int code = -1;
    std::string output;
    std::vector<std::string> errors;
};

// Run a dry run with stderr kept apart: stdout is the plan, stderr the
// reason when there is none.
DryRun dryRun(const std::string& command) {
    DryRun run;
    const ScratchDirectory scratch;
    const std::string errors = scratch.file("stderr");
    std::FILE* pipe = popen(inPlainLocale(command + " 2>" + shellQuote(errors)).c_str(), "r");
    if (pipe == nullptr) return run;
    char line[4096];
    while (std::fgets(line, sizeof(line), pipe) != nullptr) run.output += line;
    run.code = exitCodeFromStatus(pclose(pipe));
    run.ran = true;
    std::ifstream why(errors);
    std::string message;
    while (why && std::getline(why, message)) {
        if (!message.empty()) run.errors.push_back(message);
    }
    return run;
}

std::string quotedList(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& name : names) out += " " + shellQuote(name);
    return out;
}

// No plan and nothing said: the check did not run. No plan and a
// message: the tool refused, and the message is why.
void noPlan(PlanCheck& check, const DryRun& run) {
    check.refusal = run.errors;
    check.agreement = check.refusal.empty() ? PlanAgreement::Unavailable
                                            : PlanAgreement::Refused;
}

bool needsRootMessage(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        if (line.find("superuser") != std::string::npos ||
            line.find("root user") != std::string::npos ||
            line.find("are you root") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// "glibc.x86_64 2.43-5.fc44" lines (rpm --qf '%{NAME}.%{ARCH} %{EVR}\n')
// as packageKey -> version; the newest where one name is installed
// more than once. A line with no version keeps the key, version "".
std::map<std::string, std::string> keyVersions(const std::string& output) {
    std::map<std::string, std::string> versions;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string nameArch, evr;
        if (!(words >> nameArch)) continue;
        words >> evr;
        const std::size_t dot = nameArch.rfind('.');
        const std::string key = (dot != std::string::npos && isArchitecture(nameArch.substr(dot + 1)))
            ? packageKey(nameArch.substr(0, dot), nameArch.substr(dot + 1))
            : nameArch;
        if (key.empty() || !isPackageName(stripArchitecture(key))) continue;
        auto found = versions.find(key);
        if (found == versions.end()) {
            versions[key] = evr;
        } else if (!evr.empty() && (found->second.empty() || compareRpmVersions(evr, found->second) > 0)) {
            found->second = evr;
        }
    }
    return versions;
}

// For each package layered earlier that the new layer lists again:
// does its version change, and which way (round-5 audit L4)?
void noteMoves(PlanCheck& check,
               const std::map<std::string, std::string>& installed,
               const std::map<std::string, std::string>& planned) {
    for (const auto& key : check.refreshed) {
        const auto from = installed.find(key);
        const auto to = planned.find(key);
        if (from == installed.end() || to == planned.end() ||
            from->second.empty() || to->second.empty()) {
            check.unread.insert(key);
            continue;
        }
        const int order = compareRpmVersions(to->second, from->second);
        if (order == 0) continue;
        check.moved[key] = PlanCheck::VersionMove{from->second, to->second, order < 0};
        if (order < 0) check.changes.downgrade.insert(key);
    }
}

PlanCheck compareRemoval(PlanCheck check, const std::set<std::string>& planned) {
    check.theirs = check.changes.remove;
    for (const auto& name : planned) {
        if (!check.theirs.count(name)) check.onlyOurs.push_back(name);
    }
    for (const auto& name : check.theirs) {
        if (!planned.count(name)) check.onlyTheirs.push_back(name);
    }
    const bool alsoInstalls = !check.changes.install.empty() ||
                              !check.changes.upgrade.empty() ||
                              !check.changes.downgrade.empty() ||
                              !check.changes.replaced.empty() ||
                              !check.changes.unknown.empty();
    check.agreement = (check.onlyOurs.empty() && check.onlyTheirs.empty() && !alsoInstalls)
        ? PlanAgreement::Agrees
        : PlanAgreement::Differs;
    return check;
}

}

PlanCheck checkPlanWithRpmOstree(
    const std::string& requested,
    const std::set<std::string>& approvedKeys
) {
    PlanCheck check;
    if (!commandExists("rpm-ostree") || !isPackageName(requested)) {
        return check;   // Unavailable
    }

    // The same options the real layering uses (layerWithRpmOstree), so
    // the dry run answers the question that will be asked.
    const DryRun run = dryRun(
        "rpm-ostree install --dry-run --idempotent --allow-inactive " +
        shellQuote(requested));
    if (!run.ran) return check;

    check.changes = parseRpmOstreeDryRun(run.output);
    const bool anything = !check.changes.installKeys.empty() ||
                          !check.changes.downgrade.empty() ||
                          !check.changes.remove.empty() ||
                          !check.changes.replaced.empty();
    if (run.code != 0) {
        noPlan(check, run);
        if (anything && check.agreement == PlanAgreement::Unavailable) {
            check.refusal.push_back("rpm-ostree stopped (exit " +
                                    std::to_string(run.code) + ") after listing its plan.");
            check.agreement = PlanAgreement::Refused;
        }
        return check;
    }
    // 0.1.94 (round-5 audit M2): rpm-ostree answers even "nothing to
    // do" with the whole layer (the Asus, 14 Nov: `install --dry-run
    // --idempotent gcc`, already layered, listed 137). An answer with no
    // list in it is one this check cannot read; reading it as "adds
    // nothing" would let an unseen plan through.
    if (!anything) {
        check.refusal.push_back("rpm-ostree answered, but no list of packages could be read "
                                "from its answer; nothing is layered on an unread plan.");
        check.agreement = PlanAgreement::Refused;
        return check;
    }

    // rpm-ostree lists the whole layer again, packages layered earlier
    // included (the Asus, 13 November: 193 names for VLC, most of them
    // already installed). What the running system has is not new.
    // Compared by name AND architecture (round-5 audit M1).
    std::set<std::string> present;
    std::map<std::string, std::string> installedVersions;
    if (commandExists("rpm")) {
        std::set<std::string> names;
        for (const auto& key : check.changes.installKeys) names.insert(stripArchitecture(key));
        // rpm writes "package X is not installed" to stdout too; only
        // keys rpm-ostree listed are kept, so such lines fall away.
        const DryRun query = dryRun("rpm -q --qf '%{NAME}.%{ARCH} %{EVR}\\n'" +
                                    quotedList(std::vector<std::string>(names.begin(), names.end())));
        for (const auto& [key, evr] : keyVersions(query.output)) {
            if (check.changes.installKeys.count(key)) {
                present.insert(key);
                installedVersions[key] = evr;
            }
        }
    }
    for (const auto& key : check.changes.installKeys) {
        (present.count(key) ? check.refreshed : check.theirs).insert(key);
    }
    noteMoves(check, installedVersions, check.changes.versions);

    for (const auto& key : check.theirs) {
        if (!approvedKeys.count(key)) check.onlyTheirs.push_back(key);
    }
    for (const auto& key : approvedKeys) {
        if (!check.theirs.count(key) && !present.count(key)) check.onlyOurs.push_back(key);
    }
    check.agreement = (check.onlyTheirs.empty() && !check.changes.beyondInstalling())
        ? PlanAgreement::Agrees
        : PlanAgreement::Differs;
    return check;
}

PlanCheck checkRemovalWithDnf(
    const std::vector<std::string>& names,
    const std::set<std::string>& planned
) {
    PlanCheck check;
    if (!commandExists("dnf") || names.empty()) return check;
    for (const auto& name : names) {
        if (!isPackageName(name)) return check;
    }
    const DryRun run = dryRun("dnf remove --assumeno" + quotedList(names));
    if (!run.ran) return check;
    check.changes = parseDnfTransaction(run.output);
    if (check.changes.remove.empty()) {
        noPlan(check, run);
        // Asked as an ordinary user, dnf may decline to answer at all.
        // That is a check that did not run, not a refusal of the plan.
        if (needsRootMessage(check.refusal)) {
            check.refusal.clear();
            check.agreement = PlanAgreement::Unavailable;
        }
        return check;
    }
    return compareRemoval(check, planned);
}

namespace {

// Every installed package as packageKey (name, plus the architecture
// when it is not this machine's): glibc.i686 next to glibc is a
// package of its own (round-5 audit M1).
std::map<std::string, std::string> rpmVersions(const std::string& extra) {
    return keyVersions(dryRun("rpm -qa" + extra + " --qf '%{NAME}.%{ARCH} %{EVR}\\n'").output);
}

std::set<std::string> rpmNames(const std::string& extra) {
    std::set<std::string> names;
    for (const auto& entry : rpmVersions(extra)) names.insert(entry.first);
    return names;
}

}

std::set<std::string> installedPackageKeys(bool rpm) {
    if (rpm) return commandExists("rpm") ? rpmNames("") : std::set<std::string>{};
    std::set<std::string> names;
    if (!commandExists("dpkg-query")) return names;
    const DryRun query = dryRun("dpkg-query -W -f '${db:Status-Abbrev} ${Package}\\n'");
    std::istringstream lines(query.output);
    std::string status, name;
    while (lines >> status >> name) {
        if (status == "ii" && isPackageName(name)) names.insert(name);
    }
    return names;
}

InstalledDelta compareInstalled(
    const std::set<std::string>& before,
    const std::set<std::string>& after,
    const std::set<std::string>& confirmed
) {
    InstalledDelta delta;
    for (const auto& key : after) {
        if (!before.count(key)) {
            delta.added.insert(key);
            if (!confirmed.count(key)) delta.unconfirmed.insert(key);
        }
    }
    for (const auto& key : before) {
        if (!after.count(key)) delta.removed.insert(key);
    }
    return delta;
}

PlanCheck checkRemovalWithRpmOstree(
    const std::vector<std::string>& names,
    const std::set<std::string>& planned,
    const std::string& baseDb
) {
    PlanCheck check;
    if (!commandExists("rpm-ostree") || !commandExists("rpm") || names.empty()) return check;
    for (const auto& name : names) {
        if (!isPackageName(name)) return check;
    }

    const DryRun run = dryRun("rpm-ostree uninstall --dry-run" + quotedList(names));
    if (!run.ran) return check;
    if (run.code != 0) {
        noPlan(check, run);
        return check;
    }
    const PackageChanges plan = parseRpmOstreeDryRun(run.output);
    const std::set<std::string>& remaining = plan.installKeys;

    // What the image itself carries, from the copy of its package
    // database rpm-ostree keeps. Without it, nothing tells a layered
    // package from one of the image's: refuse rather than guess.
    std::error_code error;
    const std::set<std::string> base = std::filesystem::is_directory(baseDb, error)
        ? rpmNames(" --dbpath " + shellQuote(baseDb))
        : std::set<std::string>{};
    const std::map<std::string, std::string> installedVersions = rpmVersions("");
    std::set<std::string> installed;
    for (const auto& entry : installedVersions) installed.insert(entry.first);
    if (base.empty() || installed.empty()) {
        check.refusal.push_back("The image's own package list (" + baseDb +
                                ") could not be read, so what the removal takes cannot be worked out.");
        check.agreement = PlanAgreement::Refused;
        return check;
    }

    for (const auto& name : installed) {
        if (base.count(name)) continue;               // the image's own
        if (remaining.count(name)) check.refreshed.insert(name);
        else check.changes.remove.insert(name);       // layered, and going
    }
    for (const auto& name : remaining) {
        if (!installed.count(name)) check.changes.install.insert(name);
    }
    check.changes.upgrade = plan.upgrade;
    check.changes.downgrade = plan.downgrade;
    check.changes.unknown = plan.unknown;
    noteMoves(check, installedVersions, plan.versions);
    if (check.changes.remove.empty()) {
        check.refusal.push_back("rpm-ostree's plan removes nothing that is installed now.");
        check.agreement = PlanAgreement::Refused;
        return check;
    }
    return compareRemoval(check, planned);
}

PlanCheck checkRemovalWithApt(
    const std::vector<std::string>& names,
    const std::set<std::string>& planned
) {
    PlanCheck check;
    if (!commandExists("apt-get") || names.empty()) return check;
    for (const auto& name : names) {
        if (!isPackageName(name)) return check;
    }
    const DryRun run = dryRun("apt-get remove --simulate" + quotedList(names));
    if (!run.ran) return check;
    check.changes = parseAptSimulation(run.output);
    if (check.changes.remove.empty()) {
        noPlan(check, run);
        return check;
    }
    return compareRemoval(check, planned);
}

ApplyDecision decideInstall(
    const std::string& requested,
    const std::set<std::string>& expected,
    const PlanCheck& manager,
    const PlanCheck* layer,
    bool assumeYes
) {
    ApplyDecision decision;

    if (manager.agreement == PlanAgreement::Refused ||
        manager.agreement == PlanAgreement::Unavailable) {
        decision.reason = "the package manager did not confirm this plan";
        return decision;
    }
    // A list that leaves out what was asked for is the package manager
    // meaning something else by that name.
    if (manager.agreement == PlanAgreement::Differs &&
        manager.theirs.count(requested) == 0) {
        decision.reason = "the package manager's plan does not contain " + requested;
        return decision;
    }

    decision.willChange =
        manager.agreement == PlanAgreement::Agrees ? expected : manager.theirs;
    decision.alsoChanges = manager.changes.describeBeyondInstalling();

    if (layer != nullptr) {
        if (layer->agreement == PlanAgreement::Refused ||
            layer->agreement == PlanAgreement::Unavailable) {
            decision.reason = "rpm-ostree did not confirm what it would layer";
            return decision;
        }
        // rpm-ostree does the work, so its list is the one that counts.
        decision.willChange = layer->theirs;
        for (const auto& line : layer->changes.describeBeyondInstalling()) {
            if (std::find(decision.alsoChanges.begin(), decision.alsoChanges.end(), line) ==
                decision.alsoChanges.end()) {
                decision.alsoChanges.push_back(line);
            }
        }
    }

    // Longer than Nexus's own plan: Enter is no (round-5 audit L5).
    for (const auto& key : decision.willChange) {
        if (!expected.count(key)) {
            decision.defaultNo = true;
            break;
        }
    }

    if (!decision.alsoChanges.empty()) {
        if (assumeYes) {
            decision.reason = "it would also remove, replace or downgrade packages, "
                              "which needs a person to read and confirm (run it without -y)";
            return decision;
        }
        decision.typedConfirmation = true;
    }

    decision.allowed = true;
    return decision;
}

ApplyDecision decideRemoval(
    const std::set<std::string>& planned,
    const PlanCheck& check,
    bool assumeYes
) {
    ApplyDecision decision;
    if (check.agreement == PlanAgreement::Refused ||
        check.agreement == PlanAgreement::Unavailable) {
        decision.reason = "the package manager did not confirm what it would remove";
        return decision;
    }
    const PackageChanges& c = check.changes;
    if (!c.install.empty() || !c.upgrade.empty() || !c.downgrade.empty() ||
        !c.replaced.empty() || !c.unknown.empty()) {
        decision.reason = "removing this would also install or change other packages";
        return decision;
    }
    decision.willChange = check.theirs;
    for (const auto& name : check.onlyTheirs) {
        decision.alsoChanges.push_back("remove " + name + " (not in Nexus's plan)");
    }
    if (!decision.alsoChanges.empty() && assumeYes) {
        decision.reason = "the package manager would remove more than Nexus's plan, "
                          "which needs a person to read and confirm (run it without -y)";
        return decision;
    }
    (void)planned;
    decision.allowed = true;
    return decision;
}

}
