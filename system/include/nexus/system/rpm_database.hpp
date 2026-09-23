#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

struct RpmDatabaseResult {
    std::vector<Component> components;

    std::size_t packagesRead = 0;

    // Requirements on rpm's own features, such as
    // rpmlib(PayloadIsZstd). No package provides these; they are
    // satisfied by the rpm binary itself. Counted rather than
    // silently dropped, because they are most of what a package
    // declares and their absence from the model should be visible.
    std::size_t rpmlibRequirements = 0;

    // Paths a package owns. rpm lets a requirement name a file, so
    // these are capabilities like any other -- and without them a
    // system looks unsatisfiable in ways that have nothing to do with
    // packages. bash requires /usr/bin/sh.
    std::size_t fileProvides = 0;

    // Rich dependencies: "(a if b)", "(a or b)". The model has no
    // conditional requirements, so these are recorded and skipped
    // rather than passed through as a literal capability name that
    // nothing can ever provide.
    std::size_t booleanRequirements = 0;

    std::string error;

    // Set when what was read cannot be trusted to be the whole
    // database: rpm exited with an error, said something on stderr,
    // or listed more packages than the full query yielded. The
    // components are still here -- a partial list is worth showing --
    // but nothing may be concluded from what is absent from it.
    //
    // On 23 September a laptop read 1054 of its 1103 packages and
    // doctor reported 305 requirements nothing satisfied, libsystemd
    // among them. rpm's exit status was never checked and its errors
    // went to /dev/null, so a short list looked exactly like a
    // complete one. This field is what makes the difference visible.
    std::string incomplete;
};

// Reads the installed set from the rpm database.
//
// By asking rpm, not by parsing it. The rpmdb is a database file
// whose format has changed more than once; rpm already queries it
// correctly and is guaranteed to match whatever version wrote it.
// Parsing it here would be building something that already exists,
// and getting it subtly wrong on the first system that used a
// different backend.
class RpmDatabase {
public:
    // root is passed to rpm as --root, so an image or chroot can be
    // inspected from outside it.
    explicit RpmDatabase(std::string root = "");

    RpmDatabaseResult load() const;

    // Exposed for testing against captured output.
    static RpmDatabaseResult parse(const std::string& queryOutput);

    static bool available();

private:
    std::string root_;
};

}
