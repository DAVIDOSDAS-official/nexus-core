#include <nexus/architecture.hpp>

namespace nexus {

bool architectureSatisfies(
    const std::string& providerArchitecture,
    MultiArch providerMultiArch,
    const std::optional<std::string>& qualifier,
    const std::string& requesterArchitecture
) {
    // Architecture-independent components satisfy anything.
    if (providerArchitecture == kArchitectureAll) {
        return true;
    }

    if (qualifier.has_value()) {
        // "libfoo:any" is only legal against a provider that opted in.
        if (*qualifier == kArchitectureAny) {
            return providerMultiArch == MultiArch::Allowed ||
                   providerMultiArch == MultiArch::Foreign;
        }

        return providerArchitecture == *qualifier;
    }

    // The caller is not tracking architecture.
    if (requesterArchitecture.empty()) {
        return true;
    }

    // A foreign-marked component satisfies any architecture.
    if (providerMultiArch == MultiArch::Foreign) {
        return true;
    }

    return providerArchitecture == requesterArchitecture;
}

MultiArch parseMultiArch(const std::string& text) {
    if (text == "same") {
        return MultiArch::Same;
    }

    if (text == "foreign") {
        return MultiArch::Foreign;
    }

    if (text == "allowed") {
        return MultiArch::Allowed;
    }

    return MultiArch::No;
}

std::string toString(MultiArch value) {
    switch (value) {
        case MultiArch::No:
            return "no";
        case MultiArch::Same:
            return "same";
        case MultiArch::Foreign:
            return "foreign";
        case MultiArch::Allowed:
            return "allowed";
    }

    return "no";
}

}
