#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/system/control_file.hpp>
#include <nexus/system/dependency_expression.hpp>

namespace nexus::system {

// A constraint that exists in the real package metadata but that the
// current Nexus component model cannot represent.
//
// These are recorded rather than silently discarded. A model that
// quietly drops constraints would violate the project's own rule
// against hiding system behaviour from the user.
enum class ModelGapKind {
    PreDependency   // Pre-Depends — the model has no ordering strength
};

struct ModelGap {
    std::string componentId;
    ModelGapKind kind;
    std::string field;
    std::string detail;
};

struct DpkgSourceResult {
    std::vector<Component> components;
    std::vector<ModelGap> gaps;

    std::size_t stanzasRead = 0;
    std::size_t stanzasSkipped = 0;
    std::size_t dependencyClauses = 0;
    std::size_t representableClauses = 0;
};

// Reads installed-package metadata from a dpkg status file.
//
// This source is strictly read-only. It never invokes dpkg or apt and
// never modifies the system.
class DpkgSource {
public:
    explicit DpkgSource(
        std::string statusPath = "/var/lib/dpkg/status"
    );

    DpkgSourceResult load() const;

    // Exposed for testing against fixture data.
    DpkgSourceResult loadFromStanzas(
        const std::vector<ControlStanza>& stanzas
    ) const;

    const std::string& statusPath() const;

private:
    std::string statusPath_;
};

// Map a Debian section name onto the closest Nexus component type.
ComponentType classifySection(const std::string& section);

std::string toString(ModelGapKind kind);

}
