#include <nexus/component.hpp>

#include <utility>

namespace nexus {

Component::Component(
    std::string id,
    std::string name,
    std::string version,
    ComponentType type
)
    : id_(std::move(id)),
      name_(std::move(name)),
      version_(std::move(version)),
      type_(type) {
}

const std::string& Component::id() const {
    return id_;
}

const std::string& Component::name() const {
    return name_;
}

const std::string& Component::version() const {
    return version_;
}

ComponentType Component::type() const {
    return type_;
}

void Component::addProvidedCapability(Capability capability) {
    provides_.push_back(std::move(capability));
}

void Component::addRequiredCapability(Capability capability) {
    requires_.push_back(std::move(capability));
}

void Component::addRecommendedCapability(Capability capability) {
    recommends_.push_back(std::move(capability));
}

const std::vector<Capability>& Component::providedCapabilities() const {
    return provides_;
}

const std::vector<Capability>& Component::requiredCapabilities() const {
    return requires_;
}

const std::vector<Capability>& Component::recommendedCapabilities() const {
    return recommends_;
}

std::string toString(ComponentType type) {
    switch (type) {
        case ComponentType::Kernel:         return "Kernel";
        case ComponentType::Bootloader:     return "Bootloader";
        case ComponentType::Desktop:        return "Desktop";
        case ComponentType::Compositor:     return "Compositor";
        case ComponentType::DisplayServer:  return "DisplayServer";
        case ComponentType::Audio:          return "Audio";
        case ComponentType::Filesystem:     return "Filesystem";
        case ComponentType::Driver:         return "Driver";
        case ComponentType::PackageManager: return "PackageManager";
        case ComponentType::PackageSource:  return "PackageSource";
        case ComponentType::Service:        return "Service";
        case ComponentType::Security:       return "Security";
        case ComponentType::Networking:     return "Networking";
        case ComponentType::Development:    return "Development";
        case ComponentType::Utility:        return "Utility";
        case ComponentType::Application:    return "Application";
        case ComponentType::Library:        return "Library";
        case ComponentType::Toolchain:      return "Toolchain";
        case ComponentType::Profile:        return "Profile";
    }

    return "Unknown";
}

}
