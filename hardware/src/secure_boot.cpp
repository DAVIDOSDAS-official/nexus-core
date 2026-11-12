#include <nexus/hardware/secure_boot.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace nexus::hardware {

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

    // By full name, GUID included (0.1.84, outside review: a prefix
    // match could read another vendor's "SecureBoot" variable).
    const std::string kGlobal = "-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    const std::string kShim = "-605dab50-e046-4300-abb6-3dd810dd8b23";
    auto exact = [&](const std::string& name) {
        std::ifstream in(efivars / (name), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const std::string secureBoot = exact("SecureBoot" + kGlobal);

    if (secureBoot.empty()) {
        report.unreadable.push_back(
            "SecureBoot variable is not present");
        return report;
    }

    report.state = stateFromVariable(secureBoot);

    // Setup mode means the firmware will accept new keys without a
    // password, which is how a key gets enrolled in the first place.
    const std::string setupMode = exact("SetupMode" + kGlobal);

    if (!setupMode.empty() &&
        stateFromVariable(setupMode) == SecureBootState::Enabled) {
        report.state = SecureBootState::SetupMode;
    }

    // MokListRT: shim's key list. Since shim 15.3 it also carries the
    // distribution's own built-in key, so a non-empty list does NOT
    // mean the owner enrolled one (0.1.84, outside review). Kept as
    // "a MOK list is present"; `mokutil --list-enrolled` says whose.
    const std::string mok = exact("MokListRT" + kShim);

    if (mok.size() > 4) {
        report.ownerKeysEnrolled = true;
        report.enrolledKeys = 1;
    }

    // MokSBStateRT = 1: shim's checking was turned off with
    // `mokutil --disable-validation` -- the firmware checks shim, and
    // shim then checks nothing.
    const std::string shimState = exact("MokSBStateRT" + kShim);
    if (shimState.size() >= 5 && shimState[4] == 1) {
        report.shimValidationDisabled = true;
    }

    return report;
}

}
