#pragma once

#include <string>
#include <vector>

namespace nexus::system {

// "Did you mean" for a package name nobody provides (0.1.63).
//
// `nexus install java-lastest-openjdk` used to answer "No combination
// of components satisfies every requirement" -- true, and useless to
// somebody who made a typo (the Asus, 1 November). This finds the
// names a typo could have meant: at most two letters off (one for
// short names), closest first, then shortest. Pure, so it is tested.
std::vector<std::string> spellingSuggestions(
    const std::vector<std::string>& names,
    const std::string& typed,
    std::size_t max = 3);

// Edit distance, giving up (returning limit + 1) as soon as it is
// clearly more than limit.
std::size_t boundedDistance(const std::string& a, const std::string& b,
                            std::size_t limit);

}  // namespace nexus::system
