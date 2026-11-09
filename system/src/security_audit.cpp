#include <nexus/system/security_audit.hpp>

#include <ctime>
#include <iomanip>
#include <sstream>

namespace nexus::system {

const std::vector<HardeningSetting>& hardeningTable() {
    static const std::vector<HardeningSetting> t = {
        {"kernel.kptr_restrict", 1, true, "hide kernel addresses from programs (makes exploits harder)"},
        {"kernel.dmesg_restrict", 1, true, "kernel log only for admins (`sudo dmesg`)"},
        {"kernel.unprivileged_bpf_disabled", 1, true, "no kernel BPF programs for normal users"},
        {"net.ipv4.conf.all.accept_redirects", 0, false, "ignore routing redirects (a laptop is not a router)"},
        {"net.ipv4.conf.default.accept_redirects", 0, false, "the same, for networks joined later"},
        {"net.ipv6.conf.all.accept_redirects", 0, false, "the same, for IPv6"},
        {"net.ipv4.conf.all.send_redirects", 0, false, "send no routing redirects"},
        {"net.ipv4.conf.all.accept_source_route", 0, false, "refuse packets that choose their own route"},
        {"net.ipv4.tcp_syncookies", 1, true, "keep working under a SYN flood"},
        {"net.ipv4.conf.all.rp_filter", 2, false, "drop packets with impossible sender addresses (loose: VPN-safe)", 1},
        {"fs.protected_symlinks", 1, true, "block symlink tricks in shared folders"},
        {"fs.protected_hardlinks", 1, true, "block hardlink tricks in shared folders"},
        {"fs.protected_fifos", 1, true, "block named-pipe tricks in /tmp"},
        {"fs.protected_regular", 1, true, "block file tricks in /tmp"},
    };
    return t;
}

std::vector<HardeningSetting> weakSettings(const std::map<std::string, int>& now) {
    std::vector<HardeningSetting> out;
    for (const auto& s : hardeningTable()) {
        const auto it = now.find(s.key);
        if (it == now.end()) continue;
        const bool fine = (s.atLeast ? it->second >= s.wanted : it->second == s.wanted) ||
                          (s.alsoFine >= 0 && it->second == s.alsoFine);
        if (!fine) out.push_back(s);
    }
    return out;
}

std::string hardeningFile(const std::vector<HardeningSetting>& settings) {
    std::string f =
        "# Written by nexus security harden --apply: Nexus's default hardening tier.\n"
        "# Undo: sudo nexus security harden off --apply\n";
    for (const auto& s : settings)
        f += "\n# " + s.why + "\n" + s.key + " = " + std::to_string(s.wanted) + "\n";
    return f;
}

std::vector<std::string> parseListeners(const std::string& ss) {
    // tcp   LISTEN 0 4096 0.0.0.0:80   0.0.0.0:*
    // udp   UNCONN 0 0    [::]:5353    [::]:*
    std::vector<std::string> out;
    std::istringstream in(ss);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream w(line);
        std::string proto, state, recv, send, local;
        if (!(w >> proto >> state >> recv >> send >> local)) continue;
        if (proto != "tcp" && proto != "udp") continue;
        const auto colon = local.rfind(':');
        if (colon == std::string::npos) continue;
        std::string addr = local.substr(0, colon);
        const auto pct = addr.find('%');
        if (pct != std::string::npos) addr = addr.substr(0, pct);  // "0.0.0.0%lo" interface suffix
        if (addr.rfind("127.", 0) == 0 || addr == "[::1]" || addr == "::1" ||
            local.find("%lo:") != std::string::npos)
            continue;
        // the DHCP clients that get this machine its address: not a service
        const std::string port = local.substr(colon + 1);
        if (proto == "udp" && (port == "68" || port == "546")) continue;
        out.push_back(proto + " " + local);
    }
    return out;
}

std::vector<std::string> emptyPasswordAccounts(const std::string& shadow) {
    std::vector<std::string> out;
    std::istringstream in(shadow);
    std::string line;
    while (std::getline(in, line)) {
        const auto c1 = line.find(':');
        if (c1 == std::string::npos || c1 == 0) continue;
        const auto c2 = line.find(':', c1 + 1);
        const std::string field = line.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
        if (field.empty()) out.push_back(line.substr(0, c1));
    }
    return out;
}

std::optional<long> daysSince(const std::string& iso, long nowEpoch) {
    if (iso.size() < 10) return std::nullopt;
    std::tm tm{};
    std::istringstream ts(iso.substr(0, 10));
    ts >> std::get_time(&tm, "%Y-%m-%d");
    if (ts.fail()) return std::nullopt;
    const long then = static_cast<long>(timegm(&tm));
    if (nowEpoch < then) return 0;
    return (nowEpoch - then) / 86400;
}

}  // namespace nexus::system
