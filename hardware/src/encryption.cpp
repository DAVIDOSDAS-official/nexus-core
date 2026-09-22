#include <nexus/hardware/encryption.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace nexus::hardware {

namespace {

std::string readLine(const std::filesystem::path& path) {
    std::ifstream input(path);

    if (!input) {
        return "";
    }

    std::string line;

    std::getline(input, line);

    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }

    return line;
}

// Every device-mapper node, by the name it is known as.
std::map<std::string, std::string> mapperNodes(
    const std::filesystem::path& root
) {
    std::map<std::string, std::string> nodes;

    std::error_code error;

    const std::filesystem::path block = root / "sys/block";

    if (!std::filesystem::is_directory(block, error)) {
        return nodes;
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(block, error)) {

        const std::string node = entry.path().filename().string();

        if (node.rfind("dm-", 0) != 0) {
            continue;
        }

        const std::string name = readLine(entry.path() / "dm/name");

        if (!name.empty()) {
            nodes[name] = node;
        }
    }

    return nodes;
}

}

std::string toString(Encrypted state) {
    switch (state) {
        case Encrypted::Yes:     return "yes";
        case Encrypted::No:      return "no";
        case Encrypted::Unknown: return "unknown";
    }

    return "unknown";
}

std::vector<std::string> EncryptionReport::capabilities() const {
    std::vector<std::string> names;

    if (root == Encrypted::Yes) {
        names.push_back("encrypted-root");
    }

    if (home == Encrypted::Yes) {
        names.push_back("encrypted-home");
    }

    if (hasSwap && swap == Encrypted::Yes) {
        names.push_back("encrypted-swap");
    }

    return names;
}

EncryptionDetector::EncryptionDetector(std::string root)
    : root_(std::move(root)) {
}

EncryptionReport EncryptionDetector::detect() const {
    EncryptionReport report;

    const std::filesystem::path root(root_);

    const auto nodes = mapperNodes(root);

    // Is this block device built on a LUKS container, directly or
    // through anything stacked on top of it?
    //
    // Following the stack is the whole point: the common layout is
    // LVM on LUKS, where the mounted device is a logical volume whose
    // uuid says nothing about encryption. Checking only the top layer
    // reports an encrypted machine as unencrypted.
    std::set<std::string> visiting;

    std::function<bool(const std::string&)> backedByLuks =
        [&](const std::string& node) -> bool {
            if (node.empty() || !visiting.insert(node).second) {
                return false;
            }

            const std::filesystem::path directory =
                root / "sys/block" / node;

            const std::string uuid = readLine(directory / "dm/uuid");

            if (uuid.rfind("CRYPT-", 0) == 0) {
                return true;
            }

            std::error_code error;

            const std::filesystem::path slaves = directory / "slaves";

            if (!std::filesystem::is_directory(slaves, error)) {
                return false;
            }

            for (const auto& slave :
                 std::filesystem::directory_iterator(slaves, error)) {

                if (backedByLuks(slave.path().filename().string())) {
                    return true;
                }
            }

            return false;
        };

    const auto deviceIsEncrypted =
        [&](const std::string& device) -> Encrypted {
            // Not a device at all: composefs, overlay, tmpfs. What it
            // is built on is somewhere else, and this cannot see it
            // from here. That is "unknown", not "unencrypted" -- the
            // second is a claim about a disk nobody looked at.
            if (device.rfind("/dev/", 0) != 0) {
                return Encrypted::Unknown;
            }

            // /dev/mapper/<name> and /dev/dm-N both occur.
            std::string node;

            const std::string mapper = "/dev/mapper/";

            if (device.rfind(mapper, 0) == 0) {
                const auto found =
                    nodes.find(device.substr(mapper.size()));

                if (found != nodes.end()) {
                    node = found->second;
                }
            } else if (device.rfind("/dev/dm-", 0) == 0) {
                node = device.substr(5);
            }

            if (node.empty()) {
                // A plain partition: not encrypted, and that is a
                // fact rather than a failure to read.
                return Encrypted::No;
            }

            visiting.clear();

            return backedByLuks(node) ? Encrypted::Yes : Encrypted::No;
        };

    // Mounts.
    std::ifstream mounts(root / "proc/mounts");

    if (!mounts) {
        report.unreadable.push_back(
            "proc/mounts is not readable; nothing can be said about "
            "encryption");

        return report;
    }

    std::string line;

    // Where '/' comes from, and where the physical root is mounted if
    // this is an image-based system.
    //
    // On bootc and ostree, '/' is a composefs overlay assembled from
    // the deployment; the disk it lives on is mounted at /sysroot.
    // Judging '/' alone read "composefs", found it was not a mapper
    // device, and reported a LUKS-encrypted machine as unencrypted --
    // on the same boot that had just asked for its passphrase.
    //
    // The last '/' wins: later lines are mounted over earlier ones.
    std::string rootDevice;
    std::string sysrootDevice;

    while (std::getline(mounts, line)) {
        std::istringstream fields(line);

        std::string device;
        std::string point;
        std::string type;

        if (!(fields >> device >> point >> type)) {
            continue;
        }

        if (point == "/") {
            rootDevice = device;
        }

        if (point == "/sysroot") {
            sysrootDevice = device;
        }

        if (point == "/home") {
            report.homeIsSeparate = true;

            // eCryptfs encrypts a home directory on top of an
            // ordinary filesystem, so the device says nothing and the
            // type says everything.
            if (type == "ecryptfs") {
                report.home = Encrypted::Yes;
                report.method = "ecryptfs";
            } else {
                report.home = deviceIsEncrypted(device);
            }
        }
    }

    // Decided after reading every line, because the answer for '/'
    // can depend on a line that comes after it.
    if (!rootDevice.empty()) {
        report.root = deviceIsEncrypted(rootDevice);

        if (report.root == Encrypted::Unknown && !sysrootDevice.empty()) {
            report.root = deviceIsEncrypted(sysrootDevice);
        }

        if (report.root == Encrypted::Yes) {
            report.method = "luks";
        }
    }

    // A home that is not its own filesystem is as encrypted as root.
    if (!report.homeIsSeparate) {
        report.home = report.root;
    }

    // Swap: where memory goes to be written to disk.
    std::ifstream swaps(root / "proc/swaps");

    if (!swaps) {
        report.unreadable.push_back("proc/swaps is not readable");
        return report;
    }

    bool first = true;

    while (std::getline(swaps, line)) {
        if (first) {
            first = false;
            continue;
        }

        std::istringstream fields(line);

        std::string device;
        std::string type;

        if (!(fields >> device >> type)) {
            continue;
        }

        // zram is compressed swap held in RAM. It is not written to
        // a disk, so it cannot leak to one, and counting it as
        // unencrypted swap reports a machine with an encrypted swap
        // volume as leaking.
        if (device.rfind("/dev/zram", 0) == 0) {
            report.hasZramSwap = true;
            continue;
        }

        report.hasSwap = true;

        // A swap file is written through the filesystem it lives on,
        // so it is exactly as encrypted as that filesystem. Its path
        // is not a device, and treating it as one called every swap
        // file unencrypted -- including one inside an encrypted root.
        //
        // Assumed to live on root, which is true of the usual places
        // (/swapfile, /var/swapfile on an image-based system, where
        // /var is the same disk). A swap file on a separate data disk
        // would be judged by the wrong device; none of the layouts
        // this project installs does that.
        const Encrypted state = type == "file"
            ? report.root
            : deviceIsEncrypted(device);

        // Several swap areas: the weakest one is what matters.
        if (report.swap == Encrypted::Unknown ||
            state == Encrypted::No) {
            report.swap = state;
        }
    }

    if (!report.hasSwap) {
        report.swap = Encrypted::Unknown;
    }

    return report;
}

}
