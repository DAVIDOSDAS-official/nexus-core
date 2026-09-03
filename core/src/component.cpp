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
    requirements_.push_back(
        Requirement(Constraint(capability.name()))
    );
}

void Component::setId(std::string id) {
    id_ = std::move(id);
}

void Component::setSource(Source source) {
    source_ = source;
}

Source Component::source() const {
    return source_;
}

void Component::setArchitecture(std::string architecture) {
    architecture_ = std::move(architecture);
}

const std::string& Component::architecture() const {
    return architecture_;
}

void Component::setMultiArch(MultiArch value) {
    multiArch_ = value;
}

MultiArch Component::multiArch() const {
    return multiArch_;
}

void Component::addRequirement(Requirement requirement) {
    requirements_.push_back(std::move(requirement));
}

const std::vector<Requirement>& Component::requirements() const {
    return requirements_;
}

void Component::addRecommendedCapability(Capability capability) {
    recommends_.push_back(
        Requirement(Constraint(capability.name()))
    );
}

const std::vector<Capability>& Component::providedCapabilities() const {
    return provides_;
}

// Flattened view: the first alternative of each requirement. Kept for
// callers that predate alternatives; new code should use
// requirements(), which does not discard the choices.
std::vector<Capability> Component::requiredCapabilities() const {
    std::vector<Capability> flattened;

    for (const Requirement& requirement : requirements_) {
        if (requirement.empty()) {
            continue;
        }

        flattened.push_back(
            Capability(requirement.alternatives.front().capability)
        );
    }

    return flattened;
}

void Component::addRecommendation(Requirement requirement) {
    recommends_.push_back(std::move(requirement));
}

const std::vector<Requirement>& Component::recommendations() const {
    return recommends_;
}

std::vector<Capability> Component::recommendedCapabilities() const {
    std::vector<Capability> flattened;

    for (const Requirement& requirement : recommends_) {
        if (requirement.empty()) {
            continue;
        }

        flattened.push_back(
            Capability(requirement.alternatives.front().capability)
        );
    }

    return flattened;
}

void Component::addConflict(Constraint constraint) {
    conflicts_.push_back(std::move(constraint));
}

const std::vector<Constraint>& Component::conflicts() const {
    return conflicts_;
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
