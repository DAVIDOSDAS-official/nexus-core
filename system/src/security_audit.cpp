#include <nexus/system/security_audit.hpp>
#include <cstdint>
#include <arpa/inet.h>
#include <nexus/system/json.hpp>
#include <nexus/system/process.hpp>
#include <initializer_list>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <iomanip>
#include <sstream>

namespace nexus::system {

const std::vector<HardeningSetting>& hardeningTable() {
    static const std::vector<HardeningSetting> t = {
        {"kernel.kptr_restrict", 1, true, "hide kernel addresses from programs (makes exploits harder)"},
        {"kernel.dmesg_restrict", 1, true, "kernel log only for admins (`sudo dmesg`)"},
        {"kernel.unprivileged_bpf_disabled", 1, true, "no kernel BPF programs for normal users"},
        // 0.2.3: one program may no longer read another's memory unless it
        // started it (Fedora ships 0: anything you run could read the
        // browser's). Ubuntu's default. The one cost, said in the preview:
        // attaching gdb or strace to a program already running needs sudo.
        {"kernel.yama.ptrace_scope", 1, true, "a program may not read another's memory unless it started it (cost: gdb -p / strace -p need sudo)"},
        // 0.2.3: no replacing the running kernel without a restart. Nothing
        // on a desktop uses it (kdump is off); once on, it stays on until
        // the next start.
        {"kernel.kexec_load_disabled", 1, true, "no swapping in another kernel without a restart"},
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

std::string parseLockdown(const std::string& text) {
    const auto open = text.find('[');
    if (open == std::string::npos) return "";
    const auto close = text.find(']', open + 1);
    if (close == std::string::npos || close == open + 1) return "";
    const std::string mode = text.substr(open + 1, close - open - 1);
    for (const char c : mode)
        if (!(c >= 'a' && c <= 'z')) return "";
    return mode;
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

std::vector<std::string> parseActiveZones(const std::string& output) {
    std::vector<std::string> zones;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == ' ' || line[0] == '\t') continue;
        const auto end = line.find_first_of(" \t");
        zones.push_back(line.substr(0, end));
    }
    return zones;
}

std::string wideOpenRange(const std::string& ports) {
    std::istringstream in(ports);
    std::string port;
    while (in >> port) {
        const auto dash = port.find('-');
        const auto slash = port.find('/');
        if (dash == std::string::npos || slash == std::string::npos || dash > slash) continue;
        try {
            const int from = std::stoi(port.substr(0, dash));
            const int to = std::stoi(port.substr(dash + 1, slash - dash - 1));
            if (to - from >= 10000) return port;
        } catch (...) {
        }
    }
    return "";
}

bool policyRequiresKey(const std::string& policyJson, const std::string& scope,
                       const std::string& keyPath) {
    // Parsed, not searched (0.1.79, fourth outside review: a text scan
    // could find the words in the wrong place and say yes). Every
    // docker scope that covers Nexus's images -- the scope itself and
    // any more exact one under it, which would win for those images --
    // must hold a sigstoreSigned rule with this key.
    const auto parsed = parseJson(policyJson);
    if (!parsed.error.empty()) return false;
    const JsonValue& docker = parsed.value["transports"]["docker"];
    if (!docker.isObject()) return false;
    bool sawScope = false;
    for (const auto& [name, rules] : docker.fields) {
        const bool covers = name == scope ||
                            (name.size() > scope.size() && name.rfind(scope, 0) == 0 &&
                             (name[scope.size()] == '/' || name[scope.size()] == ':' || name[scope.size()] == '@'));
        if (!covers) continue;
        if (name == scope) sawScope = true;
        bool signedWithKey = false;
        if (rules.isArray()) {
            for (const auto& rule : rules.items) {
                if (rule["type"].asString() != "sigstoreSigned") continue;
                if (rule["keyPath"].asString() == keyPath) signedWithKey = true;
                for (const auto& k : rule["keyPaths"].items)
                    if (k.asString() == keyPath) signedWithKey = true;
            }
        }
        if (!signedWithKey) return false;
    }
    return sawScope;
}

namespace {

bool startsWithAny(const std::string& name, std::initializer_list<const char*> prefixes) {
    for (const char* p : prefixes)
        if (name.rfind(p, 0) == 0) return true;
    return false;
}

bool isContainerInterface(const std::string& name) {
    return name == "lo" ||
           startsWithAny(name, {"docker", "br-", "podman", "cni", "virbr", "veth", "lxc", "vnet"});
}

// "10.88.0.1/16" -> network and prefix, IPv4 only.
bool ipv4Network(const std::string& cidr, std::uint32_t& network, int& prefix) {
    const auto slash = cidr.find('/');
    if (slash == std::string::npos) return false;
    in_addr a{};
    if (inet_pton(AF_INET, cidr.substr(0, slash).c_str(), &a) != 1) return false;
    try { prefix = std::stoi(cidr.substr(slash + 1)); } catch (...) { return false; }
    if (prefix < 0 || prefix > 32) return false;
    const std::uint32_t mask = prefix == 0 ? 0 : 0xFFFFFFFFu << (32 - prefix);
    network = ntohl(a.s_addr) & mask;
    return true;
}

bool sameNetwork(const std::string& a, const std::string& b) {
    std::uint32_t na = 0, nb = 0;
    int pa = 0, pb = 0;
    return ipv4Network(a, na, pa) && ipv4Network(b, nb, pb) && na == nb && pa == pb;
}

}  // namespace

std::vector<LocalNetwork> parseIpAddresses(const std::string& output) {
    std::vector<LocalNetwork> out;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string index, name, family, cidr;
        if (!(words >> index >> name >> family >> cidr) || family != "inet") continue;
        out.push_back({name, cidr});
    }
    return out;
}

TrustedReach trustedReach(const ZoneInfo& zone, const std::vector<LocalNetwork>& local) {
    TrustedReach worst = TrustedReach::Nothing;
    auto raise = [&](TrustedReach r) {
        if (static_cast<int>(r) > static_cast<int>(worst)) worst = r;
    };
    std::istringstream in(zone.interfaces);
    std::string name;
    while (in >> name) {
        if (isContainerInterface(name)) raise(TrustedReach::Containers);
        else if (startsWithAny(name, {"tun", "tap", "wg", "proton", "pvpn", "ipv6leak", "tailscale", "zt", "nordlynx", "mullvad"}))
            raise(TrustedReach::Tunnel);
        else raise(TrustedReach::Network);
    }
    // IPv6 sources are not matched to a network yet: one is judged a
    // real network -- a false warning at worst, never a false ok.
    std::istringstream sources(zone.sources);
    std::string source;
    while (sources >> source) {
        bool containers = sameNetwork(source, "10.88.0.0/16") ||    // podman
                          sameNetwork(source, "172.17.0.0/16");     // docker
        for (const auto& l : local)
            if (isContainerInterface(l.interface) && sameNetwork(source, l.cidr)) containers = true;
        raise(containers ? TrustedReach::Containers : TrustedReach::Network);
    }
    return worst;
}

ZoneInfo parseZoneInfo(const std::string& output) {
    ZoneInfo info;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        const auto b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const auto colon = line.find(':', b);
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(b, colon - b);
        std::string value = line.substr(colon + 1);
        const auto v = value.find_first_not_of(" \t");
        value = v == std::string::npos ? "" : value.substr(v);
        if (key == "target") info.target = value;
        else if (key == "services") info.services = value;
        else if (key == "ports") info.ports = value;
        else if (key == "interfaces") info.interfaces = value;
        else if (key == "sources") info.sources = value;
    }
    return info;
}

// ---------------------------------------------------------------------
// "Starts by itself" (0.2.0, ledger R4-L2).

namespace {

bool endsWith(const std::string& text, const std::string& ending) {
    return text.size() > ending.size() &&
           text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

// Where an open folder really is, whatever links led to it.
std::string realPathOf(int fd) {
    char buffer[PATH_MAX + 1];
    const std::string link = "/proc/self/fd/" + std::to_string(fd);
    const ssize_t n = ::readlink(link.c_str(), buffer, PATH_MAX);
    return n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : std::string();
}

bool isInside(const std::string& homeReal, const std::string& path) {
    return !path.empty() &&
           (path == homeReal || path.rfind(homeReal + "/", 0) == 0);
}

struct Entry {
    std::string name;
    mode_t mode = 0;   // of the entry itself: a link is a link
};

// The names in an open folder, sorted; at most kAutoStartFolderLimit
// (more: `cut` is set). Takes the descriptor over and closes it.
std::vector<Entry> namesIn(int fd, bool& cut) {
    std::vector<Entry> entries;
    DIR* dir = ::fdopendir(fd);
    if (dir == nullptr) {
        ::close(fd);
        return entries;
    }
    while (const dirent* item = ::readdir(dir)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..") continue;
        if (entries.size() >= kAutoStartFolderLimit) {
            cut = true;
            break;
        }
        Entry entry;
        entry.name = name;
        struct stat info {};
        // The entry, not what it points to: nothing here is followed.
        if (::fstatat(::dirfd(dir), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
            entry.mode = info.st_mode;
        }
        entries.push_back(entry);
    }
    ::closedir(dir);
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return entries;
}

// Opens home/relative for listing. -1 when it is not there (said by
// nobody), or when it is not read (said in notRead): it cannot be
// opened, or it is somewhere other than under the home folder. The
// check is made on the folder that was opened, so nothing can be
// swapped between the check and the listing.
int openPlace(const std::string& homeReal, const std::string& home,
              const std::string& relative, std::vector<std::string>& notRead) {
    const std::string shown = "~/" + relative;
    const int fd = ::open((home + "/" + relative).c_str(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT && errno != ENOTDIR) {
            notRead.push_back(shown + " (could not be opened)");
        }
        return -1;
    }
    if (!isInside(homeReal, realPathOf(fd))) {
        notRead.push_back(shown + " (a link that leads outside your home folder)");
        ::close(fd);
        return -1;
    }
    return fd;
}

void noteCut(bool cut, const std::string& relative, std::vector<std::string>& notRead) {
    if (cut) {
        notRead.push_back("~/" + relative + " (only its first " +
                          std::to_string(kAutoStartFolderLimit) + " names)");
    }
}

// A folder where every entry counts, or every entry with one ending.
void listPlace(const std::string& homeReal, const std::string& home,
               const std::string& relative, const std::string& ending,
               const std::string& prefix, std::vector<std::string>& into,
               std::vector<std::string>& notRead) {
    const int fd = openPlace(homeReal, home, relative, notRead);
    if (fd < 0) return;
    bool cut = false;
    for (const Entry& entry : namesIn(fd, cut)) {
        if (!ending.empty() && !endsWith(entry.name, ending)) continue;
        into.push_back(prefix + entry.name);
    }
    noteCut(cut, relative, notRead);
}

// A folder of systemd user units: the units themselves, the drop-ins
// that change a unit (<unit>.d/*.conf) and the links that switch one
// on (<target>.wants/, .requires/, .upholds/).
void listUnits(const std::string& homeReal, const std::string& home,
               const std::string& relative, const std::string& prefix,
               std::vector<std::string>& into, std::vector<std::string>& notRead) {
    const int fd = openPlace(homeReal, home, relative, notRead);
    if (fd < 0) return;
    // namesIn closes fd; the folders inside are opened from a copy.
    const int again = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    bool cut = false;
    const std::vector<Entry> entries = namesIn(fd, cut);
    noteCut(cut, relative, notRead);

    for (const Entry& entry : entries) {
        const bool dropIns = endsWith(entry.name, ".d");
        const bool switchedOn = endsWith(entry.name, ".wants") ||
                                endsWith(entry.name, ".requires") ||
                                endsWith(entry.name, ".upholds");
        if (dropIns || switchedOn) {
            if (!S_ISDIR(entry.mode)) {
                // A link standing where the folder would be.
                if (S_ISLNK(entry.mode)) {
                    notRead.push_back("~/" + relative + "/" + entry.name +
                                      " (a link; not followed)");
                }
                continue;
            }
            const int inner = again < 0 ? -1 :
                ::openat(again, entry.name.c_str(),
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
            if (inner < 0) {
                notRead.push_back("~/" + relative + "/" + entry.name +
                                  " (could not be opened)");
                continue;
            }
            bool innerCut = false;
            for (const Entry& file : namesIn(inner, innerCut)) {
                if (dropIns && !endsWith(file.name, ".conf")) continue;
                into.push_back(prefix + entry.name + "/" + file.name);
            }
            noteCut(innerCut, relative + "/" + entry.name, notRead);
            continue;
        }
        for (const char* kind : {".service", ".timer", ".socket", ".path"}) {
            if (endsWith(entry.name, kind) && !S_ISDIR(entry.mode)) {
                into.push_back(prefix + entry.name);
                break;
            }
        }
    }
    if (again >= 0) ::close(again);
}

// Names joined with ", ", broken into lines of about `width`.
std::vector<std::string> wrappedNames(const std::vector<std::string>& names,
                                      std::size_t most, std::size_t width) {
    std::vector<std::string> lines(1);
    for (std::size_t i = 0; i < names.size() && i < most; ++i) {
        // A file name is somebody's text: nothing in it steers the
        // terminal (0.1.85 N3, 0.1.86 R2).
        const std::string name = printable(names[i]) +
                                 (i + 1 < names.size() ? "," : "");
        if (!lines.back().empty() && lines.back().size() + 1 + name.size() > width) {
            lines.emplace_back();
        }
        lines.back() += (lines.back().empty() ? "" : " ") + name;
    }
    if (names.size() > most) {
        lines.back() += " ... and " + std::to_string(names.size() - most) + " more";
    }
    return lines;
}

}  // namespace

AutoStarts findAutoStarts(const std::string& home) {
    AutoStarts found;
    if (home.empty() || home[0] != '/') {
        found.notRead.push_back("your home folder (not known)");
        return found;
    }
    char resolved[PATH_MAX + 1];
    if (::realpath(home.c_str(), resolved) == nullptr) {
        found.notRead.push_back("your home folder (could not be opened)");
        return found;
    }
    const std::string homeReal = resolved;

    // Desktop autostart, and what KDE runs at login and logout.
    listPlace(homeReal, home, ".config/autostart", ".desktop", "",
              found.starts, found.notRead);
    listPlace(homeReal, home, ".config/autostart-scripts", "", "autostart-scripts/",
              found.starts, found.notRead);
    listPlace(homeReal, home, ".config/plasma-workspace/env", "", "plasma-workspace/env/",
              found.starts, found.notRead);
    listPlace(homeReal, home, ".config/plasma-workspace/shutdown", "",
              "plasma-workspace/shutdown/", found.starts, found.notRead);
    // The person's own systemd units, in both folders systemd reads.
    listUnits(homeReal, home, ".config/systemd/user", "", found.starts, found.notRead);
    listUnits(homeReal, home, ".local/share/systemd/user", ".local/share/systemd/user/",
              found.starts, found.notRead);
    // Variables set for the whole session (LD_PRELOAD lives here if
    // anywhere).
    listPlace(homeReal, home, ".config/environment.d", ".conf", "environment.d/",
              found.starts, found.notRead);
    std::sort(found.starts.begin(), found.starts.end());

    // Shell and login files: there or not, nothing more.
    for (const char* file : {".bashrc", ".bash_profile", ".bash_login", ".profile",
                             ".zshenv", ".zprofile", ".zshrc", ".zlogin", ".xprofile",
                             ".config/fish/config.fish"}) {
        struct stat info {};
        if (::lstat((home + "/" + file).c_str(), &info) == 0 &&
            (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))) {
            found.shellFiles.push_back(file);
        }
    }
    listPlace(homeReal, home, ".config/fish/conf.d", ".fish", ".config/fish/conf.d/",
              found.shellFiles, found.notRead);
    return found;
}

std::vector<std::string> autoStartLines(const AutoStarts& found, bool& nothingFound) {
    std::vector<std::string> lines;
    nothingFound = found.starts.empty() && found.notRead.empty();

    if (found.starts.empty()) {
        lines.push_back(found.notRead.empty()
            ? "nothing set up in your autostart, user services or login scripts."
            : "nothing found where Nexus could look; see below.");
    } else {
        lines = wrappedNames(found.starts, 40, 60);
        lines.back() += "  (check you know each one)";
    }
    if (!found.shellFiles.empty()) {
        auto shell = wrappedNames(found.shellFiles, 40, 60);
        shell.front() = "runs in every new shell or at login: " + shell.front();
        shell.back() += "  (named, not judged)";
        lines.insert(lines.end(), shell.begin(), shell.end());
    }
    for (const std::string& place : found.notRead) {
        lines.push_back("not read: " + printable(place));
    }
    // What this line does not cover, every time: an "ok" here is about
    // one person's own folders and nothing else.
    lines.push_back("not looked at: what starts for the whole system (services and autostart");
    lines.push_back("in /etc and /usr), cron and at jobs, other accounts.");
    return lines;
}

// ---------------------------------------------------------------------
// Firewall suggestions (0.2.0, ledger item 11).

std::string firewallFix(const std::string& zone, const std::string& change) {
    const std::string what = " --zone=" + zone + " " + change;
    return "sudo firewall-cmd --permanent" + what + " && sudo firewall-cmd" + what;
}

std::vector<std::vector<std::string>> firewallChanges(const std::string& change) {
    return {{"firewall-cmd", "--permanent", change}, {"firewall-cmd", change}};
}

std::string firewallBackToSaved() {
    return "sudo firewall-cmd --reload   (back to the saved rules: every rule added for this"
           " session only is dropped, a VPN's or a game's too. To keep what runs now"
           " instead: sudo firewall-cmd --runtime-to-permanent)";
}

}  // namespace nexus::system
