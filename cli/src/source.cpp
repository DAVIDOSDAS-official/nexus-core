// nexus source: what building a package would cost.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// What building a package from source would cost.
//
// The benefit is specific -- your own flags, a patch, a version the
// archive does not have -- and it is not free. Nexus can say what the
// build requires, because Build-Depends is written in the same
// grammar as everything else and the resolver already costs
// dependency sets.
//
// What it does not say is how long the compile takes. That depends on
// the package, the machine and the flags, and an invented number
// would be worse than none.
int commandSource(
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& package,
    const std::string& architecture,
    bool showCommands
) {
    const auto info = nexus::system::readSourceBuild(package);

    if (!info.error.empty()) {
        std::cerr << info.error << "\n";

        if (!info.sourcesAvailable) {
            // apt says what is missing without saying what to do
            // about it, which is most of why people give up here.
            std::cerr
                << "\nDebian and Ubuntu ship source separately, and "
                << "it is off by default.\nEnable it with:\n\n"
                << "    sudo sed -i 's/^Types: deb$/Types: deb "
                << "deb-src/' \\\n"
                << "        /etc/apt/sources.list.d/ubuntu.sources\n"
                << "    sudo apt update\n\n"
                << "On older releases the same thing is a commented "
                << "deb-src line in\n/etc/apt/sources.list.\n";
        }

        return 1;
    }

    std::cout
        << "Package:     " << info.package << "\n"
        << "Source:      " << info.sourcePackage;

    if (!info.version.empty()) {
        std::cout << "  " << info.version;
    }

    std::cout
        << "\nBuild needs: " << info.buildDependencies.size()
        << " requirement(s)\n";

    // Cost them the way anything else is costed.
    std::set<std::string> here;

    for (const Component& component : installed) {
        here.insert(component.name() + ":" + component.architecture());
    }

    nexus::SolverRequest request;

    request.architecture = architecture;
    request.scope = nexus::Source::Base;

    for (const auto& requirement : info.buildDependencies) {
        request.requirements.push_back(requirement);
    }

    const auto solution = buildSolver(universe).solve(request);

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout
            << "\nThe build dependencies cannot be resolved.\n";

        if (!solution.blockedOn.empty()) {
            std::cout << "Blocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::vector<std::string> toInstall;
    std::uint64_t download = 0;
    std::uint64_t disk = 0;

    for (const std::string& id : solution.selected) {
        for (const Component& component : universe) {
            if (component.id() != id) {
                continue;
            }

            if (here.count(component.name() + ":" +
                           component.architecture()) == 0) {
                toInstall.push_back(component.name());
                download += component.downloadSize();
                disk += component.installedSize();
            }

            break;
        }
    }

    std::cout
        << "Would add:   " << toInstall.size() << " component(s)";

    if (download > 0 || disk > 0) {
        std::cout << " (";

        if (download > 0) {
            std::cout
                << nexus::system::formatSize(download) << " to fetch";
        }

        if (download > 0 && disk > 0) {
            std::cout << ", ";
        }

        if (disk > 0) {
            std::cout
                << nexus::system::formatSize(disk) << " on disk";
        }

        std::cout << ")";
    }

    std::cout << "\n";

    if (!toInstall.empty()) {
        std::cout << "\nBuild tools it would install:\n";

        std::size_t shown = 0;

        for (const std::string& name : toInstall) {
            std::cout << "    " << name << "\n";

            if (++shown >= 12) {
                std::cout
                    << "    ... and " << (toInstall.size() - shown)
                    << " more\n";
                break;
            }
        }
    }

    std::cout
        << "\nHow long the compile takes is not predicted: it "
        << "depends on the\npackage, the machine and the flags.\n";

    if (showCommands || true) {
        std::cout
            << "\nWhat to run:\n\n"
            << "    sudo apt-get build-dep " << package << "\n"
            << "    apt-get source " << package << "\n"
            << "    cd " << info.sourcePackage << "-*\n"
            << "    dpkg-buildpackage -b -uc -us\n"
            << "\nYour flags go in DEB_CFLAGS_APPEND, or edit "
            << "debian/rules.\n"
            << "Nexus does not run these: a build is yours to watch.\n";
    }

    return 0;
}

}  // namespace nexus_cli
