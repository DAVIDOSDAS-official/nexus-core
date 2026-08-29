#include <nexus/capability.hpp>

#include <utility>

namespace nexus {

Capability::Capability(std::string name)
    : name_(std::move(name)) {
}

Capability::Capability(std::string name, std::string version)
    : name_(std::move(name)),
      version_(std::move(version)) {
}

const std::string& Capability::name() const {
    return name_;
}

const std::string& Capability::version() const {
    return version_;
}

bool Capability::hasVersion() const {
    return !version_.empty();
}

}
