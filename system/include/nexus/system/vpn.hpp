#pragma once

#include <string>
#include <vector>

namespace nexus::system {

struct KeyPair {
    std::string privateKey;
    std::string publicKey;

    std::string error;

    bool ok() const {
        return error.empty() && !privateKey.empty() &&
               !publicKey.empty();
    }
};

// What a WireGuard connection needs that Nexus cannot work out.
//
// The server's public key, where it lives and what to route through
// it all come from wherever you are connecting. There is no sensible
// default for any of them, and a config written with placeholders
// looks finished and does not work -- which is worse than no config,
// because the failure arrives later and looks like something else.
struct TunnelSettings {
    std::string interfaceName = "wg0";

    // This machine's address inside the tunnel, e.g. 10.0.0.2/24.
    std::string address;

    std::string peerPublicKey;

    // host:port of the server.
    std::string endpoint;

    // What to route through it. 0.0.0.0/0 sends everything;
    // 10.0.0.0/24 sends only the private network.
    std::string allowedIps = "0.0.0.0/0, ::/0";

    std::string dns;

    // Keeps a connection alive through NAT. 25 is conventional.
    int keepalive = 0;

    std::vector<std::string> missing() const;

    // What is given but cannot be right, one line each (0.1.76).
    //
    // wg-quick runs PreUp/PostUp lines from the config as root, so a
    // value with a line break in it is a command, not an address --
    // an outside review found that every field went into the file as
    // typed. Each field is checked for its own shape: addresses and
    // routes are IPs with an optional /prefix, DNS is IPs or search
    // domains, the endpoint is host:port, the key is WireGuard's
    // 44-character base64, the interface name is wg-quick's own rule.
    std::vector<std::string> invalid() const;
};

// Generate a key pair with wg.
//
// wg's own generator, not one written here: key generation is
// cryptography, it exists, and a second implementation would be a
// second chance to get it wrong.
KeyPair generateKeyPair();

std::string publicKeyOf(const std::string& privateKey);

// Render a config. The private key is included because a WireGuard
// config cannot work without one.
std::string renderTunnel(
    const TunnelSettings& settings,
    const std::string& privateKey
);

struct WriteResult {
    bool ok = false;
    std::string path;
    std::string error;
};

// Write a config that contains a private key.
//
// Refuses rather than writing a key that others could read: a secret
// written with the wrong permissions is worse than one not written,
// because it looks like it worked.
WriteResult writeTunnel(
    const std::string& path,
    const std::string& contents,
    bool overwrite
);

bool wireguardAvailable();

}
