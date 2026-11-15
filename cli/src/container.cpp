// nexus container: a package from another distribution, in a container.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// The first thing somebody sees.
//
// Everything under this already exists -- profiles, composition,
// hardware detection, verified installs. What was missing was
// anything that presents them, and a system nobody can choose is a
// system nobody uses.
//
// It states what each profile would cost on this machine before
// asking, rather than after. Choosing between names alone is not
// choosing.
// Install a package from another distribution, through a container.
//
// The container is distrobox's: it creates it, wires the home
// directory and the display, and exports the binary onto the host
// PATH. Nexus decides which distribution, which container, and which
// package -- and says what it will cost before doing any of it.
int commandContainerInstall(
    const std::vector<Component>& universe,
    const std::string& requested,
    const std::string& distribution,
    bool apply,
    bool assumeYes
) {
    // The name goes into the container's package manager on a command
    // line: only something that can be a package name gets that far.
    if (!nexus::system::isPackageName(requested)) {
        std::cerr << "Not a package name: " << requested << "\n";
        return 2;
    }

    if (!nexus::system::Containers::available()) {
        std::cerr
            << "distrobox is not installed.\n"
            << "It creates and wires the container; Nexus only "
            << "decides what goes in it.\n";

        return 1;
    }

    const std::string image =
        nexus::system::containerImageFor(distribution);

    if (image.empty()) {
        std::cerr
            << "No container image known for '" << distribution
            << "'.\nKnown: arch, fedora, debian, ubuntu\n";

        return 1;
    }

    const std::string container =
        nexus::system::containerNameFor(distribution);

    // What this actually costs, before anything is created.
    const Component* found = nullptr;

    for (const Component& component : universe) {
        if (component.name() == requested &&
            component.source() == nexus::Source::Container) {
            found = &component;
            break;
        }
    }

    std::cout
        << "Package:     " << requested << "\n"
        << "From:        " << distribution << " (" << image << ")\n"
        << "Container:   " << container << "\n";

    if (found != nullptr && found->downloadSize() > 0) {
        std::cout
            << "Package:     "
            << nexus::system::formatSize(found->downloadSize())
            << " to fetch\n";
    }

    std::cout
        << "\nThe container is a whole distribution. The first "
        << "package from it\ncosts several hundred megabytes; "
        << "later ones cost only themselves.\n";

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to do it.\n";

        return 0;
    }

    if (!assumeYes) {
        std::cout << "\nProceed? [y/N] ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "y" && answer != "Y" && answer != "yes") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    std::cout << "\nPreparing the container.\n";

    const auto prepared =
        nexus::system::Containers::ensure(container, image);

    for (const std::string& line : prepared.output) {
        std::cout << line << "\n";
    }

    if (!prepared.ok) {
        std::cerr
            << "\nCould not prepare the container";

        if (!prepared.error.empty()) {
            std::cerr << ": " << prepared.error;
        }

        std::cerr << "\nNothing has been changed on the host.\n";

        return 1;
    }

    std::cout << "\nInstalling inside it.\n";

    // The container's own package manager does the installing, the
    // same way apt does on the host.
    const std::string install =
        distribution == "arch"
            ? "sudo pacman -Sy --noconfirm " + shellQuote(requested)
            : distribution == "fedora"
                ? "sudo dnf install -y " + shellQuote(requested)
                : "sudo apt-get install -y " + shellQuote(requested);

    const auto installed =
        nexus::system::Containers::run(container, install);

    for (const std::string& line : installed.output) {
        std::cout << line << "\n";
    }

    if (!installed.ok) {
        std::cerr
            << "\nThe package did not install. The container is "
            << "still there\nand the host is unchanged.\n";

        return 1;
    }

    std::cout << "\nFinding out what it installed.\n";

    // Not "the binary named after the package". Arch's metasploit
    // ships msfconsole, msfvenom and msfdb, and nothing called
    // metasploit.
    const auto binaries = nexus::system::Containers::binariesOf(
        container, distribution, requested);

    if (binaries.paths.empty()) {
        // Say which of the two things happened.
        if (!binaries.queried || binaries.linesSeen == 0) {
            std::cout
                << "\nCould not ask the container what it installed"
                << " (" << binaries.linesSeen << " line(s) back).\n";

            if (!binaries.firstLine.empty()) {
                std::cout
                    << "First line was: " << binaries.firstLine
                    << "\n";
            }
        } else {
            std::cout
                << "\nInstalled. Its " << binaries.linesSeen
                << " file(s) include no commands under /usr/bin.\n";

            if (!binaries.firstLine.empty()) {
                std::cout
                    << "First line was: " << binaries.firstLine
                    << "\n";
            }
        }

        std::cout
            << "Reach the container with:\n"
            << "    distrobox enter " << container << "\n";

        return 0;
    }

    std::cout
        << "\nPutting " << binaries.paths.size()
        << " command(s) on your PATH.\n";

    std::vector<std::string> exported;
    std::vector<std::string> refused;

    for (const std::string& path : binaries.paths) {
        const auto result =
            nexus::system::Containers::exportBinary(container, path);

        const std::size_t slash = path.rfind('/');

        const std::string command =
            slash == std::string::npos
                ? path
                : path.substr(slash + 1);

        if (result.ok) {
            exported.push_back(command);
        } else {
            refused.push_back(command);
        }
    }

    for (const std::string& command : exported) {
        std::cout << "    " << command << "\n";
    }

    if (exported.empty()) {
        std::cout
            << "\nInstalled, but nothing could be exported. Reach "
            << "it with:\n"
            << "    distrobox enter " << container << "\n";

        return 0;
    }

    std::cout
        << "\nDone. Those run inside " << container
        << ". They are on your PATH at\n"
        << "~/.local/bin, so a new shell will find them.\n";

    if (!refused.empty()) {
        std::cout
            << "\n" << refused.size()
            << " could not be exported.\n";
    }

    return 0;
}

}  // namespace nexus_cli
