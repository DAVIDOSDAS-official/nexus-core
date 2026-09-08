#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <nexus/hardware/secure_boot.hpp>

using nexus::hardware::SecureBootDetector;
using nexus::hardware::SecureBootState;

namespace {

class FakeFirmware {
public:
    FakeFirmware() {
        root_ = std::filesystem::temp_directory_path() /
                ("nexus-sb-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter_++));

        efivars_ = root_ / "sys/firmware/efi/efivars";
    }

    ~FakeFirmware() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    void withEfi() {
        std::filesystem::create_directories(efivars_);
    }

    // EFI variables carry four attribute bytes before the value.
    void variable(const std::string& name, char value) {
        withEfi();

        const std::string file =
            name + "-8be4df61-93ca-11d2-aa0d-00e098032b8c";

        std::ofstream out(efivars_ / file, std::ios::binary);

        const char header[4] = {0x06, 0x00, 0x00, 0x00};

        out.write(header, 4);
        out.write(&value, 1);
    }

    void mokList() {
        withEfi();

        std::ofstream out(
            efivars_ / "MokListRT-605dab50-e046-4300-abb6-3dd810dd8b23",
            std::ios::binary);

        const char content[16] = {0x06, 0, 0, 0, 'k', 'e', 'y', 'd',
                                  'a', 't', 'a', 0, 0, 0, 0, 0};

        out.write(content, 16);
    }

    std::string path() const {
        return root_.string();
    }

private:
    std::filesystem::path root_;
    std::filesystem::path efivars_;
    static int counter_;
};

int FakeFirmware::counter_ = 0;

}

// Four bytes of attributes come before the value. Reading the first
// byte gives the attributes rather than the answer, and reports every
// machine as having Secure Boot off.
TEST(SecureBootTest, TheValueIsPastTheAttributeHeader) {
    EXPECT_EQ(
        SecureBootDetector::stateFromVariable(
            std::string("\x06\x00\x00\x00\x01", 5)),
        SecureBootState::Enabled);

    EXPECT_EQ(
        SecureBootDetector::stateFromVariable(
            std::string("\x06\x00\x00\x00\x00", 5)),
        SecureBootState::Disabled);
}

TEST(SecureBootTest, ATruncatedVariableIsUnknownNotDisabled) {
    EXPECT_EQ(SecureBootDetector::stateFromVariable("\x06\x00"),
              SecureBootState::Unknown);
    EXPECT_EQ(SecureBootDetector::stateFromVariable(""),
              SecureBootState::Unknown);
}

// A BIOS machine has no EFI variables. That is not a failure to read.
TEST(SecureBootTest, NoEfiMeansNotSupported) {
    FakeFirmware firmware;

    const auto report =
        SecureBootDetector(firmware.path()).detect();

    EXPECT_EQ(report.state, SecureBootState::NotSupported);
    EXPECT_FALSE(report.efiPresent);
    EXPECT_TRUE(report.unreadable.empty());
}

TEST(SecureBootTest, ReadsAnEnabledMachine) {
    FakeFirmware firmware;

    firmware.variable("SecureBoot", 1);

    const auto report =
        SecureBootDetector(firmware.path()).detect();

    EXPECT_EQ(report.state, SecureBootState::Enabled);
    EXPECT_TRUE(report.efiPresent);
    EXPECT_TRUE(report.wouldRefuseUnsigned());
}

TEST(SecureBootTest, ReadsADisabledMachine) {
    FakeFirmware firmware;

    firmware.variable("SecureBoot", 0);

    const auto report =
        SecureBootDetector(firmware.path()).detect();

    EXPECT_EQ(report.state, SecureBootState::Disabled);
    EXPECT_FALSE(report.wouldRefuseUnsigned());
}

// Setup mode means the firmware will take a new key without a
// password, which is how one gets enrolled in the first place.
TEST(SecureBootTest, SetupModeIsDistinctFromEnabled) {
    FakeFirmware firmware;

    firmware.variable("SecureBoot", 1);
    firmware.variable("SetupMode", 1);

    EXPECT_EQ(SecureBootDetector(firmware.path()).detect().state,
              SecureBootState::SetupMode);
}

// An owner's key is a different thing from the manufacturer's.
TEST(SecureBootTest, DetectsAnEnrolledOwnerKey) {
    FakeFirmware firmware;

    firmware.variable("SecureBoot", 1);

    EXPECT_FALSE(
        SecureBootDetector(firmware.path()).detect().ownerKeysEnrolled);

    firmware.mokList();

    EXPECT_TRUE(
        SecureBootDetector(firmware.path()).detect().ownerKeysEnrolled);
}

TEST(SecureBootTest, EfiWithoutTheVariableSaysSo) {
    FakeFirmware firmware;

    firmware.withEfi();

    const auto report =
        SecureBootDetector(firmware.path()).detect();

    EXPECT_TRUE(report.efiPresent);
    EXPECT_FALSE(report.unreadable.empty());
}

TEST(SecureBootTest, StateBecomesCapabilities) {
    FakeFirmware firmware;

    firmware.variable("SecureBoot", 1);
    firmware.mokList();

    const auto names =
        SecureBootDetector(firmware.path()).detect().capabilities();

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "secure-boot-enabled");
    EXPECT_EQ(names[1], "secure-boot-owner-keys");
}
