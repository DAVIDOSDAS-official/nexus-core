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
    // Not Success: a result nobody filled in must not pass for one.
    ResolutionStatus status = ResolutionStatus::NotFound;
    std::string reason;
    std::vector<std::string> candidates;
    std::string selectedProvider;
    ResolutionPlan plan;
};

}
