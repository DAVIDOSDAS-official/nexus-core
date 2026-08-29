#pragma once

#include <string>
#include <vector>

#include <nexus/constraint.hpp>

namespace nexus {

// One thing a component needs, which may be satisfiable in more than
// one way.
//
//     libc6 (>= 2.38)              one alternative
//     base-passwd | adduser        two alternatives
//
// Alternatives are ordered: the first is the declared preference.
// Keeping all of them is what makes resolution a search rather than a
// lookup, and it is why the resolver has to be able to backtrack.
struct Requirement {
    std::vector<Constraint> alternatives;

    // A pre-dependency must be fully configured before this component
    // is even unpacked. An ordinary dependency only has to be
    // configured before this one is. The distinction is invisible to
    // resolution and decisive for ordering.
    bool pre = false;

    Requirement() = default;

    explicit Requirement(Constraint single) {
        alternatives.push_back(std::move(single));
    }

    explicit Requirement(std::vector<Constraint> options)
        : alternatives(std::move(options)) {
    }

    bool hasChoice() const {
        return alternatives.size() > 1;
    }

    bool empty() const {
        return alternatives.empty();
    }
};

std::string toString(const Requirement& requirement);

}
