// nexus remove.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

int commandRemove(
    const std::vector<Component>& installed,
    const std::string& target,
    const std::string& statesPath,
    const std::string& architecture,
    const std::set<std::string>& protectedIds,
    bool apply,
    bool assumeYes,
    bool useRpm,
    bool showCommands,
    const std::string& invocation
) {
    // apt keeps a record of what came along automatically; on an rpm
    // system there is none to read here (0.1.92: the Asus was warned
    // about /var/lib/apt). Every package then counts as wanted, and
    // what dnf would also take is shown by its own check below.
    const auto automatic = useRpm
        ? std::set<std::string>{}
        : nexus::system::readAutoInstalled(statesPath);

    if (automatic.empty() && !useRpm) {
        std::cerr
            << "Warning: no auto-installed record found at "
            << statesPath << ".\n"
            << "Every package looks explicitly wanted, so nothing "
            << "will appear removable.\n\n";
    }

    // Roots are what was asked for: installed, minus what apt says
    // came along automatically.
    std::set<std::string> roots;

    for (const Component& component : installed) {
        const std::string key =
            component.name() + ":" + component.architecture();

        if (automatic.count(key) == 0) {
            roots.insert(component.id());
        }
    }

    const auto plan = nexus::planRemoval(
        target,
        installed,
        roots,
        buildDetector(),
        architecture,
        protectedIds);

    std::cout
        << "Target:     " << plan.target << "\n"
        << "Roots:      " << roots.size()
        << " explicitly wanted component(s)\n\n";

    if (!plan.possible) {
        std::cout << plan.reason << "\n";

        if (!plan.requiredBy.empty()) {
            std::cout << "\nStill required by:\n";

            std::size_t shown = 0;

            for (const std::string& id : plan.requiredBy) {
                std::cout << "    " << id << "\n";

                if (++shown >= 10) {
                    std::cout
                        << "    ... and "
                        << (plan.requiredBy.size() - shown)
                        << " more\n";
                    break;
                }
            }
        }

        return 1;
    }

    std::cout
        << "Would remove " << plan.removed.size()
        << " component(s):\n\n"
        << "    " << plan.target << "  (requested)\n";

    for (const std::string& id : plan.orphaned) {
        std::cout << "    " << id << "  (nothing else needs it)\n";
    }

    if (showCommands) {
        std::cout << "\nWhat this would run:\n\n    sudo "
                  << (useRpm ? "dnf" : "apt-get") << " remove";

        for (const std::string& name : plan.removed) {
            std::cout << " " << name;
        }

        std::cout
            << "\n\nRun it yourself, or use --apply to have Nexus "
            << "do it.\n";

        return 0;
    }

    // What the package manager itself would remove (0.1.91, round-4
    // audit M1). `dnf remove` also takes everything that depends on a
    // target and the dependencies left unused, and Nexus's own plan
    // cannot see rpm's rich dependencies; so the count confirmed below
    // is the package manager's, and a removal it would not confirm is
    // not carried out.
    const std::set<std::string> planned(plan.removed.begin(), plan.removed.end());
    // On the image dnf cannot remove anything; rpm-ostree takes layered
    // packages off (0.1.93). Its own plan is the one checked and shown.
    const bool imageBased = useRpm && nexus::system::isImageBased();
    const auto removal = imageBased
        ? nexus::system::checkRemovalWithRpmOstree(plan.removed, planned)
        : useRpm
        ? nexus::system::checkRemovalWithDnf(plan.removed, planned)
        : nexus::system::checkRemovalWithApt(plan.removed, planned);
    const std::string tool = imageBased ? "rpm-ostree" : useRpm ? "dnf" : "apt";

    switch (removal.agreement) {
        case nexus::system::PlanAgreement::Agrees:
            std::cout << "\nChecked:   " << tool << " would remove the same "
                      << removal.theirs.size() << " package(s).\n";
            break;
        case nexus::system::PlanAgreement::Differs:
            std::cout << "\nChecked:   " << tool << " would remove "
                      << removal.theirs.size() << " package(s); its list is the one used.\n";
            if (!removal.onlyTheirs.empty()) {
                std::cout << "\nAlso removed by " << tool << " (not in the plan above):\n";
                for (const std::string& name : removal.onlyTheirs) {
                    std::cout << "    " << name << "\n";
                }
            }
            if (!removal.onlyOurs.empty()) {
                std::cout << "\nIn the plan above, but " << tool << " would not remove:\n";
                for (const std::string& name : removal.onlyOurs) {
                    std::cout << "    " << name << "\n";
                }
            }
            break;
        case nexus::system::PlanAgreement::Refused:
            std::cout << "\n" << tool << " refuses this removal:\n";
            for (const std::string& message : removal.refusal) {
                std::cout << "    " << message << "\n";
            }
            if (imageBased) {
                std::cout << "\nOn the image, only packages you added (layered) can be"
                             " removed, by\nthe name you added them with:"
                             " rpm-ostree status lists them.\n";
            }
            break;
        case nexus::system::PlanAgreement::Unavailable:
            std::cout << "\nChecked:   " << tool << " could not be asked"
                      << (nexus::system::haveRootPrivileges()
                              ? "; the removal cannot be applied.\n"
                              : " without root; with sudo it is asked first.\n");
            break;
    }

    if (imageBased && removal.agreement != nexus::system::PlanAgreement::Refused) {
        printLayerMoves(removal, "  ");
    }

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to remove, "
            << "or\n--commands to see what it would run.\n";

        return 0;
    }

    if (!nexus::system::haveRootPrivileges()) {
        std::cout
            << "\nRemoving needs root. Re-run with sudo:\n"
            << "    sudo " << invocation << "\n"
            << "\nNothing has been changed.\n";

        return 1;
    }

    const auto decision = nexus::system::decideRemoval(planned, removal, assumeYes);

    if (!decision.allowed) {
        std::cout << "\nRefusing to remove: " << decision.reason << ".\n"
                  << "Nothing has been changed.\n";
        return 1;
    }

    // Removing is the dangerous direction, so the confirmation is
    // stricter than for installing: the count is stated and the whole
    // word must be typed. A y/N prompt is answered by reflex. The count
    // is the package manager's (0.1.91).
    if (!assumeYes) {
        std::cout
            << "\nThis will remove " << decision.willChange.size()
            << " package(s). Type 'remove' to confirm: ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "remove") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    nexus::system::TransactionRecord record;

    record.kind = nexus::system::TransactionKind::Remove;
    record.when = nexus::system::currentTimestamp();
    record.request = "remove " + target;
    record.resolved = plan.target;
    record.packages = decision.willChange;   // what was confirmed

    const std::string log = nexus::system::defaultTransactionLog();

    const std::string marker =
        nexus::system::beginTransaction(log, record);

    std::cout
        << "\nHanding the plan to " << tool << ".\n\n";

    const auto removed = imageBased
        ? nexus::system::uninstallWithRpmOstree(plan.removed)
        : useRpm
        ? nexus::system::removeWithDnf(plan.removed)
        : nexus::system::removeWithApt(plan.removed);

    for (const std::string& line : removed.output) {
        std::cout << line << "\n";
    }

    record.outcome = toString(removed.outcome);
    record.exitCode = removed.exitCode;
    record.succeeded =
        removed.outcome == nexus::system::ApplyOutcome::Applied ||
        removed.outcome == nexus::system::ApplyOutcome::Staged;

    if (!nexus::system::finishTransaction(log, marker, record)) {
        std::cerr
            << "\nWarning: could not write the transaction record to "
            << log << ".\n";
    }

    if (removed.outcome == nexus::system::ApplyOutcome::Applied) {
        std::cout << "\nDone.\n";
        return 0;
    }

    if (removed.outcome == nexus::system::ApplyOutcome::Staged) {
        std::cout << "\nDone. It takes effect the next time the machine starts;"
                     " until then\nthe packages are still there. The previous"
                     " state stays in the boot menu.\n";
        return 0;
    }

    std::cout
        << "\n" << tool
        << " did not complete (exit " << removed.exitCode
        << ").\nIt manages its own recovery; the messages above are "
        << "its own.\n";

    return 1;
}

}  // namespace nexus_cli
