#pragma once

#include <string>
#include <vector>

namespace nexus::system {

struct ContainerInfo {
    std::string id;
    std::string name;
    std::string status;
    std::string image;

    bool running() const;
};

struct ContainerResult {
    bool ok = false;

    std::vector<std::string> output;

    int exitCode = 0;

    std::string error;
};

// Containers, by asking distrobox.
//
// Nexus decides which container a package should come from and which
// package it is. Creating containers, wiring the home directory,
// sharing the display and putting a binary on the host PATH are
// distrobox's job, and it does them properly -- reimplementing that
// would be building something that already exists and getting the
// integration subtly wrong.
//
// What Nexus adds is the reasoning: that this capability is best met
// from Arch, that it costs 745 MB, and that the alternative is a
// Flatpak at 107 MB.
class Containers {
public:
    static bool available();

    // Every container distrobox knows about.
    static std::vector<ContainerInfo> list();

    // Exposed so the parsing can be tested against captured output.
    static std::vector<ContainerInfo> parseList(const std::string& text);

    // Create one if it does not exist. Existing containers are left
    // alone: somebody may have put things in theirs.
    static ContainerResult ensure(
        const std::string& name,
        const std::string& image
    );

    // Run a command inside one, interactively.
    //
    // Through distrobox, because its integration -- the home
    // directory, the display, the user -- is the point of using it.
    static ContainerResult run(
        const std::string& name,
        const std::string& command
    );

    // Run a command inside one and read all of its output.
    //
    // Through podman, not distrobox. `distrobox enter` truncates its
    // output when it is not attached to a terminal, and it truncates
    // by a different amount each time: the same query returned 39,158
    // lines once and 9,987 the next. A reader cannot recover output a
    // writer never wrote.
    //
    // A distrobox container is a podman container, so asking podman
    // directly is the same container without the terminal handling.
    // distrobox stays for the things it is good at.
    static ContainerResult query(
        const std::string& name,
        const std::string& command
    );

    // Which executables a package owns, according to the container's
    // own package manager.
    //
    // Guessing the binary from the package name is wrong twice: Arch's
    // metasploit ships msfconsole, msfvenom and msfdb and nothing
    // called metasploit, and a package that ships a dozen commands
    // does not have "a" binary. The package manager already knows;
    // asking it is the whole answer.
    struct Binaries {
        std::vector<std::string> paths;

        // How many lines the query returned, and whether it
        // succeeded. "Ships no commands" and "the query failed" are
        // different facts, and reporting the first when the second
        // happened sends somebody looking in the wrong place.
        std::size_t linesSeen = 0;
        bool queried = false;
        std::string firstLine;
    };

    static Binaries binariesOf(
        const std::string& name,
        const std::string& distribution,
        const std::string& package
    );

    // Put a container's binary on the host PATH, so a package from
    // another distribution becomes something you type rather than
    // something you exec into a container to reach.
    static ContainerResult exportBinary(
        const std::string& name,
        const std::string& binaryPath
    );
};

// The image a source's packages come from, and the container name
// Nexus uses for it. One container per distribution rather than one
// per package: a second Arch container would download Arch twice.
std::string containerImageFor(const std::string& distribution);

std::string containerNameFor(const std::string& distribution);

}
