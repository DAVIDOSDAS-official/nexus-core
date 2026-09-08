#pragma once

#include <string>

#include <nexus/hardware/hardware.hpp>
#include <vector>

namespace nexus::hardware {

// What Secure Boot is doing, and what could be done about it.
//
// The usual advice for a custom distribution is to turn Secure Boot
// off. That is the wrong answer given to everybody because the right
// answer is unfamiliar: you can enrol your own key, and millions of
// people already have without knowing -- it is what happens when
// Ubuntu builds an NVIDIA driver.
struct SecureBootReport {
    SecureBootState state = SecureBootState::Unknown;

    // Keys the owner of this machine has enrolled, as opposed to the
    // ones the manufacturer shipped.
    bool ownerKeysEnrolled = false;
    std::size_t enrolledKeys = 0;

    bool efiPresent = false;

    std::vector<std::string> unreadable;

    // Whether an unsigned kernel could boot as things stand.
    bool wouldRefuseUnsigned() const {
        return state == SecureBootState::Enabled;
    }

    std::vector<std::string> capabilities() const;
};

class SecureBootDetector {
public:
    explicit SecureBootDetector(std::string root = "/");

    SecureBootReport detect() const;

    // Exposed for testing: EFI variables carry a four-byte attribute
    // header before their value, so the byte that matters is the
    // fifth.
    static SecureBootState stateFromVariable(const std::string& bytes);

private:
    std::string root_;
};



}
