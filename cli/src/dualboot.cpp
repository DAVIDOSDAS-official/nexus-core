// nexus dualboot.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// nexus dualboot: the other systems on this disk, Nexus's boot menu
// entries for them, and "restart into that one, once".
// See system/include/nexus/system/dualboot.hpp for how it works and
// what was learnt on the Asus.
const char* const kGrubCustom = "/boot/grub2/custom.cfg";

const char* const kGrubEnv = "/boot/grub2/grubenv";

// Every tool in this file is started directly, each value an argument
// of its own (0.2.1, ledger item 4): this runs as root on what other
// systems' disks hold, and no command line here is read by a shell any
// more. mergedErrors() is what "2>&1" was.
using nexus::system::mergedErrors;
using nexus::system::runProgram;

// Look inside a partition, read-only, and give it back. Options that
// never write, even to replay a journal: a system that was hibernated
// or switched off hard keeps its unfinished journal for itself.
struct LookInside {
    std::string dir;
    bool mounted = false;

    explicit LookInside(const nexus::system::BlockDevice& device) {
        // Another system's filesystem, maybe a USB stick's: nothing on it
        // runs, opens a device or gains rights here (0.1.84, outside review).
        std::string options = "ro,nosuid,nodev,noexec";
        if (device.fstype == "ext4" || device.fstype == "ext3" ||
            device.fstype == "ext2") {
            options += ",noload";
        } else if (device.fstype == "xfs") {
            options += ",norecovery";
        } else if (device.fstype == "btrfs") {
            // btrfs replays its tree log even read-only (0.1.85,
            // outside review N4): rescue=nologreplay keeps it untouched.
            options += ",rescue=nologreplay";
        } else if (device.fstype == "f2fs" || device.fstype == "bcachefs") {
            options += ",norecovery";
        }
        char pattern[] = "/run/nexus-dualboot-XXXXXX";
        if (!::mkdtemp(pattern)) {
            return;
        }
        dir = pattern;
        const auto ran = runProgram({"mount", "-o", options, device.path, dir},
                                    mergedErrors());
        mounted = ran.exitCode == 0;
    }

    ~LookInside() {
        if (mounted) {
            runProgram({"umount", dir}, mergedErrors());
        }
        if (!dir.empty()) {
            std::error_code ignored;
            std::filesystem::remove(dir, ignored);
        }
    }

    // A file inside, following a symbolic link inside too: Ubuntu's
    // etc/os-release is ../usr/lib/os-release, and an absolute link
    // must not end up reading this system's file instead.
    std::string read(const std::string& relative) const {
        namespace fs = std::filesystem;
        fs::path path = fs::path(dir) / relative;
        std::error_code error;
        for (int hops = 0; hops < 8 && fs::is_symlink(path, error); ++hops) {
            const fs::path target = fs::read_symlink(path, error);
            path = target.is_absolute()
                       ? fs::path(dir) / target.relative_path()
                       : path.parent_path() / target;
        }
        return fs::is_regular_file(path, error) ? readWholeFile(path) : "";
    }
};

nexus::system::OsRelease readOtherRelease(const LookInside& inside) {
    // Plain, then Ubuntu's btrfs layout (@), then Fedora's (root).
    for (const std::string prefix : {"", "@/", "root/", "@rootfs/"}) {
        for (const std::string file : {"usr/lib/os-release", "etc/os-release"}) {
            const std::string text = inside.read(prefix + file);
            if (!text.empty()) {
                return nexus::system::parseOsRelease(text);
            }
        }
    }
    return {};
}

std::vector<nexus::system::EfiLoader> readLoaders(
    const nexus::system::BlockDevice& esp
) {
    namespace fs = std::filesystem;
    std::vector<nexus::system::EfiLoader> loaders;
    LookInside inside(esp);
    if (!inside.mounted) {
        return loaders;
    }
    std::error_code error;
    fs::path efi;
    for (const auto& entry : fs::directory_iterator(inside.dir, error)) {
        std::string name = entry.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name == "efi" && entry.is_directory()) {
            efi = entry.path();
        }
    }
    if (efi.empty()) {
        return loaders;
    }
    for (const auto& entry : fs::directory_iterator(efi, error)) {
        if (!entry.is_directory() ||
            nexus::system::skippedEfiDirectory(
                entry.path().filename().string())) {
            continue;
        }
        std::vector<std::string> files;
        std::string grubCfg;
        for (const auto& file : fs::directory_iterator(entry.path(), error)) {
            const std::string name = file.path().filename().string();
            files.push_back(name);
            std::string low = name;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low == "grub.cfg" && file.is_regular_file() && !file.is_symlink()) {
                grubCfg = readWholeFile(file.path());
            }
        }
        const std::string loader = nexus::system::pickLoader(files);
        if (loader.empty()) {
            continue;
        }
        nexus::system::EfiLoader found;
        found.partition = esp.path;
        found.espUuid = esp.uuid;
        found.directory = entry.path().filename().string();
        found.file = "/" + efi.filename().string() + "/" + found.directory +
                     "/" + loader;
        // Never into GRUB's script unless it is plainly a path (0.1.84).
        if (!nexus::system::isSafeLoaderPath(found.file) ||
            !nexus::system::isSafeFsUuid(found.espUuid)) {
            std::cerr << "Skipped a boot loader with an unusual name: "
                      << found.partition << " " << found.directory << "\n";
            continue;
        }
        found.target = nexus::system::grubStubTarget(grubCfg);
        loaders.push_back(found);
    }
    std::sort(loaders.begin(), loaders.end(),
              [](const auto& a, const auto& b) {
                  return a.directory < b.directory;
              });
    return loaders;
}

struct DiskPicture {
    std::string error;
    std::vector<nexus::system::BlockDevice> devices;
    // Which disks were looked at for other systems, and which were
    // left out because they are plugged in (0.2.0, ledger item 10).
    nexus::system::DiskChoice choice;
    std::string thisName;
    std::string thisDisk;
    std::string thisPartition;
    std::uint64_t thisSize = 0;
    bool thisEncrypted = false;
    std::string diskModel;
    std::uint64_t diskSize = 0;
    std::vector<nexus::system::OtherSystem> systems;
    bool lookedInside = false;
};

// Which partition this system runs from: the one holding /sysroot
// (or /), possibly through an opened encrypted volume.
bool findThisSystem(const nexus::system::BlockDevice& device,
                    const nexus::system::BlockDevice* partition,
                    const nexus::system::BlockDevice* disk,
                    bool encrypted,
                    DiskPicture& picture) {
    const bool isPartition = device.type == "part";
    if (isPartition) {
        partition = &device;
    }
    encrypted = encrypted || device.fstype == "crypto_LUKS" ||
                device.type == "crypt";
    for (const auto& point : device.mountpoints) {
        if ((point == "/sysroot" || point == "/") && partition) {
            picture.thisPartition = partition->path;
            picture.thisSize = partition->size;
            picture.thisEncrypted = encrypted;
            if (disk) {
                picture.thisDisk = disk->path;
                picture.diskModel = disk->model;
                picture.diskSize = disk->size;
            }
            return true;
        }
    }
    for (const auto& child : device.children) {
        if (findThisSystem(child, partition, disk, encrypted, picture)) {
            return true;
        }
    }
    return false;
}

// What the kernel calls a card on the mmc bus: "SD" for a card in a
// slot, "MMC" for storage soldered to the board (0.2.0, ledger item
// 10). lsblk shows both as mmc and "not removable". A few bytes of
// /sys, and only for a name made of letters and digits.
std::string mmcCardType(const std::string& name) {
    if (name.empty() ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isalnum(c) != 0;
        })) {
        return "";
    }
    std::ifstream in("/sys/block/" + name + "/device/type");
    char text[16] = {};
    in.read(text, sizeof text - 1);
    std::string type(text, static_cast<std::size_t>(in.gcount()));
    while (!type.empty() && (type.back() == '\n' || type.back() == ' ')) {
        type.pop_back();
    }
    return type;
}

// named: the disks the person asked for with --disk.
DiskPicture lookAtDisk(const std::vector<std::string>& named) {
    DiskPicture picture;
    // RM and TRAN: whether a disk can be taken out, and how it is
    // connected (0.2.0, ledger item 10).
    const auto listed = runProgram(
        {"lsblk", "-J", "-b", "-o",
         "NAME,PATH,SIZE,TYPE,FSTYPE,PARTTYPE,UUID,LABEL,MODEL,MOUNTPOINTS,RM,TRAN"},
        nexus::system::shownErrors());
    if (listed.exitCode != 0) {
        picture.error = "lsblk did not answer (exit " +
                        std::to_string(listed.exitCode) + ").";
        return picture;
    }
    picture.devices = nexus::system::parseLsblk(listed.text, picture.error);
    if (!picture.error.empty()) {
        return picture;
    }
    for (const auto& disk : picture.devices) {
        if (disk.type == "disk" &&
            findThisSystem(disk, nullptr, &disk, false, picture)) {
            break;
        }
    }
    picture.thisName = nexus::system::systemName(
        nexus::system::parseOsRelease(readWholeFile("/etc/os-release")));

    // USB sticks, SD cards and other disks that can be taken out are
    // not looked into, and nothing on them reaches the boot menu,
    // unless the person names the disk (0.2.0, ledger item 10). The
    // disk this system runs from is always looked at.
    for (auto& device : picture.devices) {
        if (device.transport == "mmc" || device.name.rfind("mmcblk", 0) == 0) {
            device.cardType = mmcCardType(device.name);
        }
    }
    picture.choice = nexus::system::chooseDisks(picture.devices, named,
                                                picture.thisDisk);
    const auto& looked = picture.choice.looked;

    picture.lookedInside = nexus::system::haveRootPrivileges();

    std::vector<nexus::system::OtherSystem> systems;
    for (const auto* part : nexus::system::otherPartitions(looked)) {
        nexus::system::OtherSystem system;
        system.partition = part->path;
        system.fstype = part->fstype;
        system.size = part->size;
        system.encrypted = part->fstype == "crypto_LUKS";
        if (picture.lookedInside && !system.encrypted) {
            LookInside inside(*part);
            if (inside.mounted) {
                const auto release = readOtherRelease(inside);
                system.name = nexus::system::systemName(release);
                system.named = !system.name.empty();
            }
        }
        if (system.name.empty()) {
            system.name = system.encrypted
                              ? "encrypted -- which system, cannot be told "
                                "without its password"
                              : (picture.lookedInside
                                     ? "no system found on it (a /home or "
                                       "data partition?)"
                                     : "a partition (" + system.fstype + ")");
        }
        // Only partitions with a system on them, or that might have
        // one, are worth a line; data partitions are dropped once
        // looked inside.
        if (picture.lookedInside && !system.named && !system.encrypted) {
            continue;
        }
        systems.push_back(system);
    }

    std::vector<nexus::system::EfiLoader> loaders;
    if (picture.lookedInside) {
        for (const auto& disk : looked) {
            for (const auto& part : disk.children) {
                if (nexus::system::isEfiPartition(part) &&
                    !nexus::system::inUse(part)) {
                    const auto found = readLoaders(part);
                    loaders.insert(loaders.end(), found.begin(), found.end());
                }
            }
        }
    }
    picture.systems =
        nexus::system::matchSystems(systems, loaders, looked);
    return picture;
}

// The disks that were left out, and how to include them.
void showSkippedDisks(const DiskPicture& picture) {
    const auto lines = nexus::system::skippedDiskLines(picture.choice);
    if (lines.empty()) {
        return;
    }
    std::cout << "\n";
    for (const auto& line : lines) {
        std::cout << line << "\n";
    }
}

void showDiskPicture(const DiskPicture& picture,
                     const std::vector<std::string>& inMenu) {
    std::cout << "Systems on this disk";
    if (!picture.diskModel.empty()) {
        std::cout << " (" << picture.diskModel << ", "
                  << nexus::system::sizeInWords(picture.diskSize) << ")";
    }
    std::cout << "\n\n";

    auto row = [](const std::string& tag, const std::string& name,
                  const std::string& where, bool encrypted,
                  std::uint64_t size) {
        std::cout << "  " << std::left << std::setw(13) << tag << name
                  << "\n  " << std::setw(13) << "" << where
                  << (where.empty() ? "" : ", ")
                  << (encrypted ? "encrypted" : "not encrypted");
        if (size) {
            std::cout << ", " << nexus::system::sizeInWords(size);
        }
        std::cout << "\n";
    };

    row("This system", picture.thisName, picture.thisPartition,
        picture.thisEncrypted, picture.thisSize);

    bool anyStartable = false;
    bool anyMissing = false;
    for (const auto& system : picture.systems) {
        std::cout << "\n";
        const std::string tag =
            system.number ? std::to_string(system.number) : "-";
        row(tag, system.name, system.partition, system.encrypted,
            system.size);
        if (system.loader.file.empty()) {
            if (picture.lookedInside) {
                std::cout << "               no boot loader found for it: "
                             "Nexus cannot start it\n";
            }
            continue;
        }
        anyStartable = true;
        const bool listed =
            std::find(inMenu.begin(), inMenu.end(), system.entryId) !=
            inMenu.end();
        anyMissing = anyMissing || !listed;
        std::cout << "               started by " << system.loader.file
                  << " on " << system.loader.partition << "\n"
                  << "               in Nexus's boot menu: "
                  << (listed ? "yes" : "no") << "\n";
    }

    if (picture.systems.empty()) {
        std::cout << "\nNo other system found";
        if (!picture.lookedInside) {
            std::cout << " from the outside";
        }
        std::cout << ".\n";
    }

    showSkippedDisks(picture);

    if (!picture.lookedInside) {
        std::cout
            << "\nTheir names, and what starts them, are inside their "
               "partitions.\nNexus looks, read-only; that needs root:\n"
               "    sudo nexus dualboot\n";
        return;
    }
    if (anyMissing) {
        std::cout << "\nTo put them in Nexus's boot menu:\n"
                     "    sudo nexus dualboot add --apply\n";
    } else if (anyStartable) {
        std::cout << "\nTo restart into one of them, once:\n"
                     "    sudo nexus dualboot <number> --apply\n"
                     "Restarting normally comes back to Nexus.\n";
    }
}

// Write custom.cfg. /boot may be mounted read-only; if it is, it is
// made writable for the write and put back.
bool writeGrubCustom(const std::string& text, std::string& error) {
    const auto options = runProgram({"findmnt", "-n", "-o", "OPTIONS", "/boot"},
                                    mergedErrors());
    const bool readOnly =
        options.text.rfind("ro,", 0) == 0 || options.text == "ro\n";
    if (readOnly) {
        const auto rw = runProgram({"mount", "-o", "remount,rw", "/boot"},
                                   mergedErrors());
        if (rw.exitCode != 0) {
            error = "/boot could not be made writable: " + rw.text;
            return false;
        }
    }
    const std::string temporary = std::string(kGrubCustom) + ".nexus-new";
    bool ok = false;
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << text;
        out.flush();
        ok = static_cast<bool>(out);
    }
    if (ok) {
        std::error_code renameError;
        if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
            std::filesystem::remove(temporary, renameError);
            std::filesystem::remove(kGrubCustom, renameError);
        } else {
            std::filesystem::rename(temporary, kGrubCustom, renameError);
        }
        ok = !renameError;
        if (!ok) {
            error = renameError.message();
        }
    } else {
        error = "could not write " + temporary;
    }
    if (readOnly) {
        runProgram({"mount", "-o", "remount,ro", "/boot"}, mergedErrors());
    }
    return ok;
}

}  // namespace

int commandDualboot(

    const std::vector<std::string>& raw,
    bool apply,
    bool assumeYes,
    const std::string& invocation
) {
    // --disk <path>: also look at a disk that is plugged in (0.2.0,
    // ledger item 10). Its own option, like `nexus server`'s: main
    // hands unknown words through.
    std::vector<std::string> words;
    std::vector<std::string> named;
    bool badOption = false;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == "--disk" && i + 1 < raw.size()) {
            named.push_back(raw[++i]);
        } else if (!raw[i].empty() && raw[i][0] == '-') {
            badOption = true;
        } else {
            words.push_back(raw[i]);
        }
    }

    const std::string action = words.empty() ? "" : words[0];
    const bool isNumber =
        !action.empty() &&
        action.find_first_not_of("0123456789") == std::string::npos;

    if (badOption || words.size() > 1 ||
        !(action.empty() || action == "add" || action == "remove" ||
          isNumber)) {
        std::cerr << "Use: nexus dualboot              the systems on "
                     "this disk\n"
                     "     nexus dualboot add          put them in Nexus's "
                     "boot menu\n"
                     "     nexus dualboot remove       take them out again\n"
                     "     nexus dualboot <number>     restart into that "
                     "one, once\n"
                     "     --disk /dev/sdX             also look at that USB "
                     "or removable disk\n";
        return 2;
    }

    const std::string current = readWholeFile(kGrubCustom);
    const auto inMenu = nexus::system::entryIdsInBlock(current);

    // A --disk that names nothing here: say which disks there are,
    // rather than quietly looking at none of them.
    const auto noSuchDisk = [](const DiskPicture& picture) {
        if (picture.choice.notFound.empty()) {
            return false;
        }
        for (const auto& path : picture.choice.notFound) {
            std::cerr << "There is no disk " << nexus::system::printable(path)
                      << " here.\n";
        }
        std::string disks;
        for (const auto& device : picture.devices) {
            if (device.type == "disk" && !device.children.empty()) {
                disks += " " + nexus::system::printable(device.path);
            }
        }
        std::cerr << "Disks with partitions here:"
                  << (disks.empty() ? std::string(" none") : disks) << "\n";
        return true;
    };

    if (action.empty()) {
        const DiskPicture picture = lookAtDisk(named);
        if (!picture.error.empty()) {
            std::cerr << picture.error << "\n";
            return 1;
        }
        if (noSuchDisk(picture)) {
            return 2;
        }
        showDiskPicture(picture, inMenu);
        return 0;
    }

    if (!nexus::system::haveRootPrivileges()) {
        std::cout << "This looks inside the other systems' partitions "
                     "(read-only) and\nchanges Nexus's boot menu: it needs "
                     "root. Re-run with sudo:\n    sudo "
                  << invocation << "\n";
        return 1;
    }

    const DiskPicture picture = lookAtDisk(named);
    if (!picture.error.empty()) {
        std::cerr << picture.error << "\n";
        return 1;
    }
    if (noSuchDisk(picture)) {
        return 2;
    }

    nexus::system::TransactionRecord record;
    record.when = nexus::system::currentTimestamp();
    record.request = "dualboot " + action;

    if (action == "add" || action == "remove") {
        const bool add = action == "add";
        const std::string block =
            add ? nexus::system::grubBlock(picture.systems) : "";
        if (add && block.empty()) {
            showDiskPicture(picture, inMenu);
            std::cout << "\nNothing Nexus can start was found, so "
                         "nothing to add.\n";
            return 1;
        }
        if (!add && inMenu.empty()) {
            std::cout << "Nexus's boot menu has no other systems in it. "
                         "Nothing to remove.\n";
            return 0;
        }

        std::cout << "What Nexus will do\n";
        if (add) {
            for (const auto& system : picture.systems) {
                if (!system.entryId.empty()) {
                    std::cout << "  Add \"" << system.name
                              << "\" to the boot menu: it hands over to "
                              << system.loader.file << " on "
                              << system.loader.partition << "\n";
                }
            }
            // Nexus's part of the menu is written anew from what was
            // found now. An entry whose system is not among them goes,
            // and that is said (0.2.0: one on a USB disk, added before
            // USB disks were left out, would have gone without a word).
            for (const auto& id : inMenu) {
                const bool still = std::any_of(
                    picture.systems.begin(), picture.systems.end(),
                    [&id](const auto& system) { return system.entryId == id; });
                if (!still) {
                    std::cout << "  Take \"" << nexus::system::printable(id)
                              << "\" out of the boot menu: its system was "
                                 "not found this time\n";
                }
            }
        } else {
            std::cout << "  Take the other systems out of the boot menu ("
                      << inMenu.size() << ")\n";
        }
        std::cout << "  In " << kGrubCustom
                  << ", between its own two marker lines\n"
                  << "\nIt will not\n"
                     "  Change anything of the other systems, their "
                     "partitions or their own menus\n"
                     "  Change the order the computer starts in: Nexus "
                     "stays first\n"
                     "  Touch anything else in "
                  << kGrubCustom << "\n";
        std::cout << "\nThe boot menu shows for a few seconds at every "
                     "start; pick the system\nwith the arrow keys.\n";
        if (add) {
            showSkippedDisks(picture);
        }

        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    sudo "
                      << invocation << " --apply\n";
            return 0;
        }
        if (!confirmed(assumeYes)) {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }

        record.kind = add ? nexus::system::TransactionKind::Install
                          : nexus::system::TransactionKind::Remove;
        record.resolved = std::string("boot menu: ") + kGrubCustom;
        if (add) {
            for (const auto& system : picture.systems) {
                if (!system.entryId.empty()) {
                    record.packages.insert(system.name);
                }
            }
        } else {
            record.packages.insert(inMenu.begin(), inMenu.end());
        }
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker =
            nexus::system::beginTransaction(log, record);

        std::string error;
        const bool ok = writeGrubCustom(
            nexus::system::replaceGrubBlock(current, block), error);

        record.outcome = ok ? (add ? "added" : "removed") : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);

        if (!ok) {
            std::cout << "\nNot written: " << error
                      << "\nNothing else was changed.\n";
            return 1;
        }
        std::cout << (add ? "\nDone. They are in the boot menu from the "
                            "next start.\n"
                          : "\nDone. The boot menu has only Nexus again.\n");
        return 0;
    }

    // A number: restart into that system, once.
    const int number = action.size() > 3 ? 0 : std::stoi(action);
    const nexus::system::OtherSystem* chosen = nullptr;
    for (const auto& system : picture.systems) {
        if (system.number == number) {
            chosen = &system;
        }
    }
    if (!chosen) {
        showDiskPicture(picture, inMenu);
        std::cout << "\nThere is no system " << action << ".\n";
        return 1;
    }
    // Not in the menu yet (a fresh install, say): it is added on the
    // way. Two commands for one wish was the Asus experience on
    // 16 October -- "add" first, then the restart.
    const bool addFirst =
        std::find(inMenu.begin(), inMenu.end(), chosen->entryId) ==
        inMenu.end();

    std::cout << "What Nexus will do\n";
    if (addFirst) {
        std::cout << "  Add the other systems to the boot menu (in "
                  << kGrubCustom << "): they stay there\n";
    }
    std::cout << "  Restart the computer now, into " << chosen->name << "\n"
              << "  Only this once: the next restart starts Nexus again\n"
              << "\nIt will not\n"
                 "  Change the order the computer starts in\n"
                 "  Close your apps for you first: save your work\n";
    if (!apply) {
        std::cout << "\nNothing has been changed. To restart:\n    sudo "
                  << invocation << " --apply\n";
        return 0;
    }
    if (!confirmed(assumeYes)) {
        std::cout << "Nothing has been changed.\n";
        return 1;
    }

    if (addFirst) {
        nexus::system::TransactionRecord added;
        added.when = nexus::system::currentTimestamp();
        added.request = "dualboot add";
        added.kind = nexus::system::TransactionKind::Install;
        added.resolved = std::string("boot menu: ") + kGrubCustom;
        for (const auto& system : picture.systems) {
            if (!system.entryId.empty()) {
                added.packages.insert(system.name);
            }
        }
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, added);
        std::string error;
        const bool ok = writeGrubCustom(
            nexus::system::replaceGrubBlock(
                current, nexus::system::grubBlock(picture.systems)),
            error);
        added.outcome = ok ? "added" : "failed";
        added.succeeded = ok;
        added.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, added);
        if (!ok) {
            std::cout << "\nThe boot menu could not be written: " << error
                      << "\nNothing else was changed; not restarting.\n";
            return 1;
        }
        std::cout << "\nAdded to the boot menu.\n";
    }

    const auto set = runProgram(
        {"grub2-editenv", kGrubEnv, "set", "next_entry=" + chosen->entryId},
        mergedErrors());
    if (set.exitCode != 0) {
        std::cout << "\nThe boot menu would not take it (" << set.text
                  << ").\nNothing has been changed; not restarting.\n";
        return 1;
    }
    std::cout << "\nRestarting into " << chosen->name << ".\n" << std::flush;
    return runProgram({"systemctl", "reboot"}, nexus::system::handedOver(false)).ok ? 0 : 1;
}

}  // namespace nexus_cli
