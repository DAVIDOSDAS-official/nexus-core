#include <nexus/system/rollback.hpp>

#include <cctype>
#include <sstream>

namespace nexus::system {

std::vector<OstreeDeployment> parseOstreeAdminStatus(const std::string& text) {
    std::vector<OstreeDeployment> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 3) continue;
        const bool booted = line[0] == '*';
        if (!(booted || (line[0] == ' ' && line[1] == ' ' && line[2] != ' '))) continue;
        std::istringstream w(line.substr(booted ? 1 : 0));
        OstreeDeployment d;
        d.booted = booted;
        std::string rest;
        if (!(w >> d.stateroot >> d.id)) continue;
        // "<64 hex>.<serial>"
        const auto dot = d.id.find('.');
        if (dot != 64) continue;
        bool hex = true;
        for (std::size_t i = 0; i < 64; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(d.id[i]))) hex = false;
        if (!hex) continue;
        std::string tail;
        std::getline(w, tail);
        const auto a = tail.find('('), b = tail.find(')');
        if (a != std::string::npos && b != std::string::npos && b > a) d.flag = tail.substr(a + 1, b - a - 1);
        out.push_back(d);
    }
    return out;
}

std::optional<ImageRef> parseImageRef(const std::string& text) {
    ImageRef r;
    std::string rest;
    const auto scheme = text.find("://");
    if (scheme != std::string::npos) {
        r.transport = text.substr(0, scheme + 3);
        rest = text.substr(scheme + 3);
    } else if (text.rfind("ostree-unverified-registry:", 0) == 0) {
        // rpm-ostree's own short form, no docker:// (0.1.84).
        r.transport = "ostree-unverified-registry:";
        rest = text.substr(r.transport.size());
    } else {
        return std::nullopt;
    }
    if (rest.empty()) return std::nullopt;
    const auto at = rest.find('@');
    if (at != std::string::npos) {
        r.repository = rest.substr(0, at);
        r.digest = rest.substr(at + 1);
        if (!validDigest(r.digest)) return std::nullopt;
    } else {
        // a ':' after the last '/' starts the tag (a ':' before it is a port)
        const auto slash = rest.rfind('/');
        const auto colon = rest.rfind(':');
        if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
            r.repository = rest.substr(0, colon);
            r.tag = rest.substr(colon + 1);
        } else {
            r.repository = rest;
            r.tag = "latest";
        }
    }
    if (r.repository.empty()) return std::nullopt;
    return r;
}

std::string transportFor(const ImageRef& ref) {
    if (ref.repository.rfind("ghcr.io/davidosdas-official/", 0) == 0) {
        return "ostree-image-signed:docker://";
    }
    return ref.transport;
}

std::string imageRefString(const ImageRef& ref) {
    if (!ref.digest.empty()) return ref.transport + ref.repository + "@" + ref.digest;
    return ref.transport + ref.repository + ":" + ref.tag;
}

bool validDigest(const std::string& digest) {
    if (digest.rfind("sha256:", 0) != 0 || digest.size() != 7 + 64) return false;
    for (std::size_t i = 7; i < digest.size(); ++i) {
        const char c = digest[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::string editionOf(const ImageRef& ref) {
    if (ref.digest.empty() && ref.repository.find("nexus-core") != std::string::npos) return ref.tag;
    return "";
}

}  // namespace nexus::system
