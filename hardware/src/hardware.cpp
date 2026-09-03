#include <nexus/hardware/hardware.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace nexus::hardware {

namespace {

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\n\r");

    if (begin == std::string::npos) {
        return "";
    }

    const std::size_t end = text.find_last_not_of(" \t\n\r");

    return text.substr(begin, end - begin + 1);
}

// Reads a one-line sysfs attribute. Returns false when absent.
bool readLine(
    const std::filesystem::path& path,
    std::string& value
) {
    std::ifstream input(path);

    if (!input) {
        return false;
    }

    std::string line;
    std::getline(input, line);

    value = trim(line);

    return true;
}

bool present(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

std::string lowercase(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );

    return text;
}

std::string hostArchitecture() {
#if defined(__x86_64__)
    return "amd64";
#elif defined(__i386__)
    return "i386";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__arm__)
    return "armhf";
#else
    return "";
#endif
}

}

std::string vendorFromPciId(const std::string& id) {
    const std::string normalised = lowercase(trim(id));

    if (normalised == "0x1002" || normalised == "0x1022") {
        return "amd";
    }

    if (normalised == "0x8086") {
        return "intel";
    }

    if (normalised == "0x10de") {
        return "nvidia";
    }

    // Virtual display adapters. Naming them matters: a virtual
    // machine must not look like a machine with no graphics at all.
    if (normalised == "0x1af4" ||   // virtio
        normalised == "0x15ad" ||   // VMware
        normalised == "0x1234" ||   // QEMU / Bochs
        normalised == "0x1414") {   // Hyper-V
        return "virtual";
    }

    return "unknown";
}

HardwareDetector::HardwareDetector(std::string root)
    : root_(std::move(root)) {
}

const std::string& HardwareDetector::root() const {
    return root_;
}

HardwareInfo HardwareDetector::detect() const {
    HardwareInfo info;

    const std::filesystem::path root(root_);

    info.architecture = hostArchitecture();

    // Graphics devices.
    const std::filesystem::path drm = root / "sys/class/drm";

    if (!present(drm)) {
        info.unreadable.push_back(
            "sys/class/drm is not present; no graphics detected"
        );
    } else {
        std::vector<std::filesystem::path> cards;
        std::error_code error;

        for (const auto& entry :
             std::filesystem::directory_iterator(drm, error)) {

            const std::string name = entry.path().filename().string();

            // card0, card1 -- but not card0-DP-1 connector entries.
            if (name.rfind("card", 0) != 0 ||
                name.find('-') != std::string::npos) {
                continue;
            }

            cards.push_back(entry.path());
        }

        std::sort(cards.begin(), cards.end());

        for (const std::filesystem::path& card : cards) {
            GraphicsDevice device;

            device.node = card.filename().string();

            const std::filesystem::path pci = card / "device";

            if (!readLine(pci / "vendor", device.vendorId)) {
                info.unreadable.push_back(
                    device.node + ": vendor id unreadable"
                );
                continue;
            }

            readLine(pci / "device", device.deviceId);

            device.vendor = vendorFromPciId(device.vendorId);

            // The driver is a symlink; its target's name is what we
            // want. Read it as a link first, then fall back to a
            // plain file so fixtures can be plain directories.
            std::error_code linkError;
            const std::filesystem::path driver = pci / "driver";

            if (std::filesystem::is_symlink(driver, linkError)) {
                const auto target =
                    std::filesystem::read_symlink(driver, linkError);

                if (!linkError) {
                    device.driver = target.filename().string();
                }
            } else if (present(driver)) {
                if (!readLine(driver, device.driver)) {
                    device.driver = driver.filename().string();
                }
            }

            info.graphics.push_back(std::move(device));
        }
    }

    // Firmware mode.
    if (present(root / "sys/firmware/efi")) {
        info.firmware = FirmwareMode::Uefi;
    } else if (present(root / "sys/firmware")) {
        info.firmware = FirmwareMode::Bios;
    } else {
        info.unreadable.push_back(
            "sys/firmware is not present; firmware mode unknown"
        );
    }

    // Secure Boot, from the EFI variable. The value is the fifth
    // byte; the first four are attribute flags.
    const std::filesystem::path efivars =
        root / "sys/firmware/efi/efivars";

    if (present(efivars)) {
        std::error_code error;

        for (const auto& entry :
             std::filesystem::directory_iterator(efivars, error)) {

            const std::string name = entry.path().filename().string();

            if (name.rfind("SecureBoot-", 0) != 0) {
                continue;
            }

            std::ifstream input(entry.path(), std::ios::binary);

            if (!input) {
                continue;
            }

            char bytes[5]{};

            input.read(bytes, sizeof(bytes));

            if (input.gcount() >= 5) {
                info.secureBoot = bytes[4] != 0
                    ? SecureBootState::Enabled
                    : SecureBootState::Disabled;
            }

            break;
        }
    }

    // Battery, which is how a laptop announces itself.
    const std::filesystem::path power = root / "sys/class/power_supply";

    if (present(power)) {
        std::error_code error;

        for (const auto& entry :
             std::filesystem::directory_iterator(power, error)) {

            if (entry.path().filename().string().rfind("BAT", 0) == 0) {
                info.hasBattery = true;
                break;
            }
        }
    }

    // Gamepads appear as js* under sys/class/input.
    const std::filesystem::path input = root / "sys/class/input";

    if (present(input)) {
        std::error_code error;

        for (const auto& entry :
             std::filesystem::directory_iterator(input, error)) {

            if (entry.path().filename().string().rfind("js", 0) == 0) {
                info.hasGamepad = true;
                break;
            }
        }
    }

    // CPU model, for reporting only.
    std::ifstream cpuinfo(root / "proc/cpuinfo");

    if (cpuinfo) {
        std::string line;

        while (std::getline(cpuinfo, line)) {
            const std::size_t colon = line.find(':');

            if (colon == std::string::npos) {
                continue;
            }

            if (trim(line.substr(0, colon)) == "model name") {
                info.cpuModel = trim(line.substr(colon + 1));
                break;
            }
        }
    }

    return info;
}

std::vector<std::string> HardwareInfo::capabilities() const {
    std::vector<std::string> names;

    const auto add = [&names](const std::string& name) {
        if (std::find(names.begin(), names.end(), name) ==
            names.end()) {
            names.push_back(name);
        }
    };

    for (const GraphicsDevice& device : graphics) {
        if (device.vendor != "unknown") {
            add("gpu-vendor-" + device.vendor);
        }

        if (!device.driver.empty()) {
            add("gpu-driver-" + device.driver);
        }
    }

    if (firmware == FirmwareMode::Uefi) {
        add("firmware-uefi");
    } else if (firmware == FirmwareMode::Bios) {
        add("firmware-bios");
    }

    if (secureBoot == SecureBootState::Enabled) {
        add("secure-boot-enabled");
    } else if (secureBoot == SecureBootState::Disabled) {
        add("secure-boot-disabled");
    }

    add(hasBattery ? "chassis-laptop" : "chassis-desktop");

    if (hasGamepad) {
        add("gamepad-present");
    }

    if (!architecture.empty()) {
        add("cpu-arch-" + architecture);
    }

    return names;
}

Component asComponent(const HardwareInfo& info) {
    Component component(
        "system-hardware",
        "system-hardware",
        "0",
        ComponentType::Driver
    );

    // Arch independent: it describes the machine rather than running
    // on any particular architecture.
    component.setArchitecture(kArchitectureAll);
    component.setSource(Source::Detected);

    for (const std::string& name : info.capabilities()) {
        component.addProvidedCapability(Capability(name));
    }

    return component;
}

std::string toString(FirmwareMode mode) {
    switch (mode) {
        case FirmwareMode::Bios:
            return "bios";
        case FirmwareMode::Uefi:
            return "uefi";
        case FirmwareMode::Unknown:
            return "unknown";
    }

    return "unknown";
}

std::string toString(SecureBootState state) {
    switch (state) {
        case SecureBootState::Enabled:
            return "enabled";
        case SecureBootState::Disabled:
            return "disabled";
        case SecureBootState::Unknown:
            return "unknown";
    }

    return "unknown";
}

}
