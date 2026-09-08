#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

struct PortageSourceResult {
    std::vector<Component> components;

    std::size_t packagesRead = 0;

    // Ebuilds that build from the project's git head rather than a
    // release. Gentoo spells these 9999, they are never what somebody
    // means by "install nano", and they sort above every real version
    // -- so a resolver preferring the newest picks them every time.
    std::size_t liveEbuilds = 0;

    // Dependencies conditional on a USE flag whose value this reader
    // guessed from the flag's default. Counted rather than hidden:
    // Gentoo's dependencies genuinely change with configuration, and
    // a reader that pretends otherwise is answering a different
    // question from the one asked.
    std::size_t conditional = 0;

    std::map<std::string, std::size_t> gaps;

    std::string error;
};

// Reads Gentoo's md5-cache.
//
// Portage's metadata is a fourth format, and its dependency grammar
// is the richest of them:
//
//     >=sys-libs/ncurses-5.9-r1:=[unicode(+)?]   version, slot, USE
//     magic? ( sys-apps/file )                   conditional on USE
//     || ( app-crypt/gnupg app-alternatives/gpg ) any of these
//
// USE flags are the part with no equivalent in the model. A package's
// dependencies change with them, so this reader evaluates conditions
// against each flag's declared default -- IUSE lists "+spell" for on
// and "spell" for off -- and reports how many it had to assume.
//
// That is an approximation and it is stated as one. The alternative
// is refusing to read Gentoo at all, and the alternative to that is
// pretending the flags do not exist.
PortageSourceResult parsePortageEntry(
    const std::string& category,
    const std::string& nameVersion,
    const std::string& text
);

// Reads a whole md5-cache directory.
// keepEveryVersion is for looking at the tree itself. By default
// only the newest release of each package is kept.
//
// Portage holds every version at once, unlike an apt or dnf archive
// which holds about one. Listing them all turns "what are my options
// for a text editor" into five entries for nano, which is not a
// choice anybody was making.
PortageSourceResult readPortageTree(
    const std::string& path,
    const std::set<std::string>& categories = {},
    bool keepEveryVersion = false
);

// Gentoo's convention for an ebuild that tracks a repository rather
// than a release.
bool isLiveEbuild(const std::string& version);

// Portage compares numerically per component, so 9.10 is later than
// 9.9 -- which a string comparison gets backwards.
int comparePortageVersions(
    const std::string& left,
    const std::string& right
);

// Split "nano-8.7.1" into "nano" and "8.7.1". Versions start at the
// first hyphen followed by a digit, because names contain hyphens
// too: "app-alternatives/gpg" and "openpgp-keys-bennoschulenberg".
std::pair<std::string, std::string> splitNameVersion(
    const std::string& text
);

// One atom: ">=sys-libs/ncurses-5.9-r1:=[unicode(+)?]".
Constraint parsePortageAtom(const std::string& text);

// Which USE flags are on by default, from IUSE.
std::set<std::string> defaultUseFlags(const std::string& iuse);

}
