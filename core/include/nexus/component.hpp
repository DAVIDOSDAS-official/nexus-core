#pragma once

#include <string>
#include <vector>

#include <nexus/capability.hpp>
#include <nexus/constraint.hpp>

namespace nexus {

enum class ComponentType {
    Kernel,
    Bootloader,
    Desktop,
    Compositor,
    DisplayServer,
    Audio,
    Filesystem,
    Driver,
    PackageManager,
    PackageSource,
    Service,
    Security,
    Networking,
    Development,
    Utility,
    Application,
    Library,
    Toolchain,
    Profile
};

std::string toString(ComponentType type);

class Component {
public:
    Component(
        std::string id,
        std::string name,
        std::string version,
        ComponentType type
    );

    const std::string& id() const;
    const std::string& name() const;
    const std::string& version() const;
    ComponentType type() const;

    void addProvidedCapability(Capability capability);
    void addRequiredCapability(Capability capability);
    void addRecommendedCapability(Capability capability);

    const std::vector<Capability>& providedCapabilities() const;
    const std::vector<Capability>& requiredCapabilities() const;
    const std::vector<Capability>& recommendedCapabilities() const;

    // Components this one cannot coexist with.
    void addConflict(Constraint constraint);

    const std::vector<Constraint>& conflicts() const;

private:
    std::string id_;
    std::string name_;
    std::string version_;
    ComponentType type_;

    std::vector<Capability> provides_;
    std::vector<Capability> requires_;
    std::vector<Capability> recommends_;
    std::vector<Constraint> conflicts_;
};

}
