#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nexus/hardware/hardware.hpp>
#include <nexus/solver.hpp>
#include <nexus/system/version.hpp>

using nexus::hardware::asComponent;
using nexus::hardware::FirmwareMode;
using nexus::hardware::HardwareDetector;
using nexus::hardware::HardwareInfo;
using nexus::hardware::SecureBootState;
using nexus::hardware::vendorFromPciId;

namespace {

// A throwaway sysfs tree. Real hardware cannot be arranged on demand,
// so the detector reads from a configurable root and the tests build
// the machine they want to test.
class FakeSysfs {
public:
    FakeSysfs() {
        root_ = std::filesystem::temp_directory_path() /
                ("nexus-hw-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter_++));

        std::filesystem::create_directories(root_);
    }

    ~FakeSysfs() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    void graphics(
        const std::string& card,
        const std::string& vendorId,
        const std::string& deviceId,
        const std::string& driver
    ) {
        const auto device = root_ / "sys/class/drm" / card / "device";

        std::filesystem::create_directories(device);

        write(device / "vendor", vendorId);
        write(device / "device", deviceId);

        if (!driver.empty()) {
            write(device / "driver", driver);
        }
    }

    // A connector entry, which must not be mistaken for a card.
    void connector(const std::string& name) {
        std::filesystem::create_directories(
            root_ / "sys/class/drm" / name
        );
    }

    void firmware(bool uefi) {
        std::filesystem::create_directories(root_ / "sys/firmware");

        if (uefi) {
            std::filesystem::create_directories(
                root_ / "sys/firmware/efi"
            );
        }
    }

    void secureBoot(bool enabled) {
        const auto vars = root_ / "sys/firmware/efi/efivars";

        std::filesystem::create_directories(vars);

        const char bytes[5] = {
            6, 0, 0, 0, static_cast<char>(enabled ? 1 : 0)
        };

        std::ofstream out(
            vars / "SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c",
            std::ios::binary
        );

        out.write(bytes, sizeof(bytes));
    }

    void battery() {
        std::filesystem::create_directories(
            root_ / "sys/class/power_supply/BAT0"
        );
    }

    void gamepad() {
        std::filesystem::create_directories(
            root_ / "sys/class/input/js0"
        );
    }

    void cpu(const std::string& model) {
        std::filesystem::create_directories(root_ / "proc");
        write(root_ / "proc/cpuinfo", "model name\t: " + model + "\n");
    }

    std::string path() const {
        return root_.string();
    }

private:
    static void write(
        const std::filesystem::path& file,
        const std::string& text
    ) {
        std::ofstream out(file);
        out << text;
    }

    std::filesystem::path root_;
    static int counter_;
};

int FakeSysfs::counter_ = 0;

bool has(const HardwareInfo& info, const std::string& capability) {
    const auto names = info.capabilities();

    return std::find(names.begin(), names.end(), capability) !=
           names.end();
}

}

TEST(HardwareTest, MapsPciVendorIds) {
    EXPECT_EQ(vendorFromPciId("0x1002"), "amd");
    EXPECT_EQ(vendorFromPciId("0x8086"), "intel");
    EXPECT_EQ(vendorFromPciId("0x10de"), "nvidia");
    EXPECT_EQ(vendorFromPciId("0x1af4"), "virtual");
    EXPECT_EQ(vendorFromPciId("0xbeef"), "unknown");
    EXPECT_EQ(vendorFromPciId("0X1002"), "amd");
}

TEST(HardwareTest, DetectsAnAmdCard) {
    FakeSysfs sysfs;
    sysfs.graphics("card0", "0x1002", "0x73df", "amdgpu");

    const HardwareInfo info = HardwareDetector(sysfs.path()).detect();

    ASSERT_EQ(info.graphics.size(), 1u);
    EXPECT_EQ(info.graphics[0].node, "card0");
    EXPECT_EQ(info.graphics[0].vendor, "amd");
    EXPECT_EQ(info.graphics[0].deviceId, "0x73df");
    EXPECT_EQ(info.graphics[0].driver, "amdgpu");
    EXPECT_TRUE(has(info, "gpu-vendor-amd"));
    EXPECT_TRUE(has(info, "gpu-driver-amdgpu"));
}

TEST(HardwareTest, DetectsSeveralCards) {
    FakeSysfs sysfs;
    sysfs.graphics("card0", "0x8086", "0x9a49", "i915");
    sysfs.graphics("card1", "0x10de", "0x2484", "nvidia");

    const HardwareInfo info = HardwareDetector(sysfs.path()).detect();

    ASSERT_EQ(info.graphics.size(), 2u);
    EXPECT_TRUE(has(info, "gpu-vendor-intel"));
    EXPECT_TRUE(has(info, "gpu-vendor-nvidia"));
}

// Connector directories sit alongside cards and look similar.
TEST(HardwareTest, IgnoresConnectorEntries) {
    FakeSysfs sysfs;
    sysfs.graphics("card0", "0x1002", "0x73df", "amdgpu");
    sysfs.connector("card0-DP-1");
    sysfs.connector("card0-HDMI-A-1");

    const HardwareInfo info = HardwareDetector(sysfs.path()).detect();

    EXPECT_EQ(info.graphics.size(), 1u);
}

// A machine with no readable GPU and a machine with no GPU must not
// produce the same answer.
TEST(HardwareTest, ReportsWhenGraphicsCannotBeRead) {
    FakeSysfs sysfs;

    const HardwareInfo info = HardwareDetector(sysfs.path()).detect();

    EXPECT_TRUE(info.graphics.empty());
    EXPECT_FALSE(info.unreadable.empty());
}

TEST(HardwareTest, DetectsFirmwareMode) {
    FakeSysfs bios;
    bios.firmware(false);
    EXPECT_EQ(
        HardwareDetector(bios.path()).detect().firmware,
        FirmwareMode::Bios
    );

    FakeSysfs uefi;
    uefi.firmware(true);
    EXPECT_EQ(
        HardwareDetector(uefi.path()).detect().firmware,
        FirmwareMode::Uefi
    );
}

TEST(HardwareTest, ReadsSecureBootFromTheEfiVariable) {
    FakeSysfs on;
    on.firmware(true);
    on.secureBoot(true);

    const HardwareInfo enabled = HardwareDetector(on.path()).detect();

    EXPECT_EQ(enabled.secureBoot, SecureBootState::Enabled);
    EXPECT_TRUE(has(enabled, "secure-boot-enabled"));

    FakeSysfs off;
    off.firmware(true);
    off.secureBoot(false);

    const HardwareInfo disabled =
        HardwareDetector(off.path()).detect();

    EXPECT_EQ(disabled.secureBoot, SecureBootState::Disabled);
}

TEST(HardwareTest, SecureBootIsUnknownWithoutEfi) {
    FakeSysfs sysfs;
    sysfs.firmware(false);

    const HardwareInfo info = HardwareDetector(sysfs.path()).detect();

    EXPECT_EQ(info.secureBoot, SecureBootState::Unknown);
    EXPECT_FALSE(has(info, "secure-boot-enabled"));
    EXPECT_FALSE(has(info, "secure-boot-disabled"));
}

TEST(HardwareTest, ABatteryMeansALaptop) {
    FakeSysfs laptop;
    laptop.battery();

    EXPECT_TRUE(has(
        HardwareDetector(laptop.path()).detect(),
        "chassis-laptop"
    ));

    FakeSysfs desktop;

    EXPECT_TRUE(has(
        HardwareDetector(desktop.path()).detect(),
        "chassis-desktop"
    ));
}

TEST(HardwareTest, DetectsAGamepad) {
    FakeSysfs sysfs;
    sysfs.gamepad();

    EXPECT_TRUE(
        has(HardwareDetector(sysfs.path()).detect(), "gamepad-present")
    );
}

TEST(HardwareTest, ReadsTheCpuModel) {
    FakeSysfs sysfs;
    sysfs.cpu("AMD Ryzen 7 5800X 8-Core Processor");

    EXPECT_EQ(
        HardwareDetector(sysfs.path()).detect().cpuModel,
        "AMD Ryzen 7 5800X 8-Core Processor"
    );
}

TEST(HardwareTest, DoesNotRepeatACapability) {
    FakeSysfs sysfs;
    sysfs.graphics("card0", "0x1002", "0x73df", "amdgpu");
    sysfs.graphics("card1", "0x1002", "0x164e", "amdgpu");

    const auto names = HardwareDetector(sysfs.path()).detect()
                           .capabilities();

    EXPECT_EQ(
        std::count(names.begin(), names.end(), "gpu-vendor-amd"),
        1
    );
}

// The point of the whole thing: a profile can require a hardware
// capability and the solver resolves it like any other.
TEST(HardwareTest, DetectedHardwareSatisfiesARequirement) {
    FakeSysfs sysfs;
    sysfs.graphics("card0", "0x1002", "0x73df", "amdgpu");

    const HardwareInfo info = HardwareDetector(sysfs.path()).detect();

    nexus::Solver solver(
        {asComponent(info)},
        nexus::ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        )
    );

    nexus::SolverRequest wanted;
    wanted.requirements.push_back(
        nexus::Requirement(nexus::Constraint("gpu-vendor-amd"))
    );

    EXPECT_EQ(
        solver.solve(wanted).status,
        nexus::SolverStatus::Success
    );

    nexus::SolverRequest unwanted;
    unwanted.requirements.push_back(
        nexus::Requirement(nexus::Constraint("gpu-vendor-nvidia"))
    );

    EXPECT_EQ(
        solver.solve(unwanted).status,
        nexus::SolverStatus::Unsatisfiable
    );
}
