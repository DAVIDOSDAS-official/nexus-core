#pragma once

#include <string>
#include <vector>

#include <nexus/requirement.hpp>

namespace nexus::system {

struct SourceBuildInfo {
    std::string package;
    std::string sourcePackage;
    std::string version;

    // Written in the same grammar as any other dependency field, so
    // the resolver can cost them the way it costs anything else.
    std::vector<Requirement> buildDependencies;

    bool sourcesAvailable = false;

    std::string error;
};

// What building a package from source would involve.
//
// The benefit of a source build is specific: your own compiler flags,
// a patch, or a version newer than the archive has. It is not free
// and it is not general -- nobody needs a source-built `less`. So
// this says what it costs and leaves the decision alone.
//
// Compile time is not predicted. It depends on the package, the
// machine and the flags, and a number invented here would be worse
// than no number.
SourceBuildInfo readSourceBuild(const std::string& package);

// Exposed for testing against captured output.
SourceBuildInfo parseSourceStanza(
    const std::string& package,
    const std::string& text
);

// Whether the system is configured to fetch source at all. On Debian
// this needs deb-src lines, which are off by default -- and the error
// apt gives says what is missing without saying what to do about it.
bool sourcePackagesAvailable();

}
