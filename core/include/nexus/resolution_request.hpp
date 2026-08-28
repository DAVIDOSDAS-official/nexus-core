#pragma once

#include <optional>
#include <string>

#include <nexus/capability.hpp>

namespace nexus {

struct ResolutionRequest {
    Capability capability;
    std::optional<std::string> preferredProvider;
    std::optional<std::string> requiredProvider;
};

}
