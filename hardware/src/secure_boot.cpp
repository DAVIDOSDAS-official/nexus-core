#include <nexus/hardware/secure_boot.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace nexus::hardware {

namespace {

// EFI variables are stored with a four-byte attribute header in
// front of the value. Reading the first byte gives the attributes,
// not the answer, and reports every machine as having Secure Boot
// off.
std::string readVariable(const std::filesystem::path& directory,
                         const std::string& prefix) {
    std::error_code error;

    if (!std::filesystem::is_directory(directory, error)) {
        return "";
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(directory, error)) {

        const std::string name = entry.path().filename().string();

        if (name.rfind(prefix + "-", 0) != 0) {
            continue;
        }

        std::ifstream input(entry.path(), std::ios::binary);

        if (!input) {
            continue;
        }

        std::ostringstream buffer;

        buffer << input.rdbuf();

        return buffer.str();
    }

    return "";
}

}

std::vector<std::string> SecureBootReport::capabilities() const {
    std::vector<std::string> names;

    switch (state) {
        case SecureBootState::Enabled:
            names.push_back("secure-boot-enabled");
            break;
        case SecureBootState::Disabled:
            names.push_back("secure-boot-disabled");
            break;
        case SecureBootState::SetupMode:
            names.push_back("secure-boot-setup-mode");
            break;
        default:
            break;
    }

    if (ownerKeysEnrolled) {
        names.push_back("secure-boot-owner-keys");
    }

    return names;
}

SecureBootState SecureBootDetector::stateFromVariable(
    const std::string& bytes
) {
    // Four bytes of attributes, then one byte of value.
    if (bytes.size() < 5) {
        return SecureBootState::Unknown;
    }

    return bytes[4] == 1
        ? SecureBootState::Enabled
        : SecureBootState::Disabled;
}

SecureBootDetector::SecureBootDetector(std::string root)
    : root_(std::move(root)) {
}

SecureBootReport SecureBootDetector::detect() const {
    SecureBootReport report;

    const std::filesystem::path efivars =
        std::filesystem::path(root_) / "sys/firmware/efi/efivars";

    std::error_code error;

    if (!std::filesystem::is_directory(efivars, error)) {
        // A BIOS machine, or a container. Not a failure to read:
        // there is genuinely nothing there.
        report.state = SecureBootState::NotSupported;
        return report;
    }

    report.efiPresent = true;

    const std::string secureBoot = readVariable(efivars, "SecureBoot");

    if (secureBoot.empty()) {
        report.unreadable.push_back(
            "SecureBoot variable is not present");
        return report;
    }

    report.state = stateFromVariable(secureBoot);

    // Setup mode means the firmware will accept new keys without a
    // password, which is how a key gets enrolled in the first place.
    const std::string setupMode = readVariable(efivars, "SetupMode");

    if (!setupMode.empty() &&
        stateFromVariable(setupMode) == SecureBootState::Enabled) {
        report.state = SecureBootState::SetupMode;
    }

    // MokListRT is the machine owner's key list, as opposed to the
    // manufacturer's db. Its presence means somebody has enrolled a
    // key of their own.
    const std::string mok = readVariable(efivars, "MokListRT");

    if (mok.size() > 4) {
        report.ownerKeysEnrolled = true;
        report.enrolledKeys = 1;
    }

    return report;
}

}
