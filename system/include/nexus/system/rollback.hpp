#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus rollback` and `nexus rebase` (0.1.73): going back, or to
// another build or edition, with a warning before anything is lost.
//
// The pure part: reading `ostree admin status`, and taking an image
// reference apart and putting it back together. Running rpm-ostree,
// and walking /etc, is the CLI's.

// One line of `ostree admin status`:
//   * default 03cba7...b575.0
//     default 5f4523...2764.0 (rollback)
struct OstreeDeployment {
    bool booted = false;
    std::string stateroot;   // "default"
    std::string id;          // "<checksum>.<serial>"
    std::string flag;        // "", "rollback", "staged", "pending"
};
std::vector<OstreeDeployment> parseOstreeAdminStatus(const std::string& text);

// ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde
// ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core@sha256:...
struct ImageRef {
    std::string transport;   // "ostree-image-signed:docker://"
    std::string repository;  // "ghcr.io/davidosdas-official/nexus-core"
    std::string tag;         // "kde" (empty when pinned to a digest)
    std::string digest;      // "sha256:..." (empty when following a tag)
};
std::optional<ImageRef> parseImageRef(const std::string& text);
std::string imageRefString(const ImageRef& ref);

// sha256: and 64 lowercase hex characters.
bool validDigest(const std::string& digest);

// The edition a Nexus image follows ("kde", "minimal"), from its tag;
// empty when pinned to a digest or not a Nexus image.
std::string editionOf(const ImageRef& ref);

}  // namespace nexus::system
