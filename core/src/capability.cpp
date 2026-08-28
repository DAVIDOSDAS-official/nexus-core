#include <nexus/capability.hpp>

#include <utility>

namespace nexus {

Capability::Capability(std::string name)
    : name_(std::move(name)) {
}

const std::string& Capability::name() const {
    return name_;
}

}
