// `nexus` with no arguments.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// ---------------------------------------------------------------------
// `nexus` with no arguments: the machine in a few lines.
//
// Every line is read from the machine; nothing that cannot be read is
// shown as if it had been. Health is not guessed here -- doctor is
// named for it.
int commandSummary() {
    const std::string pretty = osReleaseValue("PRETTY_NAME");
    const std::string variant = osReleaseValue("VARIANT_ID");

    std::cout << "Nexus " << NEXUS_VERSION;
    if (!pretty.empty()) {
        std::cout << "  -  " << pretty;
    }
    std::cout << "\n\n";

    if (!variant.empty()) {
        std::cout << "  Edition     " << variant << "\n";
    }

    const auto deployments = readDeployments();
    const Deployment* booted = nullptr;
    const Deployment* other = nullptr;

    for (const auto& d : deployments) {
        if (d.booted && booted == nullptr) {
            booted = &d;
        } else if (!d.booted && other == nullptr) {
            other = &d;
        }
    }

    if (booted != nullptr) {
        std::cout << "  Running     " << describeImage(booted->image) << "\n";
        if (!booted->version.empty()) {
            std::cout << "  Built       " << booted->version;
            if (!booted->date.empty()) {
                std::cout << ", " << dayOf(booted->date);
            }
            std::cout << "\n";
        }
        if (other != nullptr) {
            // Two builds on one day share a version string (the Asus,
            // 1 October: 0.1.23 and 0.1.24 were both 44.20261001.0).
            std::string previous =
                other->version.empty() ? "previous version" : other->version;
            if (other->version == booted->version) {
                previous = "an earlier build from the same day";
            }
            std::cout << "  Rollback    ready: " << previous
                      << " (pick it in the boot menu if an update"
                         " misbehaves)\n";
        } else {
            std::cout << "  Rollback    none yet (appears after the"
                         " first update)\n";
        }
        if (!booted->layered.empty()) {
            std::cout << "  Added       " << booted->layered.size()
                      << " package(s) on top of the image\n";
        }
        std::cout << "  Updates     checked daily, never installed by "
                     "themselves: nexus update\n";
    } else {
        std::cout << "  (Not an image-based system, or rpm-ostree did"
                     " not answer.)\n";
    }

    std::cout
        << "\n"
        << "  nexus doctor    is everything healthy?\n"
        << "  nexus setup     add-ons for this machine\n"
        << "  nexus history   what changed, and when\n"
        << "  nexus --help    everything else\n";

    return 0;
}

}  // namespace nexus_cli
