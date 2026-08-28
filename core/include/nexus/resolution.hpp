#pragma once

#include <string>
#include <vector>

namespace nexus {

enum class ResolutionStatus {
    Success,
    NotFound,
    Ambiguous
};

struct ResolutionResult {
    ResolutionStatus status;
    std::string reason;
    std::vector<std::string> candidates;
    std::string selectedProvider;
};

}
