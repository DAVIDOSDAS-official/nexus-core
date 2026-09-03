#include <nexus/source.hpp>

namespace nexus {

std::string toString(Source source) {
    switch (source) {
        case Source::Base:      return "base";
        case Source::Flatpak:   return "flatpak";
        case Source::Container: return "container";
        case Source::Nix:       return "nix";
        case Source::AppImage:  return "appimage";
        case Source::Detected:  return "detected";
    }

    return "base";
}

std::string describe(Source source) {
    switch (source) {
        case Source::Base:
            return "integrated, smallest, moves with the distribution";
        case Source::Flatpak:
            return "current and sandboxed, larger, weaker desktop "
                   "integration";
        case Source::Container:
            return "another distribution's package, behind a boundary";
        case Source::Nix:
            return "any version, side by side, its own model to learn";
        case Source::AppImage:
            return "one file, no install, no updates and no sandbox";
        case Source::Detected:
            return "already part of the machine";
    }

    return "";
}

bool isIsolated(Source source) {
    switch (source) {
        case Source::Flatpak:
        case Source::Container:
        case Source::Nix:
        case Source::AppImage:
            return true;

        case Source::Base:
        case Source::Detected:
            return false;
    }

    return false;
}

}
