#pragma once

#include <map>
#include <string>
#include <vector>

#include <nexus/requirement.hpp>

namespace nexus {

// Maps an abstract capability onto whatever a particular ecosystem
// calls it.
//
// A profile that asks for "firefox | chromium" is a Debian profile
// wearing a disguise. Asked on Fedora it reports everything missing on
// a machine where all of it is installed. Naming the intent instead --
// "web-browser" -- and translating per ecosystem is what makes one
// profile work on both.
//
// Expansion is additive: the original name stays as the first
// alternative, so a real capability by that name always wins over the
// translation. On Debian "mail-transport-agent" is a genuine virtual
// package and resolves directly; on Fedora it falls through to the
// alternatives.
class AliasTable {
public:
    void add(
        const std::string& capability,
        std::vector<Constraint> alternatives
    );

    bool knows(const std::string& capability) const;

    // The requirement with any aliased capability expanded in place.
    Requirement expand(const Requirement& requirement) const;

    std::size_t size() const;

private:
    std::map<std::string, std::vector<Constraint>> aliases_;
};

}
