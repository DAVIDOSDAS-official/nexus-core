#pragma once

// Container images by digest (0.1.81).
//
// An outside review: the Kali, Minecraft and Caddy containers were
// pulled by tag (`:latest`, `:2`), so a machine trusted whatever the
// publisher pushed that day, and two machines could get different
// bytes. The weekly image build now resolves every tag Nexus pulls
// (image/pins/images.list) to its digest and the signed image carries
// the list; `nexus` pulls `tag@digest`, which podman fetches by digest
// alone. A tag that is not in the list -- or a build without the list,
// like a local one -- is used as it is.

#include <map>
#include <string>

namespace nexus::system {

inline constexpr const char* kPinsFile = "/usr/share/nexus/pins/pinned-images";

// "ref sha256:<64 hex>" per line; # comments and anything malformed are
// skipped.
std::map<std::string, std::string> parsePins(const std::string& text);

// ref@digest when the list pins it, otherwise ref unchanged.
std::string pinnedImage(const std::string& ref, const std::string& pinsFile = kPinsFile);

}  // namespace nexus::system
