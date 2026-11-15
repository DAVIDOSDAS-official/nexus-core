// nexus install, options and plan.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// ---------------------------------------------------------------------
// For `install`: the facts a person decides on, in their words.
std::string humanBytes(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= 1000ULL * 1000 * 1000) {
        std::snprintf(buf, sizeof buf, "%.1f GB", bytes / 1e9);
    } else if (bytes >= 1000ULL * 1000) {
        std::snprintf(buf, sizeof buf, "%.0f MB", bytes / 1e6);
    } else {
        std::snprintf(buf, sizeof buf, "%.0f kB", bytes / 1e3);
    }
    return buf;
}

// "rpmfusion-free-updates" -> "RPM Fusion (free)". The repository id is
// kept in brackets: it is what a person searching for help will need.
std::string humanRepository(const std::string& id) {
    if (id.rfind("rpmfusion-nonfree", 0) == 0) {
        return "RPM Fusion, nonfree part (" + id + ")";
    }
    if (id.rfind("rpmfusion-free", 0) == 0) {
        return "RPM Fusion (" + id + ")";
    }
    if (id == "fedora" || id.rfind("updates", 0) == 0) {
        return "Fedora (" + id + ")";
    }
    return id;
}

// One package's summary and repository, asked of dnf. Only the facts
// dnf states; anything it does not say stays empty and is not shown.
struct PackageFacts {
    std::string summary;
    std::string repository;
};

PackageFacts dnfFacts(const std::string& name) {
    PackageFacts facts;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) ||
              c == '-' || c == '_' || c == '.' || c == '+')) {
            return facts;
        }
    }
    if (!nexus::system::commandExists("dnf")) {
        return facts;
    }
    const auto info = nexus::system::runCommand(
        "LC_ALL=C dnf -q info --available " + shellQuote(name) + " 2>/dev/null",
        false);
    std::istringstream in(info.text);
    std::string line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        std::string value = line.substr(colon + 1);
        const auto start = value.find_first_not_of(' ');
        value = start == std::string::npos ? "" : value.substr(start);
        if (key == "Summary" && facts.summary.empty()) {
            facts.summary = nexus::system::printable(value);   // repository text (0.1.85)
        } else if ((key == "Repository" || key == "From repository" ||
                    key == "Repo") && facts.repository.empty()) {
            facts.repository = value;
        }
    }
    return facts;
}

}  // namespace

// Whether any package lists (repositories, Flathub) were read this run.
// Without them "no package named X" would be a guess about the archive.
bool gAvailableLoaded = false;

int commandInstall(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture,
    bool useRpm,
    bool apply,
    bool assumeYes,
    bool showCommands,
    const std::string& invocation,
    const std::string& profileDirectory,
    bool explain
) {
    nexus::SolverRequest request;

    request.architecture = architecture;
    request.requirements.push_back(
        aliases.expand(nexus::Requirement(nexus::Constraint(capability)))
    );

    const auto solution = buildSolver(universe).solve(request);

    // Nothing by that name at all (0.1.63): almost always a typo, and
    // the solver's "No combination of components satisfies every
    // requirement" said nothing a person could act on
    // (java-lastest-openjdk on the Asus, 1 November).
    if (solution.status != nexus::SolverStatus::Success &&
        !aliases.knows(capability)) {
        bool named = false;
        std::vector<std::string> names;
        names.reserve(universe.size());
        for (const Component& component : universe) {
            if (component.name() == capability) { named = true; break; }
            for (const auto& provided : component.providedCapabilities()) {
                if (provided.name() == capability) { named = true; break; }
            }
            if (named) break;
            names.push_back(component.name());
        }
        if (!named && !gAvailableLoaded) {
            std::cout << "Nothing installed is called " << capability
                      << ", and the package lists are not downloaded,\n"
                         "so Nexus cannot see what is available.\n\n";
            if (useRpm) {
                std::cout << "    sudo nexus install " << capability
                          << "      (fetches them first; installs nothing"
                             " without --apply)\n";
            } else {
                std::cout << "    sudo apt update      then again: nexus install "
                          << capability << "\n";
            }
            return 1;
        }
        if (!named) {
            std::cout << "No package named " << capability << ".\n";
            const auto near =
                nexus::system::spellingSuggestions(names, capability);
            if (!near.empty()) {
                std::cout << "Did you mean " << near.front() << "?\n";
                for (std::size_t i = 1; i < near.size(); ++i) {
                    std::cout << "        or " << near[i] << "?\n";
                }
                std::cout << "\n    nexus install " << near.front() << "\n";
            } else {
                std::cout << "Search for it:\n\n    nexus options " << capability
                          << "\n    flatpak search " << capability << "\n";
            }
            return 1;
        }
    }

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout
            << "Cannot be resolved: " << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            // blockedOn may be a capability nothing provides or a
            // rule that was violated. "Nothing available provides"
            // fits only the first.
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::set<std::string> here;

    for (const Component& component : installed) {
        here.insert(component.name() + ":" + component.architecture());
    }

    std::set<std::string> expected;
    std::string requested;
    std::uint64_t download = 0;
    std::uint64_t onDisk = 0;
    bool sizesKnown = true;

    for (const std::string& id : solution.selected) {
        for (const Component& component : universe) {
            if (component.id() != id) {
                continue;
            }

            if (requested.empty()) {
                requested = component.name();
            }

            if (here.count(component.name() + ":" +
                           component.architecture()) == 0) {
                expected.insert(component.name());
                if (component.downloadSize() == 0) {
                    sizesKnown = false;
                }
                download += component.downloadSize();
                onDisk += component.installedSize();
            }

            break;
        }
    }

    // What a person decides on, first: what it is, where it comes
    // from, how big it is, and what happens to the machine. The
    // comparison with dnf follows, in one line unless asked for.
    // (Before 1 October this printed "Request / Resolves / New" and,
    // for vlc, "The plans differ" with two lists -- accurate, and
    // meaningless to anyone not writing Nexus.)
    // Setup calls this once per item and passes no profile directory:
    // there the add-on is already the context, and a dnf query per
    // item would only slow it down.
    const bool standalone = !profileDirectory.empty();
    const PackageFacts facts =
        useRpm && standalone ? dnfFacts(requested) : PackageFacts{};

    std::cout << requested;
    if (!facts.summary.empty()) {
        std::cout << "  -  " << facts.summary;
    }
    std::cout << "\n";
    if (requested != capability) {
        std::cout << "  Asked for   " << capability << "\n";
    }
    if (!facts.repository.empty()) {
        std::cout << "  From        " << humanRepository(facts.repository)
                  << "\n";
    }
    if (expected.empty()) {
        std::cout << "  Already installed.\n";
        return 0;
    }
    std::cout << "  New         " << expected.size()
              << " package(s), counting what it needs\n";
    if (download > 0) {
        std::cout << "  Download    " << (sizesKnown ? "" : "at least ")
                  << humanBytes(download) << "\n";
    }
    if (onDisk > 0) {
        std::cout << "  On disk     about " << humanBytes(onDisk) << "\n";
    }

    // Add-ons that include this, and how they get it: the media
    // add-on takes VLC from Flathub, and installing the rpm by name
    // without being told so is how one machine ends up with two.
    if (standalone) {
        const auto loaded = nexus::system::parseProfileDirectory(
            profileDirectory);
        for (const nexus::Profile& profile : loaded.profiles) {
            if (profile.name == "base") {
                continue;
            }
            bool includes = false;
            for (const auto& requirement : profile.requirements) {
                for (const auto& alternative : requirement.alternatives) {
                    if (alternative.capability == capability) {
                        includes = true;
                    }
                }
            }
            if (!includes) {
                continue;
            }
            std::cout << "  Add-on      part of " << profile.name;
            const auto flat = profile.flatpak.find(capability);
            if (flat != profile.flatpak.end()) {
                std::cout << ", which uses the Flathub version ("
                          << flat->second << ").\n"
                          << "              For that one instead: "
                             "sudo nexus setup " << profile.name
                          << " --apply\n";
            } else {
                std::cout << "\n";
            }
        }
    }

    if (nexus::system::commandExists("rpm-ostree") &&
        std::filesystem::exists("/run/ostree-booted")) {
        std::cout << "  Restart     needed: added packages take effect "
                     "the next time the machine starts\n";
    }

    // Check the plan against the package manager's own engine, without
    // changing anything.
    //
    // Resolution being correct and installation succeeding are
    // different claims. Nexus can prove the first from metadata; only
    // the thing that actually installs packages can speak to the
    // second. Every other correctness question in this project was
    // settled by comparing against a native tool rather than by another
    // assertion. This is that, applied to the one part metadata cannot
    // answer. (0.1.98: moved here from the top of the file, where it
    // sat above unrelated helpers.)
    //
    // The interlock is the same either way; only the tool asked
    // differs.
    const auto check = useRpm
        ? nexus::system::checkPlanWithDnf(requested, expected)
        : nexus::system::checkPlanWithApt(requested, expected);

    const std::string manager = useRpm ? "dnf" : "apt";

    switch (check.agreement) {
        case nexus::system::PlanAgreement::Agrees:
            std::cout << "  Checked     " << manager
                      << " agrees with this plan\n";
            break;

        case nexus::system::PlanAgreement::Unavailable:
            std::cout << "  Checked     " << manager
                      << " could not be asked; the plan is unchecked\n";
            break;

        case nexus::system::PlanAgreement::Refused:
            std::cout << "\n" << manager << " refuses this plan:\n";
            for (const std::string& message : check.refusal) {
                std::cout << "    " << message << "\n";
            }
            std::cout << "\nNexus works from repository metadata, which"
                         " does not describe\nholds, pins or repository"
                         " rules. " << manager << " knows those.\n";
            break;

        case nexus::system::PlanAgreement::Differs: {
            // Not a fault: dnf applies rules Nexus does not model, and
            // dnf's list is the one used. Said in a line; the lists
            // are there for anyone who asks with --explain.
            const long extra = static_cast<long>(check.theirs.size()) -
                               static_cast<long>(expected.size());
            std::cout << "  Checked     " << manager << " agrees, with "
                      << (extra >= 0 ? std::to_string(extra) + " more"
                                     : std::to_string(-extra) + " fewer")
                      << " package(s) by its own rules;\n"
                         "              its list is the one used"
                         " (--explain shows both)\n";

            if (explain) {
                const auto show =
                    [](const std::string& title,
                       const std::vector<std::string>& names) {
                        if (names.empty()) {
                            return;
                        }
                        std::cout << "\n" << title << ":\n";
                        for (const std::string& name : names) {
                            std::cout << "    " << name << "\n";
                        }
                    };
                show("Nexus expects, " + manager + " does not",
                     check.onlyOurs);
                show(manager + " expects, Nexus does not",
                     check.onlyTheirs);
            }
            break;
        }
    }

    // Removals, replacements and downgrades are never only in
    // --explain: nobody approving "install X" expects them (0.1.91,
    // round-4 audit M2).
    if (check.changes.beyondInstalling()) {
        std::cout << "  Also        " << manager << " would also:\n";
        for (const std::string& line : check.changes.describeBeyondInstalling()) {
            std::cout << "                " << line << "\n";
        }
    }

    // On the image, rpm-ostree does the work with its own resolver,
    // and takes weak dependencies dnf's check leaves out. Its list is
    // the one that will run, so it is the one asked for and shown
    // (0.1.91, round-4 audit M3).
    const bool layering = useRpm && nexus::system::isImageBased();
    nexus::system::PlanCheck layer;

    if (layering && (check.agreement == nexus::system::PlanAgreement::Agrees ||
                     check.agreement == nexus::system::PlanAgreement::Differs)) {
        // dnf's own rows, by name and architecture (round-5 audit M1):
        // what was checked is what rpm-ostree's list is held against.
        layer = nexus::system::checkPlanWithRpmOstree(requested, check.changes.installKeys);

        switch (layer.agreement) {
            case nexus::system::PlanAgreement::Agrees:
                std::cout << "  Layered     rpm-ostree adds the same "
                          << layer.theirs.size() << " package(s)";
                break;
            case nexus::system::PlanAgreement::Differs:
                std::cout << "  Layered     rpm-ostree would add "
                          << layer.theirs.size() << " package(s), "
                          << layer.onlyTheirs.size()
                          << " not in dnf's list (weak dependencies\n"
                             "              and its own rules); its list is the one used:\n";
                for (const std::string& name : layer.onlyTheirs) {
                    std::cout << "                + " << name << "\n";
                }
                break;
            case nexus::system::PlanAgreement::Refused:
                std::cout << "\nrpm-ostree refuses this plan:\n";
                for (const std::string& message : layer.refusal) {
                    std::cout << "    " << message << "\n";
                }
                break;
            case nexus::system::PlanAgreement::Unavailable:
                std::cout << "  Layered     rpm-ostree could not be asked; "
                             "the plan cannot be applied\n";
                break;
        }
        if (layer.agreement == nexus::system::PlanAgreement::Agrees) {
            std::cout << "\n";
        }
        if (layer.agreement == nexus::system::PlanAgreement::Agrees ||
            layer.agreement == nexus::system::PlanAgreement::Differs) {
            printLayerMoves(layer, "              ");
        }
        if (layer.changes.beyondInstalling()) {
            std::cout << "  Also        rpm-ostree would also:\n";
            for (const std::string& line : layer.changes.describeBeyondInstalling()) {
                std::cout << "                " << line << "\n";
            }
        }
    }

    if (showCommands) {
        // The commands, not the outcome.
        //
        // The appeal of assembling a system by hand is not the
        // typing, it is knowing what happened. A tool that resolves
        // and explains and then hands over the exact commands gives
        // that without hiding anything -- and without pretending the
        // base is something it is not.
        std::cout
            << "\nWhat this would run:\n\n"
            << "    sudo " << (useRpm ? "dnf" : "apt-get")
            << " install "
            << (useRpm
                    ? "--setopt=install_weak_deps=False "
                    : "--no-install-recommends ")
            << requested << "\n"
            << "\nRun it yourself, or use --apply to have Nexus do "
            << "it.\n";

        return 0;
    }

    if (!apply) {
        std::cout
            << "\nNothing has been changed. To install it:\n"
            << "    sudo " << invocation << " --apply\n";

        return check.agreement == nexus::system::PlanAgreement::Agrees
            ? 0
            : 1;
    }

    // The interlock. A plan the package manager refuses, or could not
    // be asked about, is not applied, whatever Nexus thinks of it.
    //
    // A plan that merely differs is. What gets handed over is one
    // name -- `requested` -- and the package manager resolves it
    // itself; Nexus's list of dependencies is a preview, never the
    // thing installed. So a difference means the preview was wrong,
    // not that the install is unsafe, and the package manager has
    // just said it would do it.
    //
    // It used to refuse here too. Steam on 25 September: about a
    // hundred 32-bit packages, dnf's set a little different from
    // Nexus's -- Nexus cannot yet read rpm's rich dependencies -- and
    // the gaming profile could not install the one thing it is for.
    // The difference is still shown above, and when somebody is asked
    // to confirm, they confirm the package manager's list: that is the
    // one that will happen.
    //
    // Only when the package manager's own list contains what was asked
    // for. A plan that differs by leaving out the requested package
    // itself is not a preview that was slightly off; it is the package
    // manager meaning something else by that name.
    //
    // 0.1.91: the rule lives in decideInstall (plan_check), with the
    // two additions of the round-4 audit: on the image, rpm-ostree's
    // own list is the one confirmed; and a plan that also removes,
    // replaces or downgrades needs a typed "yes", and is refused
    // under -y.
    const auto decision = nexus::system::decideInstall(
        requested, expected, check, layering ? &layer : nullptr, assumeYes);

    if (!decision.allowed) {
        std::cout
            << "\nRefusing to apply: " << decision.reason << ".\n"
            << "Nothing has been changed.\n";

        return 1;
    }

    const bool differs =
        check.agreement == nexus::system::PlanAgreement::Differs;

    if (differs || layering) {
        std::cout
            << "\n" << (layering ? std::string("rpm-ostree") : manager)
            << " agrees to install " << requested
            << "; its list is the one that\nwill be used, not Nexus's.\n";
    }

    if (!nexus::system::haveRootPrivileges()) {
        // The original arguments, not a reconstruction. A suggested
        // command that drops the flags which made it work is worse
        // than no suggestion.
        std::cout
            << "\nInstalling needs root. Re-run with sudo:\n"
            << "    sudo " << invocation << "\n"
            << "\nNothing has been changed.\n";

        return 1;
    }

    // The plan is shown before anything happens, and the person says
    // yes to that plan rather than to a question with no content.
    if (!assumeYes) {
        // The list of the tool that will run, every name of it: what
        // is confirmed is what happens (0.1.91; it used to stop at 20).
        const std::set<std::string>& willInstall = decision.willChange;

        std::cout << "\nWould install " << willInstall.size() << " package(s):\n";

        for (const std::string& name : willInstall) {
            std::cout << "    " << name << "\n";
        }

        if (!decision.alsoChanges.empty()) {
            std::cout << "\nAnd would also:\n";
            for (const std::string& line : decision.alsoChanges) {
                std::cout << "    " << line << "\n";
            }
        }

        std::string answer;

        if (decision.typedConfirmation) {
            std::cout << "\nThis changes more than it adds. Type 'yes' to confirm: ";
            std::getline(std::cin, answer);
            if (answer != "yes") {
                std::cout << "Nothing has been changed.\n";
                return 1;
            }
        } else if (decision.defaultNo) {
            // More than Nexus's own plan is on that list (round-5 audit
            // L5): pressing Enter is not reading it.
            std::cout << "\nThis list is longer than Nexus's own plan. Proceed? [y/N] ";
            std::getline(std::cin, answer);
            if (!(answer == "y" || answer == "Y" || answer == "yes")) {
                std::cout << "Nothing has been changed.\n";
                return 1;
            }
        } else {
            std::cout << "\nProceed? [Y/n] ";

            std::getline(std::cin, answer);

            // Enter is yes: the plan is on the screen, it is Nexus's
            // own, and the command was run with --apply, so the person
            // has asked twice already.
            if (!(answer.empty() || answer == "y" || answer == "Y" ||
                  answer == "yes")) {
                std::cout << "Nothing has been changed.\n";
                return 1;
            }
        }
    } else if (layering && !layer.onlyTheirs.empty()) {
        // -y: no prompt, but what was added beyond dnf's list is on the
        // screen above and in the record below.
        std::cout << "\n-y given: going ahead with rpm-ostree's list of "
                  << decision.willChange.size() << " package(s).\n";
    }

    // Written before the work starts. An interrupted process does
    // not get to run its cleanup, so a record that only exists
    // afterwards is a record that a power cut erases -- along with
    // any sign that the machine was changed.
    nexus::system::TransactionRecord record;

    record.when = nexus::system::currentTimestamp();
    record.request = capability;
    record.resolved = requested;
    record.packages = decision.willChange;   // what was confirmed

    const std::string log = nexus::system::defaultTransactionLog();

    const std::string marker =
        nexus::system::beginTransaction(log, record);

    // Whether a change made here will still be here after a reboot.
    //
    // An image-based system mounts /usr read-only and is updated by
    // replacing the whole image. bootc usr-overlay makes it writable
    // until the next boot, which is meant for debugging -- so an
    // install can succeed, report success, and silently undo itself.
    //
    // That is worse than refusing. Everything else in this tool is
    // built on saying what actually happened.
    bool useRpmOstree = false;

    if (nexus::system::commandExists("bootc")) {
        // One answer to "is this the image?" for the whole command
        // (0.1.92): this block and the rpm-ostree check above asked it
        // in two different ways, and on the Asus they disagreed.
        const bool writable = !nexus::system::isImageBased();

        if (!writable) {
            std::cerr
                << "\nThis is an image-based system, so this will "
                << "be layered into a new\ndeployment rather than "
                << "installed into the running one.\n\n"
                << "Nothing changes until the machine reboots. The "
                << "current deployment\nstays bootable, so if the "
                << "new one is worse you can go back to it.\n";

            useRpmOstree = true;
        }

        const auto overlay = nexus::system::runCommand(
            "findmnt -no SOURCE /usr 2>/dev/null", false);

        if (overlay.text.find("overlay") != std::string::npos) {
            std::cout
                << "\nWarning: /usr is writable only because "
                << "development mode is on.\n"
                << "Anything installed now disappears at the next "
                << "reboot.\n";

            if (!assumeYes) {
                std::cout << "\nContinue anyway? [y/N] ";

                std::string answer;
                std::getline(std::cin, answer);

                if (answer != "y" && answer != "Y") {
                    std::cout << "\nNothing has been changed.\n";
                    return 0;
                }
            }
        }
    }

    // The plan was checked with rpm-ostree; it is the one to run it.
    useRpmOstree = useRpmOstree || layering;

    // Named for what actually runs: on an image-based system dnf only
    // checked the plan, and rpm-ostree carries it out.
    std::cout << "\nHanding the plan to "
              << (useRpmOstree ? std::string("rpm-ostree") : manager)
              << ".\n\n";

    // Three appliers, one shape: verify, hand over, record what
    // happened. An image-based system layers into a new deployment
    // rather than changing the running one, so what it records is
    // "staged" -- true until the machine reboots, and false to call
    // it anything else.
    // What is installed before, to hold the result against what was
    // confirmed (0.1.94, round-5 audit M3). Layering is staged: the
    // running system does not change, so there is nothing to compare
    // until the next start, and the record says so.
    const std::set<std::string> before = useRpmOstree
        ? std::set<std::string>{}
        : nexus::system::installedPackageKeys(useRpm);

    const auto applied =
        useRpmOstree
            ? nexus::system::layerWithRpmOstree({requested})
            : useRpm ? nexus::system::applyWithDnf(requested)
                     : nexus::system::applyWithApt(requested);

    for (const std::string& line : applied.output) {
        std::cout << line << "\n";
    }

    // A tool that changes a system owes an account of what it did.
    record.outcome = toString(applied.outcome);
    record.exitCode = applied.exitCode;
    record.succeeded =
        applied.outcome == nexus::system::ApplyOutcome::Applied;

    bool differsFromConfirmed = false;
    if (applied.outcome == nexus::system::ApplyOutcome::Applied && !before.empty()) {
        // dnf's rows by name and architecture; apt's by name.
        std::set<std::string> confirmed = decision.willChange;
        confirmed.insert(check.changes.installKeys.begin(), check.changes.installKeys.end());
        const auto delta = nexus::system::compareInstalled(
            before, nexus::system::installedPackageKeys(useRpm), confirmed);

        // The record holds what happened, not what was asked.
        record.packages = delta.added;
        if (!delta.unconfirmed.empty() || !delta.removed.empty()) {
            differsFromConfirmed = true;
            record.outcome += ", not as confirmed";
            std::cout << "\nWhat " << manager << " did is not exactly what was confirmed:\n";
            for (const auto& key : delta.unconfirmed) std::cout << "    + " << key << "  (not in the confirmed list)\n";
            for (const auto& key : delta.removed) std::cout << "    - " << key << "  (removed)\n";
            std::cout << "This is in the history (nexus history).\n";
        }
    } else if (applied.outcome == nexus::system::ApplyOutcome::Staged) {
        record.outcome += " (rpm-ostree's confirmed list; not compared with the machine "
                          "until it starts the new deployment)";
    }

    if (!nexus::system::finishTransaction(log, marker, record)) {
        std::cerr
            << "\nWarning: could not write the transaction record to "
            << log << ".\n";
    }

    switch (applied.outcome) {
        case nexus::system::ApplyOutcome::Applied:
            std::cout << (differsFromConfirmed ? "\nDone, with the differences above.\n"
                                               : "\nDone: what was installed is what was confirmed.\n");
            return 0;

        case nexus::system::ApplyOutcome::Staged:
            // Not done. Written, and waiting for a reboot -- saying
            // "done" here would be false for as long as the machine
            // stays up, which on a laptop is most of the time.
            std::cout
                << "\nStaged. None of it is in effect yet.\n\n"
                << "    systemctl reboot         boot into it\n"
                << "    rpm-ostree status        what is waiting\n"
                << "    rpm-ostree rollback      after rebooting, if "
                << "it is worse\n\n"
                << "The deployment you are running now stays "
                << "bootable.\n";
            return 0;

        case nexus::system::ApplyOutcome::NeedsRoot:
            std::cout << "\nInstalling needs root.\n";
            return 1;

        case nexus::system::ApplyOutcome::Unavailable:
            std::cout
                << "\n" << manager << " is not available here.\n";
            return 1;

        default:
            std::cout
                << "\n" << manager << " did not complete (exit "
                << applied.exitCode << ").\n"
                << "It manages its own recovery; the messages above "
                << "are its own.\n";
            return 1;
    }
}

int commandOptions(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture,
    const std::optional<nexus::Source>& only
) {
    const nexus::ConflictDetector detector(versionComparator());

    const auto report = nexus::findOptions(
        capability, universe, installed,
        buildSolver(universe), detector, architecture, aliases, only);

    std::cout << "Capability:  " << capability << "\n";

    if (report.options.empty()) {
        std::cout << "\nNothing available provides it";

        if (only.has_value()) {
            std::cout << " from " << toString(*only);
        }

        std::cout
            << ".\nTry --with-available or --with-flatpak to widen "
            << "the search.\n";

        return 1;
    }

    std::cout
        << "Options:     " << report.options.size() << "\n\n";

    for (const nexus::Option& option : report.options) {
        std::cout << "  " << option.component;

        if (!option.version.empty()) {
            std::cout << "  " << option.version;
        }

        if (option.installed) {
            std::cout << "   [installed]";
        }

        // Only worth saying when it is not the ordinary case.
        if (option.source != nexus::Source::Base) {
            std::cout << "   (" << toString(option.source) << ")";
        }

        std::cout << "\n";

        if (option.source != nexus::Source::Base) {
            std::cout
                << "      " << describe(option.source) << "\n";
        }

        if (!option.workable) {
            std::cout
                << "      cannot be used: " << option.blockedOn
                << "\n";
        } else if (option.installed) {
            std::cout
                << "      " << option.componentCount
                << " components, already here\n";
        } else {
            std::cout
                << "      " << option.componentCount
                << " components, " << option.wouldAdd
                << " of them new";

            // Counts and bytes are different questions, and one
            // Flatpak is not cheaper than forty packages just
            // because it is one thing. Both are stated; neither is
            // folded into the other.
            if (option.sizeKnown) {
                std::cout << " (";

                if (option.downloadBytes > 0) {
                    std::cout
                        << nexus::system::formatSize(
                               option.downloadBytes)
                        << " to fetch";
                }

                if (option.downloadBytes > 0 &&
                    option.installBytes > 0) {
                    std::cout << ", ";
                }

                if (option.installBytes > 0) {
                    std::cout
                        << nexus::system::formatSize(
                               option.installBytes)
                        << " on disk";
                }

                std::cout << ")";
            }

            std::cout << "\n";
        }

        if (!option.conflictsWith.empty()) {
            std::cout << "      collides with ";

            for (std::size_t index = 0;
                 index < option.conflictsWith.size() && index < 3;
                 ++index) {

                if (index > 0) {
                    std::cout << ", ";
                }

                std::cout << option.conflictsWith[index];
            }

            std::cout << "\n";
        }
    }

    if (!report.anyInstalled()) {
        std::cout
            << "\nNothing providing this is installed yet.\n";
    }

    std::cout
        << "\nNothing has been changed; this command only reports.\n";

    return 0;
}

int commandPlan(
    const std::vector<Component>& universe,
    const std::string& capability,
    const std::string& architecture,
    bool explain
) {
    nexus::Solver solver = buildSolver(universe);

    nexus::SolverRequest request;

    request.architecture = architecture;
    request.requirements.push_back(
        nexus::Requirement(nexus::Constraint(capability))
    );

    const auto solution = solver.solve(request);

    std::cout
        << "Request:  " << capability << "\n"
        << "Resolve:  " << toString(solution.status) << "\n";

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout << "\n" << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            // blockedOn may be a capability nothing provides or a
            // rule that was violated. "Nothing available provides"
            // fits only the first.
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    const auto plan = nexus::planTransaction(
        solution.selected,
        universe,
        buildDetector()
    );

    std::cout
        << "Order:    " << toString(plan.status) << "\n"
        << "Steps:    " << plan.steps.size() << "\n\n"
        << plan.reason << "\n";

    if (!plan.cycles.empty()) {
        std::cout << "\nDependency cycles:\n";

        for (const nexus::TransactionCycle& cycle : plan.cycles) {
            std::cout << "    ";

            for (std::size_t i = 0; i < cycle.members.size(); ++i) {
                if (i > 0) {
                    std::cout << " <-> ";
                }

                std::cout << cycle.members[i];
            }

            if (cycle.containsPreDependency) {
                std::cout << "   [contains a pre-dependency]";
            }

            std::cout << "\n";
        }
    }

    if (!plan.ready()) {
        return 1;
    }

    if (!explain) {
        std::cout
            << "\nUse --explain for the full order.\n"
            << "Nothing has been changed; this command only "
            << "reports.\n";

        return 0;
    }

    std::cout << "\nOrder:\n";

    std::size_t number = 0;

    for (const nexus::TransactionStep& step : plan.steps) {
        std::cout
            << "    " << ++number << ". " << step.component;

        if (step.inCycle) {
            std::cout << "   [cycle: unpack first, configure after]";
        } else if (!step.after.empty()) {
            std::cout << "   after ";

            for (std::size_t i = 0; i < step.after.size(); ++i) {
                if (i > 0) {
                    std::cout << ", ";
                }

                std::cout << step.after[i];
            }
        }

        std::cout << "\n";
    }

    std::cout
        << "\nNothing has been changed; this command only reports.\n";

    return 0;
}

}  // namespace nexus_cli
