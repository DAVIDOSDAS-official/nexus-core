#include <nexus/system/vpn.hpp>
#include <utility>
#include <cctype>
#include <arpa/inet.h>

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

namespace {

std::string trimmed(const std::string& text) {
    const auto b = text.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    const auto e = text.find_last_not_of(" \t");
    return text.substr(b, e - b + 1);
}

std::vector<std::string> commaList(const std::string& text) {
    std::vector<std::string> out;
    std::string part;
    std::stringstream in(text);
    while (std::getline(in, part, ',')) out.push_back(trimmed(part));
    return out;
}

bool isIp(const std::string& text) {
    unsigned char buffer[sizeof(in6_addr)];
    return inet_pton(AF_INET, text.c_str(), buffer) == 1 ||
           inet_pton(AF_INET6, text.c_str(), buffer) == 1;
}

// 10.0.0.2, 10.0.0.2/24, fd00::2/64
bool isIpWithPrefix(const std::string& text) {
    const auto slash = text.find('/');
    const std::string ip = text.substr(0, slash);
    if (!isIp(ip)) return false;
    if (slash == std::string::npos) return true;
    const std::string prefix = text.substr(slash + 1);
    if (prefix.empty() || prefix.size() > 3) return false;
    for (char c : prefix) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    const int bits = std::stoi(prefix);
    return bits <= (ip.find(':') == std::string::npos ? 32 : 128);
}

bool isHostName(const std::string& text) {
    if (text.empty() || text.size() > 253 || text[0] == '-' || text[0] == '.') return false;
    for (unsigned char c : text)
        if (!std::isalnum(c) && c != '.' && c != '-') return false;
    return true;
}

bool isList(const std::string& text, bool (*each)(const std::string&)) {
    const auto parts = commaList(text);
    if (parts.empty()) return false;
    for (const auto& p : parts) if (!each(p)) return false;
    return true;
}

bool isIpOrDomain(const std::string& text) { return isIp(text) || isHostName(text); }

// host:port, 1.2.3.4:51820, [2001:db8::1]:51820
bool isEndpoint(const std::string& text) {
    std::string host, port;
    if (!text.empty() && text[0] == '[') {
        const auto close = text.find("]:");
        if (close == std::string::npos) return false;
        host = text.substr(1, close - 1);
        port = text.substr(close + 2);
        if (!isIp(host) || host.find(':') == std::string::npos) return false;
    } else {
        const auto colon = text.rfind(':');
        if (colon == std::string::npos) return false;
        host = text.substr(0, colon);
        port = text.substr(colon + 1);
        if (!isIp(host) && !isHostName(host)) return false;
    }
    if (port.empty() || port.size() > 5) return false;
    for (char c : port) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    const int n = std::stoi(port);
    return n >= 1 && n <= 65535;
}

// 32 bytes of base64: 43 characters and one '='.
bool isWireGuardKey(const std::string& text) {
    if (text.size() != 44 || text[43] != '=') return false;
    for (std::size_t i = 0; i < 43; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(c) && c != '+' && c != '/') return false;
    }
    return true;
}

bool hasLineBreak(const std::string& text) {
    for (unsigned char c : text) if (c < 0x20 && c != '\t') return true;
    return false;
}

}  // namespace

std::vector<std::string> TunnelSettings::invalid() const {
    std::vector<std::string> bad;
    const std::vector<std::pair<std::string, const std::string*>> fields = {
        {"interface", &interfaceName}, {"address", &address}, {"peer key", &peerPublicKey},
        {"endpoint", &endpoint}, {"routes", &allowedIps}, {"dns", &dns}};
    for (const auto& [label, value] : fields) {
        if (hasLineBreak(*value)) {
            bad.push_back(label + " - contains a line break or control character");
        }
    }
    if (!bad.empty()) return bad;   // the rest would only repeat it

    bool nameOk = !interfaceName.empty() && interfaceName.size() <= 15 &&
                  interfaceName != "." && interfaceName != "..";
    for (unsigned char c : interfaceName)
        if (!std::isalnum(c) && c != '_' && c != '=' && c != '+' && c != '.' && c != '-') nameOk = false;
    if (!nameOk) bad.push_back("interface - 1 to 15 letters, digits or _ = + . -, e.g. wg0");
    if (!address.empty() && !isList(address, isIpWithPrefix))
        bad.push_back("address - IP addresses with an optional /prefix, e.g. 10.0.0.2/24");
    if (!peerPublicKey.empty() && !isWireGuardKey(peerPublicKey))
        bad.push_back("peer key - a WireGuard public key is 44 characters ending in =");
    if (!endpoint.empty() && !isEndpoint(endpoint))
        bad.push_back("endpoint - host:port, e.g. vpn.example.com:51820 or [2001:db8::1]:51820");
    if (!allowedIps.empty() && !isList(allowedIps, isIpWithPrefix))
        bad.push_back("routes - networks like 0.0.0.0/0, ::/0 or 10.0.0.0/24");
    if (!dns.empty() && !isList(dns, isIpOrDomain))
        bad.push_back("dns - IP addresses (and search domains), e.g. 10.0.0.1");
    return bad;
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
        "printf '%s' " + shellQuote(privateKey) + " | wg pubkey", false);

    return derived.ok ? firstLine(derived.text) : "";
}

std::string renderTunnel(
    const TunnelSettings& settings,
    const std::string& privateKey
) {
    // Never a line the caller did not mean: a line break in any value
    // would add a directive of its own (PostUp runs as root).
    for (const std::string* v : {&settings.address, &settings.dns, &settings.peerPublicKey,
                                 &settings.endpoint, &settings.allowedIps, &privateKey}) {
        if (hasLineBreak(*v)) return {};
    }

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
