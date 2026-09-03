#pragma once

#include <string>
#include <vector>

#include <nexus/capability.hpp>
#include <nexus/architecture.hpp>
#include <nexus/constraint.hpp>
#include <nexus/requirement.hpp>
#include <cstdint>

#include <nexus/source.hpp>

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
    std::vector<Capability> requiredCapabilities() const;
    // Recommendations are clauses, not names.
    //
    // "Recommends: libodbc2 | libodbc1" is one recommendation with two
    // ways of meeting it. Storing only the first made a machine with
    // the second one installed look as though nothing wanted it --
    // the same mistake requirements outgrew when alternatives arrived,
    // repeated here because recommendations kept the older shape.
    void addRecommendation(Requirement requirement);

    const std::vector<Requirement>& recommendations() const;

    // Flattened view: the first alternative of each recommendation.
    // Kept for callers that predate alternatives.
    std::vector<Capability> recommendedCapabilities() const;

    // Components this one cannot coexist with.
    // Full requirements, alternatives preserved.
    // Architecture defaults to "all", which satisfies anything.
    // Ids are assigned by whichever source built the component, and
    // may need requalifying once components from several sources are
    // combined.
    void setId(std::string id);

    // Where this came from. Defaults to the distribution's own
    // repositories, which is where everything came from until there
    // was more than one place for it to come from.
    // Bytes, always. Zero means unknown, which is different from
    // zero bytes and has to stay distinguishable: metadata does not
    // always carry a size, and reporting a missing one as nothing
    // would make a package look free.
    //
    // Debian states Installed-Size in kibibytes and Size in bytes;
    // rpm states both in bytes; flatpak prints "196.9 MB". The
    // conversion belongs in the reader, so that everything past this
    // point is comparing the same unit.
    void setDownloadSize(std::uint64_t bytes);

    std::uint64_t downloadSize() const;

    void setInstalledSize(std::uint64_t bytes);

    std::uint64_t installedSize() const;

    void setSource(Source source);

    Source source() const;

    void setArchitecture(std::string architecture);

    const std::string& architecture() const;

    void setMultiArch(MultiArch value);

    MultiArch multiArch() const;

    void addRequirement(Requirement requirement);

    const std::vector<Requirement>& requirements() const;

    void addConflict(Constraint constraint);

    const std::vector<Constraint>& conflicts() const;

private:
    std::string id_;
    std::string name_;
    std::string version_;
    ComponentType type_;

    std::vector<Capability> provides_;
    std::vector<Requirement> requirements_;
    std::string architecture_ = kArchitectureAll;
    MultiArch multiArch_ = MultiArch::No;
    Source source_ = Source::Base;
    std::uint64_t downloadSize_ = 0;
    std::uint64_t installedSize_ = 0;
    std::vector<Requirement> recommends_;
    std::vector<Constraint> conflicts_;
};

}
