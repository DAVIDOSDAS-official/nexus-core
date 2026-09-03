#include <gtest/gtest.h>
#include <algorithm>

#include <filesystem>
#include <fstream>

#include <nexus/hardware/encryption.hpp>

using nexus::hardware::Encrypted;
using nexus::hardware::EncryptionDetector;
using nexus::hardware::EncryptionReport;

namespace {

// A throwaway /proc and /sys. Disks cannot be arranged on demand, so
// the tests build the machine they want to examine.
class FakeSystem {
public:
    FakeSystem() {
        root_ = std::filesystem::temp_directory_path() /
                ("nexus-crypt-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter_++));

        std::filesystem::create_directories(root_ / "proc");
    }

    ~FakeSystem() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    // A device-mapper node. uuid starting with CRYPT- means LUKS;
    // slaves are what it is stacked on.
    void mapper(
        const std::string& node,
        const std::string& name,
        const std::string& uuid,
        const std::vector<std::string>& slaves = {}
    ) {
        const auto directory = root_ / "sys/block" / node / "dm";

        std::filesystem::create_directories(directory);

        write(directory / "name", name);
        write(directory / "uuid", uuid);

        for (const std::string& slave : slaves) {
            std::filesystem::create_directories(
                root_ / "sys/block" / node / "slaves" / slave);
        }
    }

    void mounts(const std::string& text) {
        write(root_ / "proc/mounts", text);
    }

    void swaps(const std::string& text) {
        write(root_ / "proc/swaps", text);
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

int FakeSystem::counter_ = 0;

const char* kSwapHeader = "Filename\tType\tSize\tUsed\tPriority\n";

}

TEST(EncryptionTest, APlainDiskIsNotEncrypted) {
    FakeSystem system;

    system.mounts("/dev/nvme0n1p2 / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_EQ(report.root, Encrypted::No);
    EXPECT_TRUE(report.capabilities().empty());
}

TEST(EncryptionTest, ALuksRootIsEncrypted) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef-cryptroot");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_EQ(report.root, Encrypted::Yes);
    EXPECT_EQ(report.method, "luks");
}

// The common layout, and the one a naive check gets wrong: the
// mounted device is a logical volume whose own uuid says nothing
// about encryption. Following the stack is the whole point.
TEST(EncryptionTest, LvmOnLuksIsEncrypted) {
    FakeSystem system;

    system.mapper("dm-0", "cryptdata", "CRYPT-LUKS2-abcdef-cryptdata");
    system.mapper("dm-1", "data-root", "LVM-xxxx", {"dm-0"});

    system.mounts("/dev/mapper/data-root / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_EQ(report.root, Encrypted::Yes);
}

TEST(EncryptionTest, LvmOnAPlainDiskIsNot) {
    FakeSystem system;

    system.mapper("dm-0", "data-root", "LVM-xxxx");
    system.mounts("/dev/mapper/data-root / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    EXPECT_EQ(
        EncryptionDetector(system.path()).detect().root,
        Encrypted::No);
}

// eCryptfs encrypts a home directory on top of an ordinary
// filesystem, so the device says nothing and the type says
// everything.
TEST(EncryptionTest, AnEcryptfsHomeIsEncrypted) {
    FakeSystem system;

    system.mounts(
        "/dev/nvme0n1p2 / ext4 rw 0 0\n"
        "/home/.ecryptfs/user/.Private /home ecryptfs rw 0 0\n");
    system.swaps(kSwapHeader);

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_EQ(report.root, Encrypted::No);
    EXPECT_EQ(report.home, Encrypted::Yes);
    EXPECT_TRUE(report.homeIsSeparate);
    EXPECT_EQ(report.method, "ecryptfs");
}

TEST(EncryptionTest, AHomeInsideRootIsAsEncryptedAsRoot) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_FALSE(report.homeIsSeparate);
    EXPECT_EQ(report.home, Encrypted::Yes);
}

// The gap almost nobody notices: memory contents written to disk in
// plain text beside an encrypted root.
TEST(EncryptionTest, EncryptedRootWithPlainSwapIsReported) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(
        std::string(kSwapHeader) +
        "/dev/nvme0n1p3 partition 8388604 0 -2\n");

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_EQ(report.root, Encrypted::Yes);
    EXPECT_TRUE(report.hasSwap);
    EXPECT_EQ(report.swap, Encrypted::No);
    EXPECT_TRUE(report.swapLeaksMemory());
}

TEST(EncryptionTest, EncryptedSwapDoesNotLeak) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mapper("dm-1", "cryptswap", "CRYPT-LUKS2-fedcba");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(
        std::string(kSwapHeader) +
        "/dev/mapper/cryptswap partition 8388604 0 -2\n");

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_FALSE(report.swapLeaksMemory());
    EXPECT_EQ(report.swap, Encrypted::Yes);
}

// Several swap areas: the weakest one is what matters.
TEST(EncryptionTest, OnePlainSwapAmongSeveralStillLeaks) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mapper("dm-1", "cryptswap", "CRYPT-LUKS2-fedcba");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(
        std::string(kSwapHeader) +
        "/dev/mapper/cryptswap partition 4194304 0 -2\n"
        "/dev/nvme0n1p3 partition 4194304 0 -3\n");

    EXPECT_TRUE(
        EncryptionDetector(system.path()).detect().swapLeaksMemory());
}

TEST(EncryptionTest, NoSwapIsNotALeak) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_FALSE(report.hasSwap);
    EXPECT_FALSE(report.swapLeaksMemory());
}

TEST(EncryptionTest, AnUnreadableSystemSaysSoRatherThanGuessing) {
    const EncryptionReport report =
        EncryptionDetector("/nonexistent").detect();

    EXPECT_EQ(report.root, Encrypted::Unknown);
    EXPECT_FALSE(report.unreadable.empty());
}

TEST(EncryptionTest, EncryptionBecomesCapabilities) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(kSwapHeader);

    const auto names =
        EncryptionDetector(system.path()).detect().capabilities();

    EXPECT_NE(
        std::find(names.begin(), names.end(), "encrypted-root"),
        names.end());
}

// zram is compressed swap held in RAM. It never reaches a disk, so it
// cannot leak to one -- and counting it as unencrypted swap reported
// a machine whose real swap was inside the LUKS container as leaking.
TEST(EncryptionTest, ZramIsNotADiskLeak) {
    FakeSystem system;

    system.mapper("dm-0", "cryptdata", "CRYPT-LUKS2-abcdef");
    system.mapper("dm-2", "data-swap", "LVM-yyyy", {"dm-0"});
    system.mounts("/dev/mapper/data-root / ext4 rw 0 0\n");
    system.mapper("dm-1", "data-root", "LVM-xxxx", {"dm-0"});
    system.swaps(
        std::string(kSwapHeader) +
        "/dev/dm-2 partition 4194304 0 -1\n"
        "/dev/zram0 partition 16000000 0 1000\n");

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_EQ(report.root, Encrypted::Yes);
    EXPECT_TRUE(report.hasZramSwap);
    EXPECT_EQ(report.swap, Encrypted::Yes);
    EXPECT_FALSE(report.swapLeaksMemory());
}

// And zram alongside a genuinely plain partition still leaks, because
// the partition does.
TEST(EncryptionTest, ZramDoesNotExcusePlainDiskSwap) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(
        std::string(kSwapHeader) +
        "/dev/zram0 partition 16000000 0 1000\n"
        "/dev/nvme0n1p4 partition 4194304 0 -2\n");

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_TRUE(report.hasZramSwap);
    EXPECT_TRUE(report.swapLeaksMemory());
}

// Swap entirely in RAM is not a disk question at all.
TEST(EncryptionTest, ZramOnlyMeansNoDiskSwap) {
    FakeSystem system;

    system.mapper("dm-0", "cryptroot", "CRYPT-LUKS2-abcdef");
    system.mounts("/dev/mapper/cryptroot / ext4 rw 0 0\n");
    system.swaps(
        std::string(kSwapHeader) +
        "/dev/zram0 partition 16000000 0 1000\n");

    const EncryptionReport report =
        EncryptionDetector(system.path()).detect();

    EXPECT_FALSE(report.hasSwap);
    EXPECT_TRUE(report.hasZramSwap);
    EXPECT_FALSE(report.swapLeaksMemory());
}
