// Understanding the system: scan, what-provides, why, inspect, conflicts,
// gaps, largest, services, hardware, secureboot.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

bool provides(const Component& component, const std::string& capability) {
    for (const Capability& provided : component.providedCapabilities()) {
        if (provided.name() == capability) {
            return true;
        }
    }

    return false;
}

bool requires_(const Component& component, const std::string& capability) {
    for (const Capability& required : component.requiredCapabilities()) {
        if (required.name() == capability) {
            return true;
        }
    }

    return false;
}

}  // namespace

int commandScan(const SourceSummary& result) {
    std::cout
        << "Source:      " << result.name << "\n"
        << "Records:     " << result.recordsRead << "\n"
        << "Components:  " << result.components.size() << "\n"
        << "Skipped:     " << result.recordsSkipped << "\n"
        << "\n";

    std::size_t capabilities = 0;

    for (const Component& component : result.components) {
        capabilities += component.providedCapabilities().size();
    }

    std::cout
        << "Capabilities provided: " << capabilities << "\n"
        << "Dependency clauses:    " << result.dependencyClauses << "\n";

    if (result.dependencyClauses > 0) {
        const double share =
            100.0 * static_cast<double>(result.representableClauses) /
            static_cast<double>(result.dependencyClauses);

        std::cout
            << "Representable:         "
            << result.representableClauses
            << " (" << static_cast<int>(share) << "%)\n";
    }

    std::cout << "\nModel gaps:\n";

    if (result.gaps.empty()) {
        std::cout << "    none\n";
    }

    for (const auto& [kind, count] : result.gaps) {
        std::cout << "    " << kind << ": " << count << "\n";
    }

    for (const std::string& note : result.notes) {
        std::cout << "\n" << note << "\n";
    }

    return 0;
}

int commandWhatProvides(
    const std::vector<Component>& components,
    const std::string& capability
) {
    std::vector<std::string> providers;

    for (const Component& component : components) {
        if (provides(component, capability)) {
            providers.push_back(component.id());
        }
    }

    std::cout << "Capability:\n    " << capability << "\n\n";

    if (providers.empty()) {
        std::cout << "No installed component provides this capability.\n";
        return 1;
    }

    std::cout << "Provided by:\n";

    for (const std::string& provider : providers) {
        std::cout << "    " << provider << "\n";
    }

    if (providers.size() > 1) {
        std::cout
            << "\nResolution status:\n"
            << "    Ambiguous - "
            << providers.size()
            << " providers, no preference given.\n";
    }

    return 0;
}

namespace {

enum class PackageOrigin { Image, Added, Unknown };

PackageOrigin packageOrigin(const std::string& name);

}  // namespace

int commandWhy(
    const std::vector<Component>& components,
    const std::string& id
) {
    const Component* target = find(components, id);

    if (target == nullptr) {
        return 1;
    }

    std::cout << "Component:\n    " << id << "\n\n";

    std::vector<std::string> dependents;

    for (const Component& component : components) {
        if (component.id() == id) {
            continue;
        }

        bool matches = requires_(component, id);

        if (!matches) {
            for (const Capability& provided :
                 target->providedCapabilities()) {

                if (requires_(component, provided.name())) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            dependents.push_back(component.id());
        }
    }

    if (dependents.empty()) {
        std::cout << "Nothing installed depends on it.\n";

        // On a Nexus machine every package either came with the image
        // or was added afterwards, and the two are kept differently:
        // an update replaces the first and keeps the second.
        switch (packageOrigin(target->name())) {
            case PackageOrigin::Added:
                std::cout
                    << "You added it (nexus install or rpm-ostree "
                       "install); updates keep it.\n";
                break;
            case PackageOrigin::Image:
                std::cout
                    << "It is part of the Nexus image: it came with the "
                       "system, and updates bring it along.\n";
                break;
            case PackageOrigin::Unknown:
                std::cout
                    << "It was requested directly or is no longer "
                       "needed.\n";
                break;
        }
        return 0;
    }

    std::cout << "Required by:\n";

    for (const std::string& dependent : dependents) {
        std::cout << "    " << dependent << "\n";
    }

    std::cout
        << "\nReason:\n    "
        << dependents.size()
        << " installed component(s) declare a dependency on it.\n";

    return 0;
}

int commandInspect(
    const std::vector<Component>& components,
    const std::string& id
) {
    const Component* component = find(components, id);

    if (component == nullptr) {
        return 1;
    }

    std::cout
        << "ID:        " << component->id() << "\n"
        << "Version:   " << component->version() << "\n"
        << "Type:      " << toString(component->type()) << "\n"
        << "Arch:      " << component->architecture()
        << "  (multi-arch: "
        << nexus::toString(component->multiArch()) << ")\n";

    std::cout << "\nProvides:\n";

    for (const Capability& capability : component->providedCapabilities()) {
        std::cout << "    " << capability.name() << "\n";
    }

    std::cout << "\nRequires:\n";

    if (component->requiredCapabilities().empty()) {
        std::cout << "    (none)\n";
    }

    for (const Capability& capability : component->requiredCapabilities()) {
        std::cout << "    " << capability.name() << "\n";
    }

    return 0;
}

int commandConflicts(const std::vector<Component>& components) {
    const nexus::ConflictDetector detector(versionComparator());

    std::size_t declared = 0;

    for (const Component& component : components) {
        declared += component.conflicts().size();
    }

    std::cout
        << "Components:          " << components.size() << "\n"
        << "Declared conflicts:  " << declared << "\n\n";

    const auto conflicts = detector.detect(components);

    if (conflicts.empty()) {
        std::cout
            << "No active conflicts.\n"
            << "Every declared conflict refers to something that is\n"
            << "either not installed or at a version that is allowed.\n";
        return 0;
    }

    std::cout << "Active conflicts (" << conflicts.size() << "):\n\n";

    for (const nexus::Conflict& conflict : conflicts) {
        std::cout << "    " << conflict.reason << "\n";
    }

    return 1;
}

namespace {

// Whether a package came with the image or was added on this machine.
// Unknown on a system that is not an image (no deployments to read).
PackageOrigin packageOrigin(const std::string& name) {
    const auto deployments = readDeployments();

    for (const Deployment& deployment : deployments) {
        if (!deployment.booted) {
            continue;
        }

        for (const std::string& layered : deployment.layered) {
            if (layered == name) {
                return PackageOrigin::Added;
            }
        }

        return PackageOrigin::Image;
    }

    return PackageOrigin::Unknown;
}

}  // namespace

// What is taking up the room.
//
// A system's size is not evenly spread: on a Fedora base a handful of
// components are most of it, and knowing which ones is the difference
// between "the image is 2.9 GB" and "the image is 2.9 GB because
// firmware for every device on earth is 1.2 GB of it".
//
// Sizes come from the same metadata everything else does, so this is
// a report rather than a measurement -- it says what the packages
// claim, which can differ from what is on disk.
int commandLargest(
    const std::vector<Component>& installed,
    std::size_t howMany,
    bool onlyUnused,
    const std::set<std::string>& unused
) {
    std::vector<const Component*> ordered;

    std::uint64_t total = 0;
    std::size_t unknown = 0;

    for (const Component& component : installed) {
        // Whether to filter is a flag, not something inferred from
        // the set being empty. An empty set means both "no filter
        // asked for" and "nothing is unused", and treating the second
        // as the first showed every component on a system that had no
        // unused ones at all.
        if (onlyUnused && unused.count(component.id()) == 0) {
            continue;
        }

        if (component.installedSize() == 0) {
            // Unknown is not zero, and counting it as zero would
            // understate the total by however much it actually is.
            unknown += 1;
            continue;
        }

        total += component.installedSize();
        ordered.push_back(&component);
    }

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const Component* left, const Component* right) {
            return left->installedSize() > right->installedSize();
        }
    );

    std::cout
        << "Components:  " << ordered.size() << "\n"
        << "Accounted:   "
        << nexus::system::formatSize(total) << "\n";

    if (onlyUnused) {
        // The two halves together are the question somebody actually
        // has about an image: not what is big, and not what is
        // unneeded, but what is both.
        std::cout
            << "Showing only what nothing needs.\n";

        if (ordered.empty()) {
            std::cout
                << "\nNothing is both installed and unneeded.\n";
        }
    }

    if (unknown > 0) {
        std::cout
            << "Unknown:     " << unknown
            << " component(s) report no size\n";
    }

    std::cout << "\n";

    std::uint64_t shown = 0;
    std::size_t count = 0;

    for (const Component* component : ordered) {
        if (count >= howMany) {
            break;
        }

        const double share =
            total == 0
                ? 0.0
                : 100.0 * static_cast<double>(
                      component->installedSize()) /
                  static_cast<double>(total);

        std::string size = nexus::system::formatSize(
            component->installedSize());

        while (size.size() < 8) {
            size = " " + size;
        }

        std::printf(
            "  %s  %5.1f%%  %s\n",
            size.c_str(),
            share,
            component->id().c_str());

        shown += component->installedSize();
        count += 1;
    }

    if (total > 0) {
        std::printf(
            "\nThose %zu are %.0f%% of the total.\n",
            count,
            100.0 * static_cast<double>(shown) /
                static_cast<double>(total));
    }

    return 0;
}

// What this machine runs.
//
// "Enabled" is the question people mean: not what is installed, and
// not what happens to be running this minute, but what will start the
// next time the machine boots.
int commandServices(bool showAll, bool useRpm) {
    auto result = nexus::system::readServices();

    if (!result.error.empty()) {
        std::cerr << result.error << "\n";
        return 1;
    }

    // Which component installed each one. This is the part that makes
    // it a question about the system rather than about systemd.
    nexus::system::attachOwners(result, useRpm);

    std::cout
        << "Services:    " << result.services.size() << " unit(s)\n"
        << "Starts at boot: " << result.enabled << "\n";

    if (result.systemdRunning) {
        std::cout << "Failed:      " << result.failedCount << "\n";

        if (result.restartingCount > 0) {
            std::cout
                << "Restarting:  " << result.restartingCount
                << " (stuck starting, never reaches failed)\n";
        }
    } else {
        // Unit files can be read without a running systemd; whether
        // anything is running now cannot.
        std::cout
            << "Running:     unknown (systemd is not running here)\n";
    }

    std::cout << "\n";

    std::size_t shown = 0;

    for (const auto& service : result.services) {
        if (!showAll && !service.startsAtBoot() && !service.failed &&
            !service.restarting) {
            continue;
        }

        std::cout << "  ";

        if (service.failed) {
            std::cout << "[failed] ";
        } else if (service.restarting) {
            std::cout << "[loop]   ";
        } else if (service.startsAtBoot()) {
            std::cout << "[boot]   ";
        } else {
            std::cout << "         ";
        }

        std::cout << service.name;

        if (!service.startsAtBoot() && !service.failed &&
            !service.restarting) {
            std::cout << "  (" << toString(service.state) << ")";
        }

        if (!service.owner.empty()) {
            std::cout << "  from " << service.owner;
        }

        std::cout << "\n";

        shown += 1;
    }

    if (shown == 0) {
        std::cout << "  nothing starts at boot\n";
    }

    if (!showAll) {
        std::cout
            << "\nShowing what starts at boot. --all for every "
            << "unit.\n";
    }

    return 0;
}

// What Secure Boot is doing, and what could be done about it.
//
// The usual advice for a custom distribution is to turn it off. That
// is the wrong answer given to everybody because the right answer is
// unfamiliar -- and it is given by projects whose own security
// features it undermines.
int commandSecureBoot(const std::string& root) {
    const auto report =
        nexus::hardware::SecureBootDetector(root).detect();

    std::cout << "Secure Boot: " << toString(report.state) << "\n";

    if (report.ownerKeysEnrolled) {
        std::cout << "MOK list:    present (Fedora's shim puts its own key there too;\n"
                     "             mokutil --list-enrolled shows whose keys they are)\n";
    }
    if (report.shimValidationDisabled) {
        std::cout << "Shim:        checking turned OFF (mokutil --disable-validation):\n"
                     "             the firmware checks shim, and shim checks nothing.\n";
    }

    for (const std::string& problem : report.unreadable) {
        std::cout << "             (" << problem << ")\n";
    }

    std::cout << "\n";

    switch (report.state) {
        case nexus::hardware::SecureBootState::NotSupported:
            std::cout
                << "This machine does not use Secure Boot. Nothing "
                << "to do.\n";
            return 0;

        case nexus::hardware::SecureBootState::Disabled:
            std::cout
                << "The firmware will boot anything, signed or not.\n"
                << "\nThat is how most custom systems are run, and "
                << "it means nothing\nchecks the kernel before it "
                << "starts.\n";
            break;

        case nexus::hardware::SecureBootState::Enabled:
            std::cout
                << "The firmware will only boot a kernel signed by a "
                << "key it trusts.\n";

            std::cout
                << "\nNexus uses Fedora's signed shim, boot loader and kernel, so it\n"
                << "boots as it is. Not covered by Secure Boot: the initramfs and\n"
                << "GRUB's settings files on /boot.\n";

            break;

        case nexus::hardware::SecureBootState::SetupMode:
            std::cout
                << "The firmware is in setup mode and will accept new "
                << "keys.\n";
            break;

        case nexus::hardware::SecureBootState::Unknown:
            std::cout
                << "The state could not be read.\n";
            break;
    }

    std::cout << R"(
There are three ways a custom system can boot with Secure Boot on,
and only one of them is available to a project without a company
behind it.

  A shim signed by Microsoft
      What every distribution you have heard of ships. Getting one
      requires review by the shim review board: a real process,
      months long, and they want a track record first.

  Your own key, enrolled by you
      You sign the kernel; the machine's owner enrols the key once,
      through the firmware, and it is trusted from then on.

      This is not exotic. It is what happens on Ubuntu when a
      driver is built for your kernel -- millions of people have
      done it without knowing what it was called.

      It needs: sbsigntool to sign, mokutil to enrol, one reboot,
      and a password typed into a blue screen the firmware shows.

  Turning Secure Boot off
      What most custom systems tell you to do. It works, and it
      means nothing checks the kernel before it starts. Reasonable
      on a machine you are experimenting with; a poor default for
      anybody else.

Nexus does not sign anything itself, and does not need to: its
images are built on Fedora's, and the kernel in them is Fedora's,
carrying Fedora's signature through the standard shim. Adding
packages on top does not touch it.

So an image built this way boots with Secure Boot on, unchanged.
That was checked the only way it can be -- by installing one on a
machine with Secure Boot enforcing and watching it start.

The three options above matter again the moment a kernel is built
rather than inherited.
)";

    return 0;
}

int commandHardware(const nexus::hardware::HardwareInfo& info) {
    std::cout << "Graphics:\n";

    if (info.graphics.empty()) {
        std::cout << "    (none detected)\n";
    }

    for (const auto& device : info.graphics) {
        std::cout
            << "    " << device.node
            << "  vendor=" << device.vendor
            << " (" << device.vendorId << ":" << device.deviceId << ")";

        if (!device.driver.empty()) {
            std::cout << "  driver=" << device.driver;
        }

        std::cout << "\n";
    }

    std::cout
        << "\nFirmware:    " << toString(info.firmware) << "\n"
        << "Secure Boot: " << toString(info.secureBoot) << "\n"
        << "Chassis:     "
        << (info.hasBattery ? "laptop" : "desktop") << "\n"
        << "Gamepad:     "
        << (info.hasGamepad ? "present" : "none detected") << "\n"
        << "Architecture:" << " " << info.architecture << "\n";

    if (!info.cpuModel.empty()) {
        std::cout << "CPU:         " << info.cpuModel << "\n";
    }

    std::cout << "\nCapabilities:\n";

    for (const std::string& capability : info.capabilities()) {
        std::cout << "    " << capability << "\n";
    }

    if (!info.unreadable.empty()) {
        std::cout << "\nCould not read:\n";

        for (const std::string& problem : info.unreadable) {
            std::cout << "    " << problem << "\n";
        }
    }

    return 0;
}

int commandGaps(const SourceSummary& result) {
    if (result.gaps.empty()) {
        std::cout
            << result.name
            << " reported nothing the model could not represent.\n";
    }

    for (const auto& [kind, count] : result.gaps) {
        std::cout << kind << ": " << count << "\n";
    }

    for (const std::string& note : result.notes) {
        std::cout << "\n" << note << "\n";
    }

    return 0;
}

}  // namespace nexus_cli
