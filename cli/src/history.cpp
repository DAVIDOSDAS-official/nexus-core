// nexus history.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

int commandHistory(const std::string& path) {
    // Every log, not the one belonging to whoever asked. Changes
    // are made under sudo and read back without it, so a machine's
    // history lives in two files and belongs to neither.
    const auto records = nexus::system::readAllTransactions();

    // The system itself first: which version is running, and which
    // one the boot menu can go back to. Image updates and switching
    // edition are the biggest changes a machine sees, and they were
    // missing from here entirely.
    const auto deployments = readDeployments();
    std::set<std::string> layeredNow;

    if (!deployments.empty()) {
        std::cout << "System versions\n";
        for (const auto& d : deployments) {
            std::cout << "  " << (d.booted ? "now      " : "previous ")
                      << (d.date.empty() ? std::string("?") : dayOf(d.date))
                      << "  " << (d.version.empty() ? "?" : d.version)
                      << "  " << describeImage(d.image);
            if (!d.booted && !deployments.empty() &&
                deployments.front().booted &&
                d.version == deployments.front().version) {
                std::cout << "  (earlier build, same day)";
            }
            std::cout << "\n";
            if (d.booted) {
                layeredNow.insert(d.layered.begin(), d.layered.end());
            }
        }
        std::cout << "\n";
    }

    if (records.empty()) {
        std::cout
            << "No changes recorded in " << path << ".\n";
        return 0;
    }

    std::cout << "Changes made with nexus\n";
    const std::string booted = nexus::system::bootTimestamp();

    for (const auto& record : records) {
        std::cout
            << record.when << "  " << record.request;

        if (!record.resolved.empty() &&
            record.resolved != record.request) {
            std::cout << " -> " << record.resolved;
        }

        // "staged" means waiting for a reboot. Once the machine has
        // rebooted into a version that carries it, it is applied --
        // the Asus still said "staged" five days and three updates on.
        const bool applied =
            (record.outcome == "staged" &&
             (layeredNow.count(record.request) > 0 ||
              layeredNow.count(record.resolved) > 0)) ||
            nexus::system::updateTookEffect(record, NEXUS_VERSION, booted);

        std::cout
            << "  ["
            << (record.unfinished ? "interrupted"
                : applied        ? "applied"
                                 : record.outcome)
            << "]\n";

        if (!record.packages.empty()) {
            std::string verb = "attempted ";

            if (record.succeeded || applied) {
                verb =
                    record.kind ==
                        nexus::system::TransactionKind::Remove
                        ? "removed "
                        : "added ";
            }

            std::cout
                << "    " << verb << record.packages.size() << ": ";

            std::size_t shown = 0;

            for (const std::string& name : record.packages) {
                if (shown > 0) {
                    std::cout << ", ";
                }

                std::cout << name;

                if (++shown >= 8) {
                    std::cout << ", ...";
                    break;
                }
            }

            std::cout << "\n";
        }
    }

    return 0;
}

}  // namespace nexus_cli
