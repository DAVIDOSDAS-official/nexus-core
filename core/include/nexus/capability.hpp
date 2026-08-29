#pragma once

#include <string>

namespace nexus {

// A named ability a component can provide or require.
//
// A capability may carry a version. This matters for virtual
// capabilities: perl provides "libnet-perl" at version 3.15, which is
// not the same as perl's own version of 5.38.2. Without this, any
// version condition on a virtual capability is checked against the
// wrong number.
//
// An empty version means the provider did not state one.
class Capability {
public:
    explicit Capability(std::string name);

    Capability(std::string name, std::string version);

    const std::string& name() const;

    const std::string& version() const;

    bool hasVersion() const;

private:
    std::string name_;
    std::string version_;
};

}
