#pragma once

#include <string>
#include <vector>

namespace nexus {

enum class ResolutionPlanStatus {
    Ready,
    Failed
};

struct ResolutionPlan {
    ResolutionPlanStatus status = ResolutionPlanStatus::Failed;   // never "Ready" by accident (0.1.84)
    std::string reason;

    std::vector<std::string> install;
    std::vector<std::string> remove;
    std::vector<std::string> configure;
};

}
