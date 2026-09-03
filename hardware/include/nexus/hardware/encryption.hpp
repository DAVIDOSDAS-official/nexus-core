#pragma once

#include <string>
#include <vector>

namespace nexus::hardware {

enum class Encrypted {
    Yes,
    No,
    Unknown
};

// What is protected when the machine is off.
//
// Full-disk encryption does nothing while a system is running and
// nothing against root. What it protects is a powered-off machine,
// and the usual gap is swap: memory contents written to disk in plain
// text beside an encrypted root, which surprises people who believe
// they are encrypted.
struct EncryptionReport {
    Encrypted root = Encrypted::Unknown;
    Encrypted home = Encrypted::Unknown;
    Encrypted swap = Encrypted::Unknown;

    bool hasSwap = false;

    // Compressed swap in RAM. It never reaches a disk, so it is not
    // a disk leak -- memory held in memory is memory being memory.
    bool hasZramSwap = false;
    bool homeIsSeparate = false;

    // How it is done, when it can be told: luks, ecryptfs.
    std::string method;

    std::vector<std::string> unreadable;

    // The condition worth naming on its own.
    bool swapLeaksMemory() const {
        return root == Encrypted::Yes && hasSwap &&
               swap == Encrypted::No;
    }

    std::vector<std::string> capabilities() const;
};

// Reads encryption state from /proc and /sys.
//
// Nothing is unlocked, mounted or executed; this only reads what the
// kernel already reports. The root is configurable so the detector
// can be pointed at a fixture tree.
class EncryptionDetector {
public:
    explicit EncryptionDetector(std::string root = "/");

    EncryptionReport detect() const;

private:
    std::string root_;
};

std::string toString(Encrypted state);

}
