#include <nexus/alias.hpp>

#include <algorithm>
#include <utility>

namespace nexus {

void AliasTable::add(
    const std::string& capability,
    std::vector<Constraint> alternatives
) {
    aliases_[capability] = std::move(alternatives);
}

void AliasTable::merge(const AliasTable& other) {
    for (const auto& [capability, alternatives] : other.aliases_) {
        std::vector<Constraint>& present = aliases_[capability];

        for (const Constraint& option : alternatives) {
            const bool already = std::any_of(
                present.begin(),
                present.end(),
                [&option](const Constraint& existing) {
                    return existing.capability == option.capability;
                }
            );

            if (!already) {
                present.push_back(option);
            }
        }
    }
}

bool AliasTable::knows(const std::string& capability) const {
    return aliases_.count(capability) > 0;
}

std::size_t AliasTable::size() const {
    return aliases_.size();
}

Requirement AliasTable::expand(const Requirement& requirement) const {
    Requirement expanded;

    for (const Constraint& option : requirement.alternatives) {
        // The original stays first, so a real capability by that name
        // wins over the translation.
        expanded.alternatives.push_back(option);

        const auto entry = aliases_.find(option.capability);

        if (entry == aliases_.end()) {
            continue;
        }

        for (const Constraint& alternative : entry->second) {
            const bool already = std::any_of(
                expanded.alternatives.begin(),
                expanded.alternatives.end(),
                [&alternative](const Constraint& present) {
                    return present.capability == alternative.capability;
                }
            );

            if (!already) {
                expanded.alternatives.push_back(alternative);
            }
        }
    }

    expanded.pre = requirement.pre;

    return expanded;
}

}
