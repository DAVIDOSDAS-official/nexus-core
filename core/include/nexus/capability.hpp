#pragma once

#include <string>

namespace nexus {

class Capability {
public:
    explicit Capability(std::string name);

    const std::string& name() const;

private:
    std::string name_;
};

}
