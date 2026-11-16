#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexus::system {

// Other systems on the same disk, and Nexus's boot menu entries for
// them (`nexus dualboot`).
//
// How it was found to work (Asus, 12 October, Nexus 0.1.43 next to
// Linux Mint 22.3):
//
//   - Nexus has its own EFI partition and /boot. Sharing the other
//     system's EFI partition breaks ostree's boot menu (see
//     image/config-installer.toml), so the other system keeps its own
//     and Nexus's menu starts it by handing over to the other system's
//     own loader on its own EFI partition ("chainloading"). Nothing of
//     the other system is changed or copied.
//   - The Asus ignores the firmware's "start this one next time"
//     (BootNext): efibootmgr --bootnext pointed at Nexus and Mint
//     started. So "restart into Mint once" goes through Nexus's own
//     boot menu (GRUB's next_entry), which the firmware cannot ignore.
//
// Everything here is pure: lsblk's output, an os-release file, a GRUB
// file in, words and text out. Mounting and writing is the CLI's.

// One line of `lsblk -J -b -o NAME,PATH,SIZE,TYPE,FSTYPE,PARTTYPE,
// UUID,LABEL,MODEL,MOUNTPOINTS,RM,TRAN`, with its children (partitions
// of a disk, the opened volume inside an encrypted partition).
struct BlockDevice {
    std::string name;       // nvme0n1p2
    std::string path;       // /dev/nvme0n1p2
    std::string type;       // disk, part, crypt, lvm...
    std::string fstype;     // ext4, vfat, crypto_LUKS...
    std::string parttype;   // GPT type GUID, lower case
    std::string uuid;       // filesystem UUID
    std::string label;
    std::string model;      // disks only
    std::uint64_t size = 0; // bytes
    std::vector<std::string> mountpoints;
    std::vector<BlockDevice> children;

    // RM and TRAN (0.2.0, ledger item 10): whether the disk can be
    // taken out, and how it is connected.
    bool removable = false;       // RM: true, 1, "1" or "true"
    bool removableKnown = false;  // false: this lsblk gave no RM for it
    std::string transport;        // usb, nvme, sata, mmc...; "" if not given
    // For a disk on the mmc bus, what the kernel calls the card
    // (/sys/block/<name>/device/type): "SD" for a card in a slot, "MMC"
    // for storage soldered to the board. lsblk reports both as mmc and
    // not removable, so the CLI reads it and puts it here.
    std::string cardType;
};

std::vector<BlockDevice> parseLsblk(
    const std::string& json,
    std::string& error
);

// The GPT type of an EFI system partition.
bool isEfiPartition(const BlockDevice& device);

// ext4, xfs, btrfs...: a filesystem a Linux system lives on.
bool isLinuxFilesystem(const std::string& fstype);

// Partitions of the disks that hold a Linux filesystem, or are
// encrypted, and are not part of the running system or an EFI
// partition: where another system's root can be.
std::vector<const BlockDevice*> otherPartitions(
    const std::vector<BlockDevice>& devices
);

// Whether this device, or anything inside it, is mounted -- that is,
// whether it is part of the running system.
bool inUse(const BlockDevice& device);

// Disks that are plugged in rather than built in (0.2.0, ledger item
// 10). Until 0.1.99 a USB stick's partitions were looked into like the
// internal disk's, and a boot loader on it could end up in Nexus's boot
// menu. They are left out now unless the person names the disk.
//
// What kind of plugged-in disk this is, in words -- "USB disk", "SD
// card", "removable disk" -- or "" for one that is built in. A disk
// lsblk says nothing about (no RM, no TRAN) counts as built in; see
// DiskChoice::unknown.
std::string portableKind(const BlockDevice& disk);

struct DiskChoice {
    // The disks to look at for other systems, with their partitions.
    std::vector<BlockDevice> looked;
    // The ones left out (without their partitions: nothing in them
    // was read).
    std::vector<BlockDevice> skipped;
    // Disks lsblk gave no RM for: they were taken to be built in.
    std::vector<std::string> unknown;
    // Named with --disk, and no disk or partition here has that path.
    std::vector<std::string> notFound;
};

// Splits lsblk's list. A plugged-in disk is looked at only if `named`
// holds its path or the path of something on it ("/dev/sda",
// "/dev/sda1"), or if it is the disk the running system is on
// (thisDisk: Nexus installed on a USB disk still sees its neighbours).
DiskChoice chooseDisks(const std::vector<BlockDevice>& devices,
                       const std::vector<std::string>& named,
                       const std::string& thisDisk);

// What to say about the disks that were left out, and how to include
// them; empty when there is nothing to say. Each string is one line.
std::vector<std::string> skippedDiskLines(const DiskChoice& choice);

// /etc/os-release, or /usr/lib/os-release.
struct OsRelease {
    std::string id;          // linuxmint
    std::string name;        // Linux Mint
    std::string version;     // 22.3 (Zena)
    std::string prettyName;  // Linux Mint 22.3
};

OsRelease parseOsRelease(const std::string& text);

// What to call a system: PRETTY_NAME, else NAME VERSION, else the id.
std::string systemName(const OsRelease& release);

// The partition a GRUB stub on an EFI partition points at -- the
// "search.fs_uuid <uuid> root" (Ubuntu, Mint) or
// "search --fs-uuid --set=dev <uuid>" (Fedora) line. That is the
// system's root or its /boot. Empty when there is none.
std::string grubStubTarget(const std::string& grubCfg);

// A program the firmware can start, on an EFI partition.
// What may be written into GRUB's own script from another system's
// boot partition (0.1.84, outside review: a folder named
// "x; }; set timeout=0; ..." on another ESP became GRUB commands run at
// every boot). A loader path: /EFI/... of letters, digits and . _ -;
// a filesystem UUID: hex digits and -. Anything else is left out of
// the menu, never escaped and hoped for.
bool isSafeLoaderPath(const std::string& path);
bool isSafeFsUuid(const std::string& uuid);

struct EfiLoader {
    std::string partition;   // /dev/nvme0n1p1
    std::string espUuid;     // 8687-F76C
    std::string directory;   // ubuntu
    std::string file;        // /EFI/ubuntu/shimx64.efi
    std::string target;      // grubStubTarget of its grub.cfg, if any
};

// The loader to start in one EFI/<directory>, given the files in it
// (names as listed, any case): shim first -- it is what carries the
// Secure Boot signature the firmware trusts -- then GRUB, then
// systemd-boot. Empty when none.
std::string pickLoader(const std::vector<std::string>& files);

// Directories that are not a system: the firmware's fallback (BOOT)
// and vendor tools some laptops ship.
bool skippedEfiDirectory(const std::string& directory);

// One system Nexus can offer to start.
struct OtherSystem {
    int number = 0;              // 1, 2... in the order shown
    std::string name;            // "Linux Mint 22.3", or a description
    std::string partition;       // its root, if found
    std::string fstype;
    std::uint64_t size = 0;
    bool encrypted = false;
    bool named = false;          // name read from its os-release
    EfiLoader loader;            // empty file: cannot be started from here
    std::string entryId;         // GRUB --id, from the loader
};

// The GRUB menu id for a loader: "nexus-dualboot-ubuntu". Letters,
// digits and dashes only.
std::string entryIdFor(const EfiLoader& loader);

// Pair systems with the loaders that start them: by the partition a
// loader's grub.cfg names, then -- one of each left -- with each
// other. Loaders left over become systems of their own, named after
// their directory. Numbers them in the order given.
std::vector<OtherSystem> matchSystems(
    std::vector<OtherSystem> systems,
    const std::vector<EfiLoader>& loaders,
    const std::vector<BlockDevice>& partitions
);

// One GRUB menu entry that hands over to the system's own loader.
std::string grubEntry(const OtherSystem& system);

// The part of /boot/grub2/custom.cfg that Nexus owns, between these
// lines. Anything else in the file is somebody's and stays.
extern const char* const kBlockBegin;
extern const char* const kBlockEnd;

std::string grubBlock(const std::vector<OtherSystem>& systems);

// existing with Nexus's block replaced by block (or added, or -- block
// empty -- taken out).
std::string replaceGrubBlock(const std::string& existing,
                             const std::string& block);

// The entry ids already in Nexus's block of custom.cfg.
std::vector<std::string> entryIdsInBlock(const std::string& customCfg);

// "95.4 GiB" -- binary, like lsblk and the installer.
std::string sizeInWords(std::uint64_t bytes);

}  // namespace nexus::system
