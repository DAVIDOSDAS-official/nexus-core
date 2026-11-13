#include <nexus/system/plan_check.hpp>

#include <nexus/system/process.hpp>

#include <algorithm>
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
// being taken away.
void dropSelfReplacements(PackageChanges& changes) {
    for (auto it = changes.replaced.begin(); it != changes.replaced.end();) {
        if (changes.upgrade.count(*it) || changes.downgrade.count(*it) ||
            changes.reinstall.count(*it) || changes.install.count(*it)) {
            it = changes.replaced.erase(it);
        } else {
            ++it;
        }
    }
}

}

std::vector<std::string> PackageChanges::describeBeyondInstalling() const {
    std::vector<std::string> lines;
    for (const auto& name : remove) lines.push_back("remove " + name);
    for (const auto& name : replaced) lines.push_back("replace " + name);
    for (const auto& name : downgrade) lines.push_back("downgrade " + name);
    return lines;
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

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Section headings start at column zero.
        if (line.front() != ' ') {
            section = sectionFor(line, changes);
            continue;
        }
        if (section == nullptr) continue;

        std::istringstream columns(line);
        std::string first;
        if (!(columns >> first) || first.empty()) continue;

        if (first == "replacing" || first == "Replacing") {
            std::string old;
            if (columns >> old) changes.replaced.insert(stripArchitecture(old));
            continue;
        }
        section->insert(first);
    }

    dropSelfReplacements(changes);
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

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line.front() != ' ') {
            // "Installing 193 packages:", "Removing 1 package:" ...
            section = (line.back() == ':' &&
                       line.find(" package") != std::string::npos)
                ? sectionFor(line, changes)
                : nullptr;
            continue;
        }
        if (section == nullptr) continue;

        std::istringstream columns(line);
        std::string nevra;
        if (columns >> nevra) section->insert(packageNameFromNevra(nevra));
    }

    dropSelfReplacements(changes);
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
                              !check.changes.replaced.empty();
    check.agreement = (check.onlyOurs.empty() && check.onlyTheirs.empty() && !alsoInstalls)
        ? PlanAgreement::Agrees
        : PlanAgreement::Differs;
    return check;
}

}

PlanCheck checkPlanWithRpmOstree(
    const std::string& requested,
    const std::set<std::string>& approved
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
    const bool anything = !check.changes.install.empty() ||
                          !check.changes.upgrade.empty() ||
                          !check.changes.downgrade.empty() ||
                          !check.changes.remove.empty() ||
                          !check.changes.replaced.empty();
    if (run.code != 0 && !anything) {
        noPlan(check, run);
        return check;
    }
    if (run.code != 0) {   // a plan, then a failure: not a plan to trust
        noPlan(check, run);
        if (check.agreement == PlanAgreement::Unavailable) {
            check.refusal.push_back("rpm-ostree stopped (exit " +
                                    std::to_string(run.code) + ") after listing its plan.");
            check.agreement = PlanAgreement::Refused;
        }
        return check;
    }

    // rpm-ostree lists the whole layer again, packages layered earlier
    // included (the Asus, 13 November: 193 names for VLC, most of them
    // already installed). What the running system has is not new.
    std::set<std::string> present;
    if (!check.changes.install.empty() && commandExists("rpm")) {
        std::vector<std::string> names(check.changes.install.begin(),
                                       check.changes.install.end());
        const DryRun query = dryRun("rpm -q --qf '%{NAME}\\n'" + quotedList(names));
        std::istringstream lines(query.output);
        std::string name;
        while (std::getline(lines, name)) {
            if (check.changes.install.count(name)) present.insert(name);
        }
    }
    for (const auto& name : check.changes.install) {
        (present.count(name) ? check.refreshed : check.theirs).insert(name);
    }

    for (const auto& name : check.theirs) {
        if (!approved.count(name)) check.onlyTheirs.push_back(name);
    }
    for (const auto& name : approved) {
        if (!check.theirs.count(name) && !present.count(name)) check.onlyOurs.push_back(name);
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
        !c.replaced.empty()) {
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
