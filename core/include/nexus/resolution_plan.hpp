#pragma once

#include <string>
#include <vector>

namespace nexus {

struct ResolutionPlan {
    std::vector<std::string> install;
    std::vector<std::string> remove;
    std::vector<std::string> configure;

    // Components are ordered so dependencies appear
    // before the components that require them.
    std::vector<std::string> installOrder;
};

}
