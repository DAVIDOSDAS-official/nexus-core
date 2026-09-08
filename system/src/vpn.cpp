#include <nexus/system/vpn.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include <nexus/system/process.hpp>

namespace nexus::system {

namespace {

std::string firstLine(const std::string& text) {
    std::istringstream input(text);
    std::string line;

    std::getline(input, line);

    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r' ||
            line.back() == ' ')) {
        line.pop_back();
    }

    return line;
}

}

bool wireguardAvailable() {
    return commandExists("wg");
}

std::vector<std::string> TunnelSettings::missing() const {
    std::vector<std::string> absent;

    if (address.empty()) {
        absent.push_back(
            "address - this machine's address inside the tunnel, "
            "e.g. 10.0.0.2/24");
    }

    if (peerPublicKey.empty()) {
        absent.push_back(
            "peer key - the server's public key, from whoever runs "
            "it");
    }

    if (endpoint.empty()) {
        absent.push_back(
            "endpoint - where the server is, as host:port");
    }

    return absent;
}

KeyPair generateKeyPair() {
    KeyPair pair;

    if (!wireguardAvailable()) {
        pair.error =
            "wg is not installed. Install wireguard-tools first.";

        return pair;
    }

    const ProcessResult generated = runCommand("wg genkey", false);

    if (!generated.ok || generated.text.empty()) {
        pair.error = "wg genkey produced nothing";
        return pair;
    }

    pair.privateKey = firstLine(generated.text);
    pair.publicKey = publicKeyOf(pair.privateKey);

    if (pair.publicKey.empty()) {
        pair.error = "could not derive the public key";
    }

    return pair;
}

std::string publicKeyOf(const std::string& privateKey) {
    if (privateKey.empty() || !wireguardAvailable()) {
        return "";
    }

    // Through a pipe rather than as an argument: arguments are
    // visible to every process on the machine in /proc, and a private
    // key on a command line is a private key anybody can read.
    const ProcessResult derived = runCommand(
        "printf '%s' '" + privateKey + "' | wg pubkey", false);

    return derived.ok ? firstLine(derived.text) : "";
}

std::string renderTunnel(
    const TunnelSettings& settings,
    const std::string& privateKey
) {
    std::ostringstream out;

    out << "# Written by nexus. Keep this file to yourself: it\n"
        << "# contains a private key.\n"
        << "\n"
        << "[Interface]\n"
        << "PrivateKey = " << privateKey << "\n"
        << "Address = " << settings.address << "\n";

    if (!settings.dns.empty()) {
        out << "DNS = " << settings.dns << "\n";
    }

    out << "\n"
        << "[Peer]\n"
        << "PublicKey = " << settings.peerPublicKey << "\n"
        << "Endpoint = " << settings.endpoint << "\n"
        << "AllowedIPs = " << settings.allowedIps << "\n";

    if (settings.keepalive > 0) {
        out << "PersistentKeepalive = " << settings.keepalive << "\n";
    }

    return out.str();
}

WriteResult writeTunnel(
    const std::string& path,
    const std::string& contents,
    bool overwrite
) {
    WriteResult result;

    result.path = path;

    std::error_code error;

    if (!overwrite && std::filesystem::exists(path, error)) {
        result.error =
            path + " already exists. Nothing has been changed.";

        return result;
    }

    const std::filesystem::path file(path);

    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }

    // Created 0600 from the start rather than written and then
    // chmodded: between those two there is a moment when the key is
    // readable, and a race nobody would ever notice losing.
    const int descriptor =
        ::open(path.c_str(),
               O_WRONLY | O_CREAT | O_TRUNC,
               S_IRUSR | S_IWUSR);

    if (descriptor < 0) {
        result.error =
            std::string("could not create ") + path + ": " +
            std::strerror(errno);

        return result;
    }

    const ssize_t written = ::write(
        descriptor, contents.data(), contents.size());

    ::close(descriptor);

    if (written < 0 ||
        static_cast<std::size_t>(written) != contents.size()) {

        result.error = "could not write the whole file to " + path;
        return result;
    }

    // Verify rather than trust. A umask or a filesystem that does not
    // carry permissions would leave the key readable, and a secret
    // written wrongly is worse than one not written -- it looks like
    // it worked.
    struct stat information {};

    if (::stat(path.c_str(), &information) != 0) {
        result.error = "could not check the permissions on " + path;
        return result;
    }

    if ((information.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        ::unlink(path.c_str());

        result.error =
            path + " could not be made private, so it was removed. "
            "The key was not left readable.";

        return result;
    }

    result.ok = true;

    return result;
}

}
