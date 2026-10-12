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
    EXPECT_NE(entry.find("chainloader /EFI/ubuntu/shimx64.efi"),
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
}
