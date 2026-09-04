#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

struct PacmanSourceResult {
    std::vector<Component> components;

    std::size_t packagesRead = 0;

    std::string error;
};

// Reads an Arch sync database.
//
// The format is a tar of directories, one per package, each holding a
// desc file of the shape:
//
//     %NAME%
//     acl
//
//     %VERSION%
//     2.4.0-1
//
//     %DEPENDS%
//     glibc>=2.38
//     attr
//
// A field name on its own line, its values beneath, a blank line
// between. Nothing like a control file and nothing like repodata:
// three ecosystems, three formats, one model underneath.
//
// The source is a parameter rather than a constant. An Arch package
// read on Arch comes from the base repositories; the same package
// read on Debian is only reachable through a container, and calling
// it Base there would promise something that cannot be delivered.
PacmanSourceResult parsePacmanDatabase(
    const std::string& text,
    Source source = Source::Container
);

// Reads a .db file, which is a gzipped tar.
PacmanSourceResult readPacmanDatabase(
    const std::string& path,
    Source source = Source::Container
);

// "glibc>=2.38" and "attr" as pacman writes them.
Constraint parsePacmanDependency(const std::string& text);

}
