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
        const auto ran = nexus::system::runCommand(
            "mount -o " + options + " " + shellQuote(device.path) + " " +
            shellQuote(dir) + " 2>&1", true);
        mounted = ran.exitCode == 0;
    }

    ~LookInside() {
        if (mounted) {
            nexus::system::runCommand("umount " + shellQuote(dir), true);
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
    std::string thisName;
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

DiskPicture lookAtDisk() {
    DiskPicture picture;
    const auto listed = nexus::system::runCommand(
        "lsblk -J -b -o NAME,PATH,SIZE,TYPE,FSTYPE,PARTTYPE,UUID,LABEL,"
        "MODEL,MOUNTPOINTS", false);
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

    picture.lookedInside = nexus::system::haveRootPrivileges();

    std::vector<nexus::system::OtherSystem> systems;
    for (const auto* part : nexus::system::otherPartitions(picture.devices)) {
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
        for (const auto& disk : picture.devices) {
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
        nexus::system::matchSystems(systems, loaders, picture.devices);
    return picture;
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
    const auto options = nexus::system::runCommand(
        "findmnt -n -o OPTIONS /boot", true);
    const bool readOnly =
        options.text.rfind("ro,", 0) == 0 || options.text == "ro\n";
    if (readOnly) {
        const auto rw = nexus::system::runCommand(
            "mount -o remount,rw /boot 2>&1", true);
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
        nexus::system::runCommand("mount -o remount,ro /boot", true);
    }
    return ok;
}

}  // namespace

int commandDualboot(

    const std::vector<std::string>& words,
    bool apply,
    bool assumeYes,
    const std::string& invocation
) {
    const std::string action = words.empty() ? "" : words[0];
    const bool isNumber =
        !action.empty() &&
        action.find_first_not_of("0123456789") == std::string::npos;

    if (words.size() > 1 ||
        !(action.empty() || action == "add" || action == "remove" ||
          isNumber)) {
        std::cerr << "Use: nexus dualboot              the systems on "
                     "this disk\n"
                     "     nexus dualboot add          put them in Nexus's "
                     "boot menu\n"
                     "     nexus dualboot remove       take them out again\n"
                     "     nexus dualboot <number>     restart into that "
                     "one, once\n";
        return 2;
    }

    const std::string current = readWholeFile(kGrubCustom);
    const auto inMenu = nexus::system::entryIdsInBlock(current);

    if (action.empty()) {
        const DiskPicture picture = lookAtDisk();
        if (!picture.error.empty()) {
            std::cerr << picture.error << "\n";
            return 1;
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

    const DiskPicture picture = lookAtDisk();
    if (!picture.error.empty()) {
        std::cerr << picture.error << "\n";
        return 1;
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

    const auto set = nexus::system::runCommand(
        "grub2-editenv " + std::string(kGrubEnv) + " set next_entry=" +
            shellQuote(chosen->entryId) + " 2>&1", true);
    if (set.exitCode != 0) {
        std::cout << "\nThe boot menu would not take it (" << set.text
                  << ").\nNothing has been changed; not restarting.\n";
        return 1;
    }
    std::cout << "\nRestarting into " << chosen->name << ".\n" << std::flush;
    return std::system("systemctl reboot") == 0 ? 0 : 1;
}

}  // namespace nexus_cli
