#pragma once

#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::hardware {

// A graphics device found on this machine.
struct GraphicsDevice {
    std::string node;          // card0, card1
    std::string vendorId;      // 0x1002
    std::string deviceId;      // 0x73df
    std::string vendor;        // amd, intel, nvidia, virtual, unknown
    std::string driver;        // amdgpu, i915, nouveau, nvidia, ...
};

enum class FirmwareMode {
    Unknown,
    Bios,
    Uefi
};

enum class SecureBootState {
    Unknown,
    Disabled,
    Enabled
};

// What was found, and what it means as capabilities.
//
// Facts and capabilities are kept apart deliberately. A fact is what
// the machine reports; a capability is what the resolver can match
// against. Mixing them would make it impossible to tell a reading
// from an interpretation.
struct HardwareInfo {
    std::vector<GraphicsDevice> graphics;

    FirmwareMode firmware = FirmwareMode::Unknown;
    SecureBootState secureBoot = SecureBootState::Unknown;

    bool hasBattery = false;
    bool hasGamepad = false;

    std::string cpuModel;
    std::string architecture;

    // Anything that could not be read, with the reason. Never
    // silently empty: a machine with no readable GPU and a machine
    // with no GPU must not look the same.
    std::vector<std::string> unreadable;

    // Capability names derived from the above, e.g.
    //   gpu-vendor-amd, gpu-driver-amdgpu, firmware-uefi,
    //   secure-boot-enabled, chassis-laptop, gamepad-present
    //
    // Hyphens, not colons. These names are written in profile
    // Requires: fields, which use Debian dependency grammar, and
    // there a colon introduces an architecture qualifier -- so
    // "gpu-vendor:amd" would parse as package gpu-vendor at
    // architecture amd.
    std::vector<std::string> capabilities() const;
};

// Reads hardware facts from sysfs.
//
// The root is configurable so the detector can be pointed at a
// fixture tree in tests. Nothing here writes, executes, or probes;
// it only reads files the kernel already exposes.
class HardwareDetector {
public:
    explicit HardwareDetector(std::string root = "/");

    HardwareInfo detect() const;

    const std::string& root() const;

private:
    std::string root_;
};

// The detected hardware as a component, so that a profile can require
// a capability like "gpu-vendor-amd" and have the solver resolve it
// exactly the way it resolves everything else.
Component asComponent(const HardwareInfo& info);

std::string toString(FirmwareMode mode);
std::string toString(SecureBootState state);

// PCI vendor id -> short name. Returns "unknown" for ids not listed.
std::string vendorFromPciId(const std::string& id);

}
