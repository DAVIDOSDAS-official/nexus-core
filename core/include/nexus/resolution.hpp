#pragma once

#include <string>
#include <vector>

#include <nexus/resolution_plan.hpp>

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
    ResolutionPlan plan;
};

}
