#include <gtest/gtest.h>

#include <nexus/system/dualboot.hpp>

using namespace nexus::system;

// The Asus on 12 October: Linux Mint 22.3 on p1 (its EFI partition)
// and p2, Nexus 0.1.43 on p3 (EFI), p4 (/boot) and p5 (encrypted),
// as `lsblk -J -b -o NAME,PATH,SIZE,TYPE,FSTYPE,PARTTYPE,UUID,LABEL,
// MODEL,MOUNTPOINTS` writes it. UUIDs of p2/p3 as seen in efibootmgr
// and the Mint install.
static const char* kAsus = R"({
   "blockdevices": [
      {"name":"zram0", "path":"/dev/zram0", "size":3904897024, "type":"disk",
       "fstype":"swap", "parttype":null, "uuid":"z", "label":"zram0",
       "model":null, "mountpoints":["[SWAP]"]},
      {"name":"nvme0n1", "path":"/dev/nvme0n1", "size":256060514304,
       "type":"disk", "fstype":null, "parttype":null, "uuid":null,
       "label":null, "model":"WDC PC SN520 SDAPNUW-256G-1202  ",
       "mountpoints":[null],
       "children": [
          {"name":"nvme0n1p1", "path":"/dev/nvme0n1p1", "size":510656512,
           "type":"part", "fstype":"vfat",
           "parttype":"C12A7328-F81F-11D2-BA4B-00A0C93EC93B",
           "uuid":"8687-F76C", "label":null, "model":null,
           "mountpoints":[null]},
          {"name":"nvme0n1p2", "path":"/dev/nvme0n1p2", "size":102400000000,
           "type":"part", "fstype":"ext4",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"5b3f6a52-1111-4c2e-9d55-7a1f0c9e2b10", "label":null,
           "model":null, "mountpoints":[null]},
          {"name":"nvme0n1p3", "path":"/dev/nvme0n1p3", "size":629145600,
           "type":"part", "fstype":"vfat",
           "parttype":"c12a7328-f81f-11d2-ba4b-00a0c93ec93b",
           "uuid":"2C1A-77B0", "label":null, "model":null,
           "mountpoints":["/boot/efi"]},
          {"name":"nvme0n1p4", "path":"/dev/nvme0n1p4", "size":2147483648,
           "type":"part", "fstype":"ext4",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"b-uuid", "label":null, "model":null,
           "mountpoints":["/boot"]},
          {"name":"nvme0n1p5", "path":"/dev/nvme0n1p5", "size":150323855360,
           "type":"part", "fstype":"crypto_LUKS",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"a81ee9e0-8c9d-4e5c-a343-4422bff9037e", "label":null,
           "model":null, "mountpoints":[null],
           "children": [
              {"name":"luks-a81ee9e0", "path":"/dev/mapper/luks-a81ee9e0",
               "size":150307078144, "type":"crypt", "fstype":"xfs",
               "parttype":null, "uuid":"x", "label":null, "model":null,
               "mountpoints":["/var", "/sysroot", "/etc"]}
           ]}
       ]}
   ]
})";

static const char* kMintStub =
    "search.fs_uuid 5b3f6a52-1111-4c2e-9d55-7a1f0c9e2b10 root \n"
    "set prefix=($root)'/boot/grub'\n"
    "configfile $prefix/grub.cfg\n";

static const char* kFedoraStub =
    "search --no-floppy --root-dev-only --fs-uuid --set=dev b-uuid\n"
    "set prefix=($dev)/grub2\n"
    "export $prefix\n"
    "configfile $prefix/grub.cfg\n";

TEST(Dualboot, ReadsLsblk) {
    std::string error;
    const auto devices = parseLsblk(kAsus, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(devices.size(), 2u);
    const auto& disk = devices[1];
    EXPECT_EQ(disk.model, "WDC PC SN520 SDAPNUW-256G-1202");
    ASSERT_EQ(disk.children.size(), 5u);
    EXPECT_TRUE(isEfiPartition(disk.children[0]));  // upper case GUID
    EXPECT_TRUE(disk.children[0].mountpoints.empty());  // [null]
    EXPECT_EQ(disk.children[1].size, 102400000000u);
    EXPECT_TRUE(inUse(disk.children[4]));  // through the opened volume
    EXPECT_FALSE(inUse(disk.children[1]));
}

TEST(Dualboot, BrokenLsblkSaysSo) {
    std::string error;
    EXPECT_TRUE(parseLsblk("{\"blockdevices\": [", error).empty());
    EXPECT_FALSE(error.empty());
}

TEST(Dualboot, OtherPartitionsAreMintsRootOnly) {
    std::string error;
    const auto devices = parseLsblk(kAsus, error);
    const auto others = otherPartitions(devices);
    ASSERT_EQ(others.size(), 1u);
    EXPECT_EQ(others[0]->path, "/dev/nvme0n1p2");
}

TEST(Dualboot, OsRelease) {
    const auto mint = parseOsRelease(
        "NAME=\"Linux Mint\"\n"
        "VERSION=\"22.3 (Zena)\"\n"
        "ID=linuxmint\n"
        "# a comment\n"
        "PRETTY_NAME=\"Linux Mint 22.3\"\n");
    EXPECT_EQ(mint.id, "linuxmint");
    EXPECT_EQ(systemName(mint), "Linux Mint 22.3");

    const auto bare = parseOsRelease("NAME='Made Up OS'\nVERSION=1\n");
    EXPECT_EQ(systemName(bare), "Made Up OS 1");

    EXPECT_EQ(systemName(parseOsRelease("ID=arch\n")), "arch");
}

TEST(Dualboot, GrubStubTargets) {
    EXPECT_EQ(grubStubTarget(kMintStub),
              "5b3f6a52-1111-4c2e-9d55-7a1f0c9e2b10");
    EXPECT_EQ(grubStubTarget(kFedoraStub), "b-uuid");
    EXPECT_EQ(grubStubTarget("search --file /x\nconfigfile y\n"), "");
    EXPECT_EQ(grubStubTarget(""), "");
}

TEST(Dualboot, PicksShimFirst) {
    EXPECT_EQ(pickLoader({"grubx64.efi", "mmx64.efi", "shimx64.efi"}),
              "shimx64.efi");
    EXPECT_EQ(pickLoader({"GRUBX64.EFI", "grub.cfg"}), "GRUBX64.EFI");
    EXPECT_EQ(pickLoader({"systemd-bootx64.efi"}), "systemd-bootx64.efi");
    EXPECT_EQ(pickLoader({"grub.cfg", "BOOTX64.CSV"}), "");
    EXPECT_TRUE(skippedEfiDirectory("BOOT"));
    EXPECT_TRUE(skippedEfiDirectory("Microsoft"));
    EXPECT_FALSE(skippedEfiDirectory("ubuntu"));
}

TEST(Dualboot, MatchesMintToItsLoader) {
    std::string error;
    const auto devices = parseLsblk(kAsus, error);

    OtherSystem mint;
    mint.name = "Linux Mint 22.3";
    mint.partition = "/dev/nvme0n1p2";
    mint.named = true;

    EfiLoader ubuntu;
    ubuntu.partition = "/dev/nvme0n1p1";
    ubuntu.espUuid = "8687-F76C";
    ubuntu.directory = "ubuntu";
    ubuntu.file = "/EFI/ubuntu/shimx64.efi";
    ubuntu.target = grubStubTarget(kMintStub);

    const auto systems = matchSystems({mint}, {ubuntu}, devices);
    ASSERT_EQ(systems.size(), 1u);
    EXPECT_EQ(systems[0].number, 1);
    EXPECT_EQ(systems[0].loader.file, "/EFI/ubuntu/shimx64.efi");
    EXPECT_EQ(systems[0].entryId, "nexus-dualboot-ubuntu");
}

TEST(Dualboot, OneOfEachPairEvenWithoutAMatch) {
    // A system with a separate /boot: its stub names /boot, not root.
    OtherSystem fedora;
    fedora.name = "Fedora Linux 44";
    fedora.partition = "/dev/sda3";
    EfiLoader loader;
    loader.directory = "fedora";
    loader.file = "/EFI/fedora/shimx64.efi";
    loader.target = "some-boot-uuid";
    const auto systems = matchSystems({fedora}, {loader}, {});
    ASSERT_EQ(systems.size(), 1u);
    EXPECT_EQ(systems[0].loader.directory, "fedora");
}

TEST(Dualboot, TwoUnmatchedAreNotGuessed) {
    OtherSystem a, b;
    a.name = "A";
    a.partition = "/dev/sda2";
    b.name = "B";
    b.partition = "/dev/sda3";
    EfiLoader x, y;
    x.directory = "x";
    x.file = "/EFI/x/grubx64.efi";
    y.directory = "y";
    y.file = "/EFI/y/grubx64.efi";
    const auto systems = matchSystems({a, b}, {x, y}, {});
    // A and B without loaders (no number), and the two loaders as
    // systems of their own, numbered 1 and 2.
    ASSERT_EQ(systems.size(), 4u);
    EXPECT_EQ(systems[0].number, 0);
    EXPECT_EQ(systems[1].number, 0);
    EXPECT_EQ(systems[2].number, 1);
    EXPECT_EQ(systems[2].name, "the system started by EFI/x");
    EXPECT_EQ(systems[3].number, 2);
}

TEST(Dualboot, SameDirectoryTwiceGetsTwoIds) {
    EfiLoader one, two;
    one.directory = two.directory = "ubuntu";
    one.file = two.file = "/EFI/ubuntu/shimx64.efi";
    const auto systems = matchSystems({}, {one, two}, {});
    ASSERT_EQ(systems.size(), 2u);
    EXPECT_EQ(systems[0].entryId, "nexus-dualboot-ubuntu");
    EXPECT_EQ(systems[1].entryId, "nexus-dualboot-ubuntu-2");
}

TEST(Dualboot, GrubEntryChainloads) {
    OtherSystem mint;
    mint.name = "Linux Mint 22.3 'Zena'";
    mint.entryId = "nexus-dualboot-ubuntu";
    mint.loader.espUuid = "8687-F76C";
    mint.loader.file = "/EFI/ubuntu/shimx64.efi";
    const std::string entry = grubEntry(mint);
    EXPECT_NE(entry.find("menuentry 'Linux Mint 22.3 '\\''Zena'\\''' --id "
                         "nexus-dualboot-ubuntu {"),
              std::string::npos)
        << entry;
    EXPECT_NE(entry.find("search --no-floppy --fs-uuid --set=root 8687-F76C"),
              std::string::npos);
    EXPECT_NE(entry.find("chainloader '/EFI/ubuntu/shimx64.efi'"),
              std::string::npos);
}

TEST(Dualboot, BlockReplacedKeepingTheRest) {
    OtherSystem mint;
    mint.name = "Linux Mint 22.3";
    mint.number = 1;
    mint.entryId = "nexus-dualboot-ubuntu";
    mint.loader.espUuid = "8687-F76C";
    mint.loader.file = "/EFI/ubuntu/shimx64.efi";
    const std::string block = grubBlock({mint});
    ASSERT_FALSE(block.empty());
    // "Restart into it once" works only if the block reads next_entry
    // and clears it.
    EXPECT_NE(block.find("set default=\"${next_entry}\""), std::string::npos);
    EXPECT_NE(block.find("save_env -f \"${config_directory}/grubenv\" "
                         "next_entry"),
              std::string::npos);

    // What nexus-boot-menu writes there first, and somebody's own.
    const std::string mine =
        "# Nexus: 5 seconds to choose at the boot menu (Fedora: 1).\n"
        "set timeout=5\n"
        "menuentry 'Mine' { true }\n";
    const std::string first = replaceGrubBlock(mine, block);
    EXPECT_EQ(first.rfind(mine, 0), 0u);
    EXPECT_NE(first.find(kBlockBegin), std::string::npos);

    // Written twice: still once.
    const std::string again = replaceGrubBlock(first, block);
    EXPECT_EQ(again, first);

    EXPECT_EQ(entryIdsInBlock(again),
              std::vector<std::string>{"nexus-dualboot-ubuntu"});

    // Removed: what was there before, exactly.
    EXPECT_EQ(replaceGrubBlock(again, ""), mine);

    // Nothing to start: no block at all.
    EXPECT_EQ(grubBlock({}), "");
    EXPECT_TRUE(entryIdsInBlock(mine).empty());
}

TEST(Dualboot, Sizes) {
    EXPECT_EQ(sizeInWords(102400000000u), "95.4 GiB");
    EXPECT_EQ(sizeInWords(629145600u), "600 MiB");
    // 0.1.87: below a megabyte it said "0 MiB".
    EXPECT_EQ(sizeInWords(1048576u), "1 MiB");
    EXPECT_EQ(sizeInWords(204800u), "200 KiB");
    EXPECT_EQ(sizeInWords(1024u), "1 KiB");
    EXPECT_EQ(sizeInWords(512u), "512 bytes");
    EXPECT_EQ(sizeInWords(0u), "0 bytes");
}

// An outside review (0.1.84): another system's ESP folder named as GRUB
// script ran at every boot. Such a system is left out of the menu.
TEST(Dualboot, AHostileLoaderNameNeverReachesGrub) {
    using namespace nexus::system;
    OtherSystem evil;
    evil.name = "Evil";
    evil.entryId = "evil";
    evil.loader.espUuid = "8687-F76C";
    evil.loader.file = "/EFI/x; }; set timeout=0; set default=0; menuentry y { true #/shimx64.efi";
    EXPECT_EQ(grubEntry(evil), "");
    EXPECT_EQ(grubBlock({evil}).find("set timeout"), std::string::npos);

    OtherSystem uuid = evil;
    uuid.loader.file = "/EFI/ubuntu/shimx64.efi";
    uuid.loader.espUuid = "8687 }; set default=0";
    EXPECT_EQ(grubEntry(uuid), "");

    EXPECT_TRUE(isSafeLoaderPath("/EFI/Microsoft/Boot/bootmgfw.efi"));
    EXPECT_FALSE(isSafeLoaderPath("/EFI/a b/x.efi"));
    EXPECT_FALSE(isSafeLoaderPath("/EFI/../x.efi"));
    EXPECT_FALSE(isSafeLoaderPath("/EFI/$x/y.efi"));
    EXPECT_TRUE(isSafeFsUuid("8687-F76C"));
    EXPECT_TRUE(isSafeFsUuid("5e2a8b0c-1f3d-4c2e-9a7b-0d6e5f4c3b2a"));
}

// ---------------------------------------------------------------------
// Disks that are plugged in (0.2.0, ledger item 10).
//
// Written by hand in the form `lsblk -J -b -o NAME,PATH,SIZE,TYPE,
// FSTYPE,PARTTYPE,UUID,LABEL,MODEL,MOUNTPOINTS,RM,TRAN` prints (the
// form itself was checked against util-linux 2.39.3: "rm" is true or
// false, "tran" a word or null). The build machine has no USB stick and
// no card slot, so these are not recordings of real hardware: an NVMe
// disk with Nexus on it, a USB stick with another system on it, and an
// SD card in a USB reader.
static const char* kWithUsb = R"({
   "blockdevices": [
      {"name":"sda", "path":"/dev/sda", "size":30752636928, "type":"disk",
       "fstype":null, "parttype":null, "uuid":null, "label":null,
       "model":"Ultra Fit", "mountpoints":[null], "rm":true, "tran":"usb",
       "children": [
          {"name":"sda1", "path":"/dev/sda1", "size":536870912,
           "type":"part", "fstype":"vfat",
           "parttype":"c12a7328-f81f-11d2-ba4b-00a0c93ec93b",
           "uuid":"AAAA-0001", "label":null, "model":null,
           "mountpoints":[null], "rm":true, "tran":null},
          {"name":"sda2", "path":"/dev/sda2", "size":30214717440,
           "type":"part", "fstype":"ext4",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"usb-root", "label":null, "model":null,
           "mountpoints":[null], "rm":true, "tran":null}
       ]},
      {"name":"sdb", "path":"/dev/sdb", "size":63864569856, "type":"disk",
       "fstype":null, "parttype":null, "uuid":null, "label":null,
       "model":"STORAGE DEVICE", "mountpoints":[null], "rm":true,
       "tran":"usb",
       "children": [
          {"name":"sdb1", "path":"/dev/sdb1", "size":63863521280,
           "type":"part", "fstype":"ext4",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"card-root", "label":null, "model":null,
           "mountpoints":[null], "rm":true, "tran":null}
       ]},
      {"name":"nvme0n1", "path":"/dev/nvme0n1", "size":256060514304,
       "type":"disk", "fstype":null, "parttype":null, "uuid":null,
       "label":null, "model":"WDC PC SN520", "mountpoints":[null],
       "rm":false, "tran":"nvme",
       "children": [
          {"name":"nvme0n1p1", "path":"/dev/nvme0n1p1", "size":629145600,
           "type":"part", "fstype":"vfat",
           "parttype":"c12a7328-f81f-11d2-ba4b-00a0c93ec93b",
           "uuid":"2C1A-77B0", "label":null, "model":null,
           "mountpoints":["/boot/efi"], "rm":false, "tran":"nvme"},
          {"name":"nvme0n1p2", "path":"/dev/nvme0n1p2", "size":102400000000,
           "type":"part", "fstype":"ext4",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"mint-root", "label":null, "model":null,
           "mountpoints":[null], "rm":false, "tran":"nvme"},
          {"name":"nvme0n1p3", "path":"/dev/nvme0n1p3", "size":150323855360,
           "type":"part", "fstype":"xfs",
           "parttype":"0fc63daf-8483-4772-8e79-3d69d8477de4",
           "uuid":"nexus-root", "label":null, "model":null,
           "mountpoints":["/sysroot"], "rm":false, "tran":"nvme"}
       ]}
   ]
})";

namespace {

std::vector<std::string> pathsOf(const std::vector<BlockDevice>& devices) {
    std::vector<std::string> paths;
    for (const auto& device : devices) paths.push_back(device.path);
    return paths;
}

std::vector<std::string> otherPaths(const std::vector<BlockDevice>& devices) {
    std::vector<std::string> paths;
    for (const auto* part : otherPartitions(devices)) paths.push_back(part->path);
    return paths;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string all;
    for (const auto& line : lines) all += line + "\n";
    return all;
}

}  // namespace

TEST(Dualboot, AUsbDiskIsNotLookedAtUnlessNamed) {
    std::string error;
    const auto devices = parseLsblk(kWithUsb, error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(devices.size(), 3u);
    EXPECT_TRUE(devices[0].removable);
    EXPECT_EQ(devices[0].transport, "usb");
    EXPECT_EQ(portableKind(devices[0]), "USB disk");
    EXPECT_EQ(portableKind(devices[2]), "");

    // Before 0.2.0: the stick's and the card's partitions were looked
    // into with the internal disk's.
    EXPECT_EQ(otherPaths(devices),
              (std::vector<std::string>{"/dev/sda2", "/dev/sdb1", "/dev/nvme0n1p2"}));

    const auto choice = chooseDisks(devices, {}, "/dev/nvme0n1");
    EXPECT_EQ(pathsOf(choice.looked), std::vector<std::string>{"/dev/nvme0n1"});
    EXPECT_EQ(pathsOf(choice.skipped),
              (std::vector<std::string>{"/dev/sda", "/dev/sdb"}));
    EXPECT_TRUE(choice.unknown.empty());
    EXPECT_TRUE(choice.notFound.empty());
    // Only the internal disk's other system is left, and no EFI
    // partition of a plugged-in disk is among what is read.
    EXPECT_EQ(otherPaths(choice.looked), std::vector<std::string>{"/dev/nvme0n1p2"});
    for (const auto& disk : choice.looked) {
        for (const auto& part : disk.children) {
            EXPECT_NE(part.path, "/dev/sda1");
        }
    }
    // Nothing of a skipped disk is carried along to be read by mistake.
    for (const auto& disk : choice.skipped) {
        EXPECT_TRUE(disk.children.empty()) << disk.path;
    }

    // Named: that one is looked at, the other still is not. A partition
    // names its disk.
    for (const char* name : {"/dev/sda", "/dev/sda2"}) {
        const auto asked = chooseDisks(devices, {name}, "/dev/nvme0n1");
        EXPECT_EQ(pathsOf(asked.looked),
                  (std::vector<std::string>{"/dev/sda", "/dev/nvme0n1"})) << name;
        EXPECT_EQ(pathsOf(asked.skipped), std::vector<std::string>{"/dev/sdb"}) << name;
        EXPECT_EQ(otherPaths(asked.looked),
                  (std::vector<std::string>{"/dev/sda2", "/dev/nvme0n1p2"})) << name;
    }
}

TEST(Dualboot, SkippedDisksAreSaidWithHowToIncludeThem) {
    std::string error;
    const auto devices = parseLsblk(kWithUsb, error);

    const std::string two = joined(skippedDiskLines(chooseDisks(devices, {}, "/dev/nvme0n1")));
    EXPECT_NE(two.find("2 USB disks not looked at:"), std::string::npos) << two;
    EXPECT_NE(two.find("/dev/sda  (Ultra Fit, 28.6 GiB)"), std::string::npos) << two;
    EXPECT_NE(two.find("/dev/sdb  (STORAGE DEVICE, 59.5 GiB)"), std::string::npos) << two;
    EXPECT_NE(two.find("sudo nexus dualboot --disk /dev/sda\n"), std::string::npos) << two;

    const std::string one =
        joined(skippedDiskLines(chooseDisks(devices, {"/dev/sda"}, "/dev/nvme0n1")));
    EXPECT_NE(one.find("1 USB disk not looked at:"), std::string::npos) << one;
    EXPECT_NE(one.find("sudo nexus dualboot --disk /dev/sdb\n"), std::string::npos) << one;
    EXPECT_EQ(one.find("/dev/sda "), std::string::npos) << one;

    // Everything named, or nothing plugged in: nothing to say.
    EXPECT_TRUE(skippedDiskLines(
        chooseDisks(devices, {"/dev/sda", "/dev/sdb"}, "/dev/nvme0n1")).empty());
}

TEST(Dualboot, RemovableIsReadHoweverLsblkWritesIt) {
    // true / false since util-linux 2.33; "1" / "0" before; a bare
    // number in between, and from other tools that write this form.
    struct Case { const char* rm; bool known; bool removable; };
    for (const Case& c : {Case{"true", true, true}, Case{"false", true, false},
                          Case{"\"1\"", true, true}, Case{"\"0\"", true, false},
                          Case{"1", true, true}, Case{"0", true, false},
                          Case{"\"true\"", true, true}, Case{"\" 1 \"", true, true},
                          Case{"null", false, false}, Case{"\"\"", false, false},
                          Case{"\"yes\"", false, false}, Case{"[1]", false, false},
                          Case{"2", false, false}}) {
        const std::string json = std::string(
            R"({"blockdevices":[{"name":"sdx","path":"/dev/sdx","type":"disk","rm":)") +
            c.rm + R"(,"children":[{"name":"sdx1","path":"/dev/sdx1","type":"part","fstype":"ext4"}]}]})";
        std::string error;
        const auto devices = parseLsblk(json, error);
        ASSERT_TRUE(error.empty()) << c.rm << ": " << error;
        ASSERT_EQ(devices.size(), 1u);
        EXPECT_EQ(devices[0].removableKnown, c.known) << c.rm;
        EXPECT_EQ(devices[0].removable, c.removable) << c.rm;
        EXPECT_EQ(chooseDisks(devices, {}, "").skipped.size(), c.removable ? 1u : 0u) << c.rm;
    }

    // TRAN in any case, with spaces around it.
    std::string error;
    const auto usb = parseLsblk(
        R"({"blockdevices":[{"name":"sdx","path":"/dev/sdx","type":"disk","rm":false,"tran":" USB "}]})",
        error);
    ASSERT_EQ(usb.size(), 1u);
    // An external disk in a USB case says "not removable" and is USB.
    EXPECT_EQ(portableKind(usb[0]), "USB disk");
}

TEST(Dualboot, AMissingRemovableFieldIsBuiltInAndSaid) {
    // The Asus fixture is from before RM and TRAN were asked for: an
    // lsblk that gives neither.
    std::string error;
    const auto devices = parseLsblk(kAsus, error);
    const auto choice = chooseDisks(devices, {}, "/dev/nvme0n1");
    EXPECT_TRUE(choice.skipped.empty());
    EXPECT_EQ(choice.looked.size(), devices.size());
    // Said for the disk with partitions; zram has nothing to look in.
    EXPECT_EQ(choice.unknown, std::vector<std::string>{"/dev/nvme0n1"});
    const std::string said = joined(skippedDiskLines(choice));
    EXPECT_NE(said.find("does not say whether /dev/nvme0n1 is removable"),
              std::string::npos) << said;
    EXPECT_NE(said.find("looked at"), std::string::npos) << said;
    // What it finds is what it found before.
    EXPECT_EQ(otherPaths(choice.looked), std::vector<std::string>{"/dev/nvme0n1p2"});
}

TEST(Dualboot, TheDiskThisSystemRunsFromIsAlwaysLookedAt) {
    // Nexus installed on the USB disk: its neighbours on that disk are
    // still its neighbours. The card is still left out.
    std::string error;
    const auto devices = parseLsblk(kWithUsb, error);
    const auto choice = chooseDisks(devices, {}, "/dev/sda");
    EXPECT_EQ(pathsOf(choice.looked),
              (std::vector<std::string>{"/dev/sda", "/dev/nvme0n1"}));
    EXPECT_EQ(pathsOf(choice.skipped), std::vector<std::string>{"/dev/sdb"});
}

TEST(Dualboot, ADiskThatIsNotThereIsSaid) {
    std::string error;
    const auto devices = parseLsblk(kWithUsb, error);
    const auto choice = chooseDisks(devices, {"/dev/sdz", "/dev/sda"}, "/dev/nvme0n1");
    EXPECT_EQ(choice.notFound, std::vector<std::string>{"/dev/sdz"});
    // A name is a whole path: "/dev/sd" is not /dev/sda.
    EXPECT_EQ(chooseDisks(devices, {"/dev/sd"}, "").notFound,
              std::vector<std::string>{"/dev/sd"});
    EXPECT_EQ(chooseDisks(devices, {""}, "").notFound, std::vector<std::string>{""});
}

TEST(Dualboot, ACardInABuiltInSlotIsLeftOutByWhatTheKernelCallsIt) {
    // A card slot on the mainboard: lsblk says mmc and "not removable"
    // for a card and for soldered storage alike. The CLI reads the
    // kernel's word for it (SD or MMC) into cardType.
    std::string error;
    auto devices = parseLsblk(
        R"({"blockdevices":[
            {"name":"mmcblk0","path":"/dev/mmcblk0","type":"disk","size":31914983424,
             "model":null,"rm":false,"tran":"mmc",
             "children":[{"name":"mmcblk0p1","path":"/dev/mmcblk0p1","type":"part","fstype":"ext4"}]},
            {"name":"mmcblk1","path":"/dev/mmcblk1","type":"disk","size":62537072640,
             "model":null,"rm":false,"tran":"mmc",
             "children":[{"name":"mmcblk1p1","path":"/dev/mmcblk1p1","type":"part","fstype":"ext4"}]}
        ]})", error);
    ASSERT_EQ(devices.size(), 2u);
    // Without the kernel's word, neither is known to be a card.
    EXPECT_TRUE(chooseDisks(devices, {}, "").skipped.empty());

    devices[0].cardType = "SD";
    devices[1].cardType = "MMC";
    EXPECT_EQ(portableKind(devices[0]), "SD card");
    EXPECT_EQ(portableKind(devices[1]), "");
    const auto choice = chooseDisks(devices, {}, "");
    EXPECT_EQ(pathsOf(choice.skipped), std::vector<std::string>{"/dev/mmcblk0"});
    EXPECT_EQ(pathsOf(choice.looked), std::vector<std::string>{"/dev/mmcblk1"});
    const std::string said = joined(skippedDiskLines(choice));
    EXPECT_NE(said.find("1 SD card not looked at:"), std::string::npos) << said;
    EXPECT_NE(said.find("--disk /dev/mmcblk0"), std::string::npos) << said;
}

TEST(Dualboot, ASkippedDisksNameCannotSteerTheTerminal) {
    std::string error;
    const auto devices = parseLsblk(
        "{\"blockdevices\":[{\"name\":\"sdx\",\"path\":\"/dev/sdx\",\"type\":\"disk\","
        "\"rm\":true,\"tran\":\"usb\",\"model\":\"Evil\\u001b[2J\\u001b]0;x\\u0007\"}]}",
        error);
    ASSERT_EQ(devices.size(), 1u);
    const std::string said = joined(skippedDiskLines(chooseDisks(devices, {}, "")));
    EXPECT_EQ(said.find('\x1b'), std::string::npos);
    EXPECT_EQ(said.find('\x07'), std::string::npos);
    EXPECT_NE(said.find("Evil"), std::string::npos) << said;
}
