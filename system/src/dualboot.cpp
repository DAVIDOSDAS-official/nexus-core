#include <nexus/system/dualboot.hpp>

#include <nexus/system/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace nexus::system {

const char* const kBlockBegin = "### BEGIN nexus dualboot ###";
const char* const kBlockEnd = "### END nexus dualboot ###";

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

BlockDevice readDevice(const JsonValue& value) {
    BlockDevice device;
    device.name = value["name"].asString();
    device.path = value["path"].asString();
    device.type = value["type"].asString();
    device.fstype = value["fstype"].asString();
    device.parttype = lower(value["parttype"].asString());
    device.uuid = value["uuid"].asString();
    device.label = value["label"].asString();
    device.model = trim(value["model"].asString());

    // -b: a number. Without it lsblk writes "95.4G"; read what is
    // there either way rather than inventing a size.
    const std::string size = value["size"].asString();
    try {
        device.size = size.empty() ? 0 : std::stoull(size);
    } catch (...) {
        device.size = 0;
    }

    // MOUNTPOINTS is a list ([null] when not mounted); older lsblk
    // has MOUNTPOINT, one string.
    for (const auto& point : value["mountpoints"].items) {
        if (point.isString() && !point.text.empty()) {
            device.mountpoints.push_back(point.text);
        }
    }
    const std::string single = value["mountpoint"].asString();
    if (!single.empty()) {
        device.mountpoints.push_back(single);
    }

    for (const auto& child : value["children"].items) {
        device.children.push_back(readDevice(child));
    }
    return device;
}

// Value of KEY="value" or KEY=value, with the quotes and escapes of
// the os-release format taken off.
std::string osReleaseValue(const std::string& raw) {
    std::string value = trim(raw);
    if (value.size() >= 2 &&
        (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);
    }
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            ++i;
        }
        out += value[i];
    }
    return out;
}

// A name GRUB shows: one line, no quote that would end it early.
std::string menuTitle(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '\'' ) {
            out += "'\\''";
        } else if (c == '\n' || c == '\r') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

const BlockDevice* findByPath(const std::vector<BlockDevice>& devices,
                              const std::string& path) {
    for (const auto& device : devices) {
        if (device.path == path) {
            return &device;
        }
        if (const BlockDevice* inner = findByPath(device.children, path)) {
            return inner;
        }
    }
    return nullptr;
}

const BlockDevice* findByUuid(const std::vector<BlockDevice>& devices,
                              const std::string& uuid) {
    for (const auto& device : devices) {
        if (!uuid.empty() && lower(device.uuid) == lower(uuid)) {
            return &device;
        }
        if (const BlockDevice* inner = findByUuid(device.children, uuid)) {
            return inner;
        }
    }
    return nullptr;
}

}  // namespace

std::vector<BlockDevice> parseLsblk(const std::string& json,
                                    std::string& error) {
    std::vector<BlockDevice> devices;
    const JsonResult parsed = parseJson(json);
    if (!parsed.error.empty()) {
        error = "lsblk's answer could not be read: " + parsed.error;
        return devices;
    }
    const JsonValue& list = parsed.value["blockdevices"];
    if (!list.isArray()) {
        error = "lsblk's answer has no device list";
        return devices;
    }
    for (const auto& item : list.items) {
        devices.push_back(readDevice(item));
    }
    return devices;
}

bool isLinuxFilesystem(const std::string& fstype) {
    static const std::set<std::string> kinds = {
        "ext4", "ext3", "ext2", "xfs", "btrfs", "f2fs", "bcachefs"};
    return kinds.count(fstype) > 0;
}

std::vector<const BlockDevice*> otherPartitions(
    const std::vector<BlockDevice>& devices
) {
    std::vector<const BlockDevice*> found;
    for (const auto& disk : devices) {
        if (disk.type != "disk") {
            continue;
        }
        for (const auto& part : disk.children) {
            if (part.type != "part" || inUse(part) || isEfiPartition(part)) {
                continue;
            }
            if (isLinuxFilesystem(part.fstype) ||
                part.fstype == "crypto_LUKS") {
                found.push_back(&part);
            }
        }
    }
    return found;
}

bool isEfiPartition(const BlockDevice& device) {
    return device.parttype == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";
}

bool inUse(const BlockDevice& device) {
    if (!device.mountpoints.empty()) {
        return true;
    }
    for (const auto& child : device.children) {
        if (inUse(child)) {
            return true;
        }
    }
    return false;
}

OsRelease parseOsRelease(const std::string& text) {
    OsRelease release;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = osReleaseValue(line.substr(equals + 1));
        if (key == "ID") {
            release.id = value;
        } else if (key == "NAME") {
            release.name = value;
        } else if (key == "VERSION") {
            release.version = value;
        } else if (key == "PRETTY_NAME") {
            release.prettyName = value;
        }
    }
    return release;
}

std::string systemName(const OsRelease& release) {
    if (!release.prettyName.empty()) {
        return release.prettyName;
    }
    if (!release.name.empty()) {
        return release.version.empty()
                   ? release.name
                   : release.name + " " + release.version;
    }
    return release.id;
}

std::string grubStubTarget(const std::string& grubCfg) {
    std::istringstream lines(grubCfg);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::vector<std::string> word;
        std::string w;
        while (words >> w) {
            word.push_back(w);
        }
        if (word.empty() || word[0].rfind("search", 0) != 0) {
            continue;
        }
        // search.fs_uuid <uuid> root
        if (word[0] == "search.fs_uuid" && word.size() >= 2) {
            return word[1];
        }
        // search [--no-floppy] --fs-uuid [--set=x] <uuid> -- the
        // first word that is not an option.
        bool byUuid = false;
        for (std::size_t i = 1; i < word.size(); ++i) {
            if (word[i] == "--fs-uuid" || word[i] == "-u") {
                byUuid = true;
            }
        }
        if (!byUuid) {
            continue;
        }
        for (std::size_t i = 1; i < word.size(); ++i) {
            if (word[i][0] == '-') {
                if (word[i] == "--set" && i + 1 < word.size()) {
                    ++i;  // "--set dev": the name, not the uuid
                }
                continue;
            }
            std::string uuid = word[i];
            if (uuid.size() >= 2 && (uuid.front() == '\'' || uuid.front() == '"')) {
                uuid = uuid.substr(1, uuid.size() - 2);
            }
            return uuid;
        }
    }
    return "";
}

std::string pickLoader(const std::vector<std::string>& files) {
    for (const std::string want :
         {"shimx64.efi", "grubx64.efi", "systemd-bootx64.efi"}) {
        for (const auto& file : files) {
            if (lower(file) == want) {
                return file;
            }
        }
    }
    return "";
}

bool skippedEfiDirectory(const std::string& directory) {
    static const std::set<std::string> skipped = {
        "boot", "tools", "dell", "hp", "lenovo", "asus", "acer",
        "microsoft"};
    // Microsoft is skipped on purpose: Nexus does not offer to start
    // Windows (decided 10 October -- Linux next to Linux only).
    return skipped.count(lower(directory)) > 0;
}

std::string entryIdFor(const EfiLoader& loader) {
    std::string id = "nexus-dualboot-";
    for (char c : lower(loader.directory)) {
        id += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    }
    return id;
}

std::vector<OtherSystem> matchSystems(
    std::vector<OtherSystem> systems,
    const std::vector<EfiLoader>& loaders,
    const std::vector<BlockDevice>& partitions
) {
    std::vector<bool> used(loaders.size(), false);

    // 1. The loader's grub.cfg names this system's root partition.
    for (auto& system : systems) {
        const BlockDevice* root = findByPath(partitions, system.partition);
        for (std::size_t i = 0; i < loaders.size(); ++i) {
            if (used[i] || loaders[i].target.empty() || !root) {
                continue;
            }
            if (lower(loaders[i].target) == lower(root->uuid)) {
                system.loader = loaders[i];
                used[i] = true;
                break;
            }
        }
    }

    // 2. One system and one loader left: they belong together. (A
    // system with a separate /boot points its stub at /boot, not at
    // its root.) Two or more of each: guessing could start the wrong
    // one, so they stay unpaired and say so.
    std::vector<OtherSystem*> lonely;
    for (auto& system : systems) {
        if (system.loader.file.empty()) {
            lonely.push_back(&system);
        }
    }
    std::vector<std::size_t> free;
    for (std::size_t i = 0; i < loaders.size(); ++i) {
        if (!used[i]) {
            free.push_back(i);
        }
    }
    if (lonely.size() == 1 && free.size() == 1) {
        lonely[0]->loader = loaders[free[0]];
        used[free[0]] = true;
    }

    // 3. Loaders nobody claimed are systems too -- an encrypted one
    // whose name cannot be read, say. Named after what is known.
    for (std::size_t i = 0; i < loaders.size(); ++i) {
        if (used[i]) {
            continue;
        }
        OtherSystem system;
        const BlockDevice* target = findByUuid(partitions, loaders[i].target);
        system.name = "the system started by EFI/" + loaders[i].directory;
        if (target) {
            system.partition = target->path;
            system.fstype = target->fstype;
            system.size = target->size;
        }
        system.loader = loaders[i];
        systems.push_back(system);
    }

    int number = 0;
    std::set<std::string> ids;
    for (auto& system : systems) {
        if (system.loader.file.empty()) {
            continue;
        }
        system.number = ++number;
        std::string id = entryIdFor(system.loader);
        const std::string base = id;
        for (int n = 2; ids.count(id); ++n) {
            id = base + "-" + std::to_string(n);
        }
        ids.insert(id);
        system.entryId = id;
    }
    return systems;
}

std::string grubEntry(const OtherSystem& system) {
    std::ostringstream out;
    out << "menuentry '" << menuTitle(system.name) << "' --id "
        << system.entryId << " {\n"
        << "\tinsmod part_gpt\n"
        << "\tinsmod fat\n"
        << "\tinsmod chain\n"
        << "\tsearch --no-floppy --fs-uuid --set=root "
        << system.loader.espUuid << "\n"
        << "\tchainloader " << system.loader.file << "\n"
        << "}\n";
    return out.str();
}

std::string grubBlock(const std::vector<OtherSystem>& systems) {
    std::ostringstream out;
    int count = 0;
    for (const auto& system : systems) {
        if (system.loader.file.empty() || system.entryId.empty()) {
            continue;
        }
        if (count++ == 0) {
            out << kBlockBegin << "\n"
                << "# Written by `nexus dualboot add`; `nexus dualboot "
                   "remove` takes it out.\n"
                << "# Each entry hands over to the other system's own "
                   "loader, on its own\n"
                << "# EFI partition. Nothing of the other system is "
                   "changed.\n"
                << "#\n"
                << "# Restarting into one of them once (`nexus dualboot "
                   "<number>`, or\n"
                << "# grub2-reboot) sets next_entry. Nexus's grub.cfg "
                   "does not read it\n"
                << "# (Asus, 12 October), so this part does: once, "
                   "then it is cleared.\n"
                << "if [ -n \"${next_entry}\" ]; then\n"
                << "\tset default=\"${next_entry}\"\n"
                << "\tset next_entry=\n"
                << "\tif [ -f \"${config_directory}/grubenv\" ]; then\n"
                << "\t\tsave_env -f \"${config_directory}/grubenv\" "
                   "next_entry\n"
                << "\telse\n"
                << "\t\tsave_env next_entry\n"
                << "\tfi\n"
                << "fi\n";
        }
        out << grubEntry(system);
    }
    if (count > 0) {
        out << kBlockEnd << "\n";
    }
    return out.str();
}

std::string replaceGrubBlock(const std::string& existing,
                             const std::string& block) {
    const auto begin = existing.find(kBlockBegin);
    std::string before = existing;
    std::string after;
    if (begin != std::string::npos) {
        const auto end = existing.find(kBlockEnd, begin);
        before = existing.substr(0, begin);
        if (end != std::string::npos) {
            auto next = existing.find('\n', end);
            after = next == std::string::npos ? "" : existing.substr(next + 1);
        }
    }
    std::string out = before;
    if (!block.empty()) {
        if (!out.empty() && out.back() != '\n') {
            out += '\n';
        }
        out += block;
    }
    out += after;
    return out;
}

std::vector<std::string> entryIdsInBlock(const std::string& customCfg) {
    std::vector<std::string> ids;
    const auto begin = customCfg.find(kBlockBegin);
    if (begin == std::string::npos) {
        return ids;
    }
    const auto end = customCfg.find(kBlockEnd, begin);
    std::istringstream lines(customCfg.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin));
    std::string line;
    while (std::getline(lines, line)) {
        const auto at = line.find(" --id ");
        if (line.rfind("menuentry ", 0) != 0 || at == std::string::npos) {
            continue;
        }
        std::istringstream rest(line.substr(at + 6));
        std::string id;
        rest >> id;
        ids.push_back(id);
    }
    return ids;
}

std::string sizeInWords(std::uint64_t bytes) {
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024 * 1024);
    char text[32];
    if (gib >= 1.0) {
        std::snprintf(text, sizeof text, "%.1f GiB", gib);
    } else {
        std::snprintf(text, sizeof text, "%.0f MiB",
                      static_cast<double>(bytes) / (1024.0 * 1024));
    }
    return text;
}

}  // namespace nexus::system
