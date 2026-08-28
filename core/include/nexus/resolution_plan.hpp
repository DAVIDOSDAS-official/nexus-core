#pragma once

#include <string>
#include <vector>

namespace nexus {

enum class ResolutionPlanStatus {
    Ready,
    Failed
};

struct ResolutionPlan {
    ResolutionPlanStatus status;
    std::string reason;

    std::vector<std::string> install;
    std::vector<std::string> remove;
    std::vector<std::string> configure;
};

}
