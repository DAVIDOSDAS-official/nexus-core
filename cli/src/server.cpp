// nexus server.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// nexus server: Minecraft and websites on this machine, each in its own
// container (see system/include/nexus/system/server.hpp for how they
// run). Asked for 19-23 October; tested first on the Asus with a
// Minecraft server and a website.
//
// The same rules as everything else: the plain command only reports,
// `add`/`remove`/`backup`/`cockpit` say what they would do and do it
// only with --apply, and every change is in `nexus history`. Nothing
// is made reachable from the internet by Nexus: that is the router's
// (port forwarding) or a tunnel's, and the person's decision.
std::string trimmed(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

// Said after every server start (0.1.80, fifth outside review): the
// firewall now lets this port in, and who that means.
std::string reachableNote(const std::string& name, int port) {
    return "\nThe firewall now lets port " + std::to_string(port) + " in: every device on this\n"
           "network can reach it, and the internet too if your router forwards\n"
           "that port. Closed again with: sudo nexus server remove " + name + " --apply\n";
}

// The machine's address on the home network, for "connect to ...".
std::string lanAddress() {
    const auto result = nexus::system::runCommand("hostname -I 2>/dev/null", false);
    std::stringstream words(result.text);
    std::string word;
    while (words >> word) {
        if (word.find('.') != std::string::npos &&
            word.rfind("127.", 0) != 0 && word.rfind("10.88.", 0) != 0) {
            return word;
        }
    }
    return "this-computer's-address";
}

std::uint64_t totalMemoryBytes() {
    std::ifstream in("/proc/meminfo");
    std::string key;
    std::uint64_t kib = 0;
    std::string unit;
    while (in >> key >> kib >> unit) {
        if (key == "MemTotal:") {
            return kib * 1024;
        }
    }
    return 0;
}

bool firewallRunning() {
    return nexus::system::commandExists("firewall-cmd") &&
           nexus::system::runCommand("firewall-cmd --state 2>/dev/null", false).ok;
}

struct ServerSeen {
    std::string name;
    std::string kind;
    std::vector<std::string> ports;  // host side, "25565/tcp"
};

ServerSeen readServer(const std::string& name) {
    ServerSeen seen;
    seen.name = name;
    std::ifstream in(nexus::system::serverQuadletPath(name));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Label=nexus.kind=", 0) == 0) {
            seen.kind = line.substr(17);
        } else if (line.rfind("PublishPort=", 0) == 0) {
            const std::string spec = line.substr(12);           // 25565:25565/tcp
            const auto colon = spec.find(':');
            const auto slash = spec.find('/');
            if (colon != std::string::npos) {
                seen.ports.push_back(
                    spec.substr(0, colon) +
                    (slash != std::string::npos ? spec.substr(slash) : "/tcp"));
            }
        }
    }
    return seen;
}

std::vector<std::string> serverNames() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(
             "/etc/containers/systemd", ec)) {
        const std::string file = entry.path().filename().string();
        const std::string prefix = "nexus-";
        const std::string suffix = ".container";
        if (file.size() > prefix.size() + suffix.size() &&
            file.rfind(prefix, 0) == 0 &&
            file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0) {
            names.push_back(file.substr(prefix.size(),
                file.size() - prefix.size() - suffix.size()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string unitState(const std::string& unit) {
    const auto state = nexus::system::runCommand(
        "systemctl is-active " + unit + " 2>/dev/null", false);
    const std::string word = trimmed(state.text);
    return word.empty() ? "unknown" : word;
}

void serverUsage() {
    std::cerr
        << "Use: nexus server                       what runs here\n"
           "     nexus server add minecraft [--accept-eula] [--port N] [--memory 1280M]\n"
           "     nexus server add website <folder> [--port N] [--domain example.org]\n"
           "     nexus server logs <name>\n"
           "     nexus server backup <name>\n"
           "     nexus server remove <name>          (the data stays)\n"
           "     nexus server cockpit                manage this machine from a browser\n"
           "Add --apply to do it; without it nothing changes.\n";
}

nexus::system::TransactionRecord serverRecord(
    const std::string& request, nexus::system::TransactionKind kind,
    const std::string& resolved, const std::string& what) {
    nexus::system::TransactionRecord record;
    record.when = nexus::system::currentTimestamp();
    record.request = request;
    record.kind = kind;
    record.resolved = resolved;
    record.packages.insert(what);
    return record;
}

int needRoot(const std::string& invocation) {
    std::cout << "\nThis needs root. Re-run with sudo:\n    sudo " << invocation
              << " --apply\n\nNothing has been changed.\n";
    return 1;
}

bool openPorts(const std::vector<std::string>& ports, bool open) {
    if (!firewallRunning()) {
        return true;
    }
    bool ok = true;
    for (const std::string& port : ports) {
        ok = nexus::system::runCommand(
                 std::string("firewall-cmd --permanent ") +
                 (open ? "--add-port=" : "--remove-port=") + port + " 2>&1",
                 true).ok && ok;
    }
    nexus::system::runCommand("firewall-cmd --reload 2>&1", true);
    return ok;
}

// Download, write, start: the part both kinds share.
bool startServer(const std::string& name, const std::string& image,
                 const std::string& quadlet, const std::vector<std::string>& ports,
                 std::string& error) {
    std::cout << "\nDownloading " << image << " (first time only; it can take a few minutes):\n"
              << std::flush;
    if (std::system(("podman pull " + shellQuote(image)).c_str()) != 0) {
        error = "the download did not finish (podman pull " + image + ")";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories("/etc/containers/systemd", ec);
    {
        std::ofstream out(nexus::system::serverQuadletPath(name), std::ios::trunc);
        out << quadlet;
        out.flush();
        if (!out) {
            error = "could not write " + nexus::system::serverQuadletPath(name);
            return false;
        }
    }

    nexus::system::runCommand("systemctl daemon-reload 2>&1", true);
    const auto started = nexus::system::runCommand(
        "systemctl start " + nexus::system::serverUnit(name) + " 2>&1", true);
    if (!started.ok) {
        error = "systemctl start " + nexus::system::serverUnit(name) + ": " +
                firstLine(started.text);
        return false;
    }
    if (!openPorts(ports, true)) {
        error = "the server runs, but the firewall did not take the port";
        return false;
    }
    return true;
}

// Copy a website folder into the site directory, as root (0.1.76).
//
// A symlink is followed by the copy, so a link to /etc/shadow in the
// folder became a readable file on a public web server -- found by an
// outside review. Links are allowed only where they stay inside the
// folder. The copy goes to site.new first and replaces the site only
// when it is complete: a copy that fails leaves the running site as it
// was.
bool copyWebsite(const std::filesystem::path& folder, const std::string& site, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // Whose read rights count: the person who ran sudo (0.1.82).
    nexus::system::Reader reader;
    // sudo sets SUDO_UID, pkexec (the Shop) PKEXEC_UID (0.1.83).
    const char* su = std::getenv("SUDO_UID");
    if (su == nullptr) su = std::getenv("PKEXEC_UID");
    if (su != nullptr) {
        reader.uid = static_cast<uid_t>(std::strtoul(su, nullptr, 10));
        if (const passwd* pw = getpwuid(reader.uid)) {
            int n = 64;
            std::vector<gid_t> groups(static_cast<std::size_t>(n));
            if (getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &n) < 0) {
                groups.resize(static_cast<std::size_t>(n));
                getgrouplist(pw->pw_name, pw->pw_gid, groups.data(), &n);
            }
            groups.resize(static_cast<std::size_t>(std::max(n, 0)));
            reader.groups = groups;
        }
    }

    const std::string staged = site + ".new";
    fs::remove_all(staged, ec);
    fs::create_directories(staged, ec);
    // One pass, nothing followed, hidden files left out, and only what
    // the person could read themselves (system/src/site_copy.cpp).
    const auto copied = nexus::system::copySiteTree(folder.string(), staged, reader);
    if (!copied.ok) {
        error = copied.error;
        fs::remove_all(staged, ec);
        return false;
    }
    std::cout << "Copied " << copied.files << " file(s).\n";
    if (!copied.hidden.empty()) {
        std::cout << "Left out " << copied.hidden.size() << " hidden file(s) and folder(s), never published:";
        for (std::size_t k = 0; k < copied.hidden.size() && k < 5; ++k) std::cout << " " << copied.hidden[k];
        std::cout << (copied.hidden.size() > 5 ? " ...\n" : "\n");
    }
    if (!copied.serverSide.empty()) {
        std::cout << "Left out " << copied.serverSide.size()
                  << " server file(s) a web server would show as text (code, databases, backups):";
        for (std::size_t k = 0; k < copied.serverSide.size() && k < 5; ++k) std::cout << " " << copied.serverSide[k];
        std::cout << (copied.serverSide.size() > 5 ? " ...\n" : "\n");
    }
    if (!copied.unreadable.empty()) {
        std::cout << "Left out " << copied.unreadable.size() << " you cannot read yourself:";
        for (std::size_t k = 0; k < copied.unreadable.size() && k < 5; ++k) std::cout << " " << copied.unreadable[k];
        std::cout << (copied.unreadable.size() > 5 ? " ...\n" : "\n");
    }
    // The old site stays until the new one is in place (0.1.77, second
    // outside review: it was removed first, so a failed rename left no
    // site at all).
    const std::string old = site + ".old";
    fs::remove_all(old, ec);
    const bool hadSite = fs::exists(site);
    if (hadSite) {
        fs::rename(site, old, ec);
        if (ec) {
            error = "could not move the running site aside: " + ec.message();
            fs::remove_all(staged, ec);
            return false;
        }
    }
    fs::rename(staged, site, ec);
    if (ec) {
        error = "could not put the new copy in place: " + ec.message();
        std::error_code back;
        if (hadSite) fs::rename(old, site, back);
        fs::remove_all(staged, back);
        return false;
    }
    fs::remove_all(old, ec);
    return true;
}

}  // namespace

int commandServer(
    const std::vector<std::string>& raw,
    bool apply,
    const std::string& invocation
) {
    namespace fs = std::filesystem;
    using nexus::system::TransactionKind;

    // Its own options: main hands unknown words through as positional.
    std::vector<std::string> words;
    std::string portText, memory, domain;
    bool acceptEula = false;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == "--port" && i + 1 < raw.size()) {
            portText = raw[++i];
        } else if (raw[i] == "--memory" && i + 1 < raw.size()) {
            memory = raw[++i];
        } else if (raw[i] == "--domain" && i + 1 < raw.size()) {
            domain = raw[++i];
        } else if (raw[i] == "--accept-eula") {
            acceptEula = true;
        } else if (!raw[i].empty() && raw[i][0] == '-') {
            // "--accept-elua" on the Asus (24 October): say the right
            // word rather than only that this one is wrong.
            std::cerr << "Unknown option: " << raw[i] << "\n";
            for (const char* known :
                 {"--accept-eula", "--port", "--memory", "--domain"}) {
                if (closeSpelling(raw[i], known)) {
                    std::cerr << "Did you mean " << known << "?\n";
                    return 2;
                }
            }
            serverUsage();
            return 2;
        } else {
            words.push_back(raw[i]);
        }
    }

    int port = 0;
    if (!portText.empty()) {
        if (portText.find_first_not_of("0123456789") != std::string::npos ||
            portText.size() > 5 || std::stoi(portText) < 1 ||
            std::stoi(portText) > 65535) {
            std::cerr << "Not a port: " << portText << "\n";
            return 2;
        }
        port = std::stoi(portText);
    }
    if (!memory.empty() &&
        (memory.size() < 2 || (memory.back() != 'M' && memory.back() != 'G') ||
         memory.substr(0, memory.size() - 1).find_first_not_of("0123456789") !=
             std::string::npos)) {
        std::cerr << "Memory as 1280M or 2G, not: " << memory << "\n";
        return 2;
    }
    // Letters, digits, dots and hyphens also spell "--browse": the
    // value goes onto Caddy's command line, so each part of it must
    // start and end like a name (0.1.87, audit).
    if (!domain.empty() && !nexus::system::validDomainName(domain)) {
        std::cerr << "Not a domain name: " << domain << "\n";
        return 2;
    }

    const std::string action = words.empty() ? "" : words[0];
    const bool havePodman = nexus::system::commandExists("podman");

    // ---------------------------------------------------------- status
    if (action.empty()) {
        std::cout << "Servers on this machine\n\n";
        if (!havePodman) {
            std::cout << "  Podman is not installed, so no servers can run here.\n"
                         "  The hosting add-on brings it: sudo nexus setup hosting --apply\n";
            return 0;
        }
        const auto names = serverNames();
        if (names.empty()) {
            std::cout << "  None yet.\n";
        }
        for (const std::string& name : names) {
            const ServerSeen seen = readServer(name);
            std::string ports;
            for (const auto& p : seen.ports) {
                ports += (ports.empty() ? "" : ", ") + p;
            }
            std::cout << "  " << std::left << std::setw(14) << name
                      << std::setw(11) << (seen.kind.empty() ? "?" : seen.kind)
                      << std::setw(10) << unitState(nexus::system::serverUnit(name))
                      << "port " << ports << "\n";
        }

        std::cout << "\n  Address on your network: " << lanAddress() << "\n";
        std::cout << "  Firewall: "
                  << (firewallRunning() ? "on; Nexus opens only the ports its servers use."
                                        : "not running, so every port is open to your network.")
                  << "\n";
        const bool cockpitThere = fs::exists("/usr/lib/systemd/system/cockpit.socket");
        std::cout << "  Cockpit:  "
                  << (!cockpitThere ? "not installed (the hosting add-on brings it)."
                      : unitState("cockpit.socket") == "active"
                          ? "on: https://" + lanAddress() + ":9090"
                          : "installed, off: sudo nexus server cockpit --apply")
                  << "\n";
        std::cout << "\nFrom outside your home, a server needs port forwarding on your\n"
                     "router or a tunnel (Tailscale, Cloudflare Tunnel). Nexus does\n"
                     "neither by itself: that decision is yours.\n";
        return 0;
    }

    // ------------------------------------------------------------ logs
    if (action == "logs") {
        if (words.size() != 2) {
            serverUsage();
            return 2;
        }
        // A name, not a path or a typo: say what was wrong instead of
        // printing the usage (0.1.94; the Asus, 14 Nov: "my_test" and a
        // file path both got only the usage text).
        if (!nexus::system::validServerName(words[1])) {
            std::cout << "\"" << nexus::system::printable(words[1])
                      << "\" is not a server name: names use a-z, 0-9 and -,"
                         " up to 30 characters.\n`nexus server` lists the servers here.\n";
            return 2;
        }
        return std::system(("journalctl -u " + nexus::system::serverUnit(words[1]) +
                            " -n 80 --no-pager").c_str()) == 0 ? 0 : 1;
    }

    // ---------------------------------------------------------- cockpit
    if (action == "cockpit") {
        if (!fs::exists("/usr/lib/systemd/system/cockpit.socket")) {
            std::cout << "Cockpit is not installed. The hosting add-on brings it:\n"
                         "    sudo nexus setup hosting --apply\n"
                         "then restart, and run this again.\n";
            return 1;
        }
        if (unitState("cockpit.socket") == "active") {
            std::cout << "Cockpit is on: https://" << lanAddress() << ":9090\n"
                         "Log in with your normal user name and password.\n";
            return 0;
        }
        std::cout << "What Nexus will do\n"
                     "  Switch on Cockpit, now and at every start (cockpit.socket)\n";
        if (firewallRunning()) {
            std::cout << "  Open its port in the firewall (9090/tcp, your network)\n";
        }
        std::cout << "\nIt will not\n"
                     "  Make it reachable from the internet\n"
                     "  Create accounts: you log in as yourself\n";
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation
                      << " --apply\n";
            return 0;
        }
        if (!nexus::system::haveRootPrivileges()) {
            return needRoot(invocation);
        }
        auto record = serverRecord("server cockpit", TransactionKind::Install,
                                   "cockpit.socket, firewall service cockpit", "cockpit");
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);
        bool ok = nexus::system::runCommand(
            "systemctl enable --now cockpit.socket 2>&1", true).ok;
        if (ok && firewallRunning()) {
            nexus::system::runCommand("firewall-cmd --permanent --add-service=cockpit 2>&1", true);
            nexus::system::runCommand("firewall-cmd --reload 2>&1", true);
        }
        record.outcome = ok ? "switched on" : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);
        if (!ok) {
            std::cout << "\nCockpit did not start (systemctl enable --now cockpit.socket).\n";
            return 1;
        }
        std::cout << "\nDone. Open https://" << lanAddress() << ":9090 on any device in your\n"
                     "network and log in with your normal user name and password. The\n"
                     "browser warns about the certificate the first time: Cockpit made\n"
                     "its own, which is expected on a home network.\n";
        return 0;
    }

    // ------------------------------------------------- remove / backup
    if (action == "remove" || action == "backup") {
        if (words.size() != 2) {
            serverUsage();
            return 2;
        }
        // A name, not a path or a typo: say what was wrong instead of
        // printing the usage (0.1.94; the Asus, 14 Nov: "my_test" and a
        // file path both got only the usage text).
        if (!nexus::system::validServerName(words[1])) {
            std::cout << "\"" << nexus::system::printable(words[1])
                      << "\" is not a server name: names use a-z, 0-9 and -,"
                         " up to 30 characters.\n`nexus server` lists the servers here.\n";
            return 2;
        }
        const std::string name = words[1];
        if (!fs::exists(nexus::system::serverQuadletPath(name))) {
            std::cout << "There is no server called " << name << ". `nexus server` lists them.\n";
            return 1;
        }
        const ServerSeen seen = readServer(name);
        const std::string unit = nexus::system::serverUnit(name);
        const std::string data = nexus::system::serverDataDir(name);
        const std::string stamp = [] {
            std::string t = nexus::system::currentTimestamp();
            for (char& c : t) {
                if (c == ':') c = '-';
            }
            return t;
        }();
        const std::string archive =
            "/var/lib/nexus/backups/" + name + "-" + stamp + ".tar.gz";

        std::cout << "What Nexus will do\n";
        if (action == "remove") {
            std::cout << "  Stop " << name << " and remove it (" << unit << ")\n";
            if (firewallRunning() && !seen.ports.empty()) {
                std::cout << "  Close its port(s) in the firewall\n";
            }
            std::cout << "\nIt will not\n"
                         "  Delete its data: it stays in " << data << "\n";
        } else {
            std::cout << "  Stop " << name << " for a moment, so the copy is whole\n"
                      << "  Copy " << data << " to\n    " << archive << "\n"
                      << "  Start it again\n";
        }
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation
                      << " --apply\n";
            return 0;
        }
        if (!nexus::system::haveRootPrivileges()) {
            return needRoot(invocation);
        }

        auto record = serverRecord("server " + action + " " + name,
                                   action == "remove" ? TransactionKind::Remove
                                                      : TransactionKind::Install,
                                   action == "remove" ? unit : archive, name);
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);

        bool ok = true;
        std::string error;
        std::string notRestarted;
        int interrupted = 0;
        bool wasRunning = true;
        {
        // From "stopped" to "started again" in one piece: Ctrl-C or a
        // closed terminal in between is remembered, not obeyed, so a
        // server is never left down by an interrupted backup and the
        // record never stays at "started" (0.1.87, audit). tar itself
        // still stops on Ctrl-C; that shows below as a failed copy.
        const nexus::system::HoldSignals hold;
        // Started again unless it had been stopped on purpose: only a
        // plain "inactive" counts as that. One that was starting,
        // restarting after a crash, or failed gets its start as before.
        const auto unitState = [&unit] {
            return trimmed(nexus::system::runCommand(
                "systemctl is-active " + shellQuote(unit) + " 2>/dev/null", false).text);
        };
        wasRunning = nexus::system::startAgainAfterBackup(unitState());
        const auto stop = nexus::system::runCommand(
            "systemctl stop " + shellQuote(unit), true);
        // Stopped -- seen to be, not assumed (0.1.88, review of patch
        // 113). 0.1.87 ignored what `systemctl stop` answered and went
        // on: a copy taken from a server that was still writing is a
        // backup that may not restore, under a name that says it will;
        // and a unit file removed from under a running server leaves it
        // running with nothing to stop it by. What decides is the state
        // afterwards, not stop's exit code: stopping a server that was
        // never started "fails" (unit not loaded) and is stopped all
        // the same.
        const std::string stateAfterStop = unitState();
        if (nexus::system::unitRunState(stateAfterStop) != nexus::system::UnitRun::Stopped) {
            ok = false;
            const std::string said = firstLine(stop.text);
            error = name + " did not stop";
            if (!stop.ok && !said.empty()) {
                error += ": " + said;
            } else {
                error += stateAfterStop.empty()
                    ? std::string(" (systemd gave no answer about it)")
                    : " (systemd says it is " + nexus::system::printable(stateAfterStop) + ")";
            }
            error += std::string(". Nothing was ") +
                     (action == "remove" ? "removed" : "copied") + ".";
        } else if (action == "remove") {
            std::error_code ec;
            fs::remove(nexus::system::serverQuadletPath(name), ec);
            ok = !ec;
            if (!ok) error = ec.message();
            nexus::system::runCommand("systemctl daemon-reload 2>&1", true);
            openPorts(seen.ports, false);
        } else {
            // Root's only (0.1.85, outside review N2): the archive holds
            // what only root may read -- Caddy's TLS and ACME keys, a
            // Minecraft world -- and was 0644 in a 0755 folder. The
            // folder is 0700, the archive is made under umask 077 (no
            // moment when it is readable) and set 0600 after; old ones
            // in the folder are made 0600 too.
            std::error_code ec;
            fs::create_directories("/var/lib/nexus/backups", ec);
            fs::permissions("/var/lib/nexus/backups", fs::perms::owner_all, fs::perm_options::replace, ec);
            // Under another name until it is whole: a copy that was cut
            // short (no space, killed, Ctrl-C) used to stay in the
            // folder under a finished backup's name.
            const std::string partial = archive + ".partial";
            const mode_t before = ::umask(077);
            const auto tar = nexus::system::runCommand(
                "tar -czf " + shellQuote(partial) + " -C /var/lib/nexus/servers " + shellQuote(name),
                true);
            ::umask(before);
            ok = tar.ok;
            if (!ok) {
                error = firstLine(tar.text);
                if (error.empty()) {
                    error = "tar stopped (exit " + std::to_string(tar.exitCode) + ")";
                }
            } else {
                std::error_code moved;
                fs::rename(partial, archive, moved);
                ok = !moved;
                if (!ok) error = moved.message();
            }
            if (!ok) {
                std::error_code gone;
                fs::remove(partial, gone);
            }
            for (const auto& old : fs::directory_iterator("/var/lib/nexus/backups", ec)) {
                std::error_code pe;
                if (!old.is_regular_file(pe)) continue;
                // What a run that was killed outright left behind: never
                // a backup, only space.
                if (old.path().extension() == ".partial") {
                    fs::remove(old.path(), pe);
                    continue;
                }
                fs::permissions(old.path(), fs::perms::owner_read | fs::perms::owner_write,
                                fs::perm_options::replace, pe);
            }
        }
        // Back the way it was. After a backup, and after a stop that
        // did not work (a server that ran before must not be left
        // half-stopped); not after a removal that went through. And
        // seen to be running, not assumed from start's exit code.
        const bool removed = action == "remove" && ok;
        if (wasRunning && !removed) {
            const auto started = nexus::system::runCommand(
                "systemctl start " + shellQuote(unit), true);
            const std::string stateAtEnd = unitState();
            if (!started.ok) {
                notRestarted = firstLine(started.text);
                if (notRestarted.empty()) notRestarted = "systemctl start failed";
            } else if (nexus::system::unitRunState(stateAtEnd) != nexus::system::UnitRun::Running) {
                notRestarted = stateAtEnd.empty()
                    ? std::string("systemd gave no answer about it")
                    : "systemd says it is " + nexus::system::printable(stateAtEnd);
            }
        }
        interrupted = hold.arrived();
        }
        if (interrupted != 0 && !ok) {
            error = "stopped before it finished (interrupted)";
        }

        record.outcome = ok ? (action == "remove" ? "removed" : "backed up") : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);

        if (!ok) {
            std::cout << "\nNot done: " << error << "\n";
            if (!notRestarted.empty()) {
                std::cout << name << " was running before and did not start again: "
                          << notRestarted
                          << "\n    sudo systemctl start " << unit
                          << "\n    nexus server logs " << name << "\n";
            } else if (wasRunning) {
                std::cout << name << " is running.\n";
            }
            // Everything is back in place and recorded; now end the way
            // the person asked, so that a script or a shell loop around
            // this stops too instead of going on to the next server.
            if (interrupted != 0) {
                std::cout.flush();
                nexus::system::endBySignal(interrupted);
            }
            return 1;
        }
        if (action == "remove") {
            std::cout << "\nDone. " << name << " is gone; its data is still in " << data
                      << ".\n";
        } else {
            std::error_code ec;
            const auto size = fs::file_size(archive, ec);
            std::cout << "\nDone. " << archive;
            if (!ec) std::cout << " (" << nexus::system::sizeInWords(size) << ")";
            // Said only when it is so (0.1.87: it was printed whatever
            // `systemctl start` answered, or with a server that had
            // been stopped on purpose).
            if (!notRestarted.empty()) {
                std::cout << "\n\nThe copy is whole, but " << name
                          << " did not start again: " << notRestarted
                          << "\n    sudo systemctl start " << unit
                          << "\n    nexus server logs " << name << "\n";
                return 1;
            }
            std::cout << (wasRunning ? "\nThe server is running again.\n"
                                     : "\nThe server was not running before, and was left stopped.\n");
        }
        return 0;
    }

    // ------------------------------------------------------------- add
    if (action != "add" || words.size() < 2) {
        serverUsage();
        return 2;
    }
    const std::string kind = words[1];
    if (!havePodman) {
        std::cout << "Podman is not installed, so no servers can run here.\n"
                     "The hosting add-on brings it: sudo nexus setup hosting --apply\n";
        return 1;
    }

    if (kind == "minecraft") {
        if (words.size() != 2) {
            serverUsage();
            return 2;
        }
        nexus::system::MinecraftOptions options;
        options.memory = memory.empty()
            ? nexus::system::minecraftMemory(totalMemoryBytes()) : memory;
        if (port) options.port = port;
        if (fs::exists(nexus::system::serverQuadletPath(options.name))) {
            std::cout << "A server called minecraft exists already. `nexus server` shows it.\n";
            return 1;
        }
        const std::vector<std::string> ports = {std::to_string(options.port) + "/tcp"};

        std::cout << "What Nexus will do\n"
                  << "  Download the Minecraft server (Java edition, itzg/minecraft-server, ~0.6 GB)\n"
                  << "  Give it " << options.memory << " of memory ("
                  << nexus::system::sizeInWords(totalMemoryBytes()) << " here; the rest stays\n"
                     "  for the desktop -- fine for you and a few friends)\n"
                  << "  Keep the world in " << nexus::system::serverDataDir(options.name)
                  << "/data\n"
                  << "  Start it now and at every start: " << nexus::system::serverUnit(options.name)
                  << "\n";
        if (firewallRunning()) {
            std::cout << "  Open port " << options.port << "/tcp in the firewall (your network)\n";
        }
        std::cout << "\nIt will not\n"
                     "  Make it reachable from the internet (router or tunnel: your choice)\n"
                     "  Accept Mojang's licence for you\n";

        if (!acceptEula) {
            std::cout << "\nMinecraft's server needs you to accept Mojang's EULA:\n"
                         "    https://aka.ms/MinecraftEULA\n"
                         "Read it; if you agree, add --accept-eula:\n    sudo " << invocation
                      << " --accept-eula --apply\n\nNothing has been changed.\n";
            return apply ? 1 : 0;
        }
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation
                      << " --apply\n";
            return 0;
        }
        if (!nexus::system::haveRootPrivileges()) {
            return needRoot(invocation);
        }

        auto record = serverRecord("server add minecraft", TransactionKind::Install,
                                   nexus::system::serverUnit(options.name) + ", port " +
                                       std::to_string(options.port),
                                   "minecraft");
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);

        std::error_code ec;
        fs::create_directories(nexus::system::serverDataDir(options.name) + "/data", ec);
        std::string error;
        const bool ok = startServer(options.name, pinnedImage("docker.io/itzg/minecraft-server:latest"),
                                    nexus::system::minecraftQuadlet(options), ports, error);
        record.outcome = ok ? "started" : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);

        if (!ok) {
            std::cout << "\nNot done: " << error << "\n";
            return 1;
        }
        std::cout << "\nDone. The first start makes the world, about one or two minutes:\n"
                     "    nexus server logs minecraft     ends with \"Done\" when ready\n"
                     "\nIn Minecraft: Multiplayer -> Direct Connection -> "
                  << lanAddress() << ":" << options.port << "\n"
                  << reachableNote(options.name, options.port);
        return 0;
    }

    if (kind == "website") {
        if (words.size() != 3) {
            std::cout << "Which folder? nexus server add website <folder>\n"
                         "(the folder with index.html, for example your site's dist)\n";
            return 2;
        }
        nexus::system::WebsiteOptions options;
        options.domain = domain;
        if (port) options.port = port;
        const fs::path folder = fs::absolute(words[2]);
        if (!fs::is_directory(folder)) {
            std::cout << "No such folder: " << folder.string() << "\n";
            return 1;
        }
        // Already there: this is an update of its files, nothing else.
        const bool update = fs::exists(nexus::system::serverQuadletPath(options.name));
        const bool hasIndex = fs::exists(folder / "index.html");
        const std::string site = nexus::system::serverDataDir(options.name) + "/site";
        const std::vector<std::string> ports = options.domain.empty()
            ? std::vector<std::string>{std::to_string(options.port) + "/tcp"}
            : std::vector<std::string>{"80/tcp", "443/tcp"};

        if (update) {
            std::cout << "What Nexus will do\n"
                      << "  Replace the files in " << site << "\n"
                      << "  with " << folder.string() << "\n"
                      << "  Restart " << nexus::system::serverUnit(options.name) << "\n"
                      << "\nIt will not\n"
                         "  Change the port, the domain or anything else about it\n";
            if (!apply) {
                std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation
                          << " --apply\n";
                return 0;
            }
            if (!nexus::system::haveRootPrivileges()) {
                return needRoot(invocation);
            }
            auto record = serverRecord("server add website (update)", TransactionKind::Install,
                                       site + " from " + folder.string(), "website");
            const std::string log = nexus::system::defaultTransactionLog();
            const std::string marker = nexus::system::beginTransaction(log, record);
            std::string copyError;
            bool ok = copyWebsite(folder, site, copyError);
            if (!ok) std::cout << "\nNot updated: " << copyError << "\n";
            if (ok) {
                ok = nexus::system::runCommand(
                    "systemctl restart " + nexus::system::serverUnit(options.name) + " 2>&1",
                    true).ok;
            }
            record.outcome = ok ? "updated" : "failed";
            record.succeeded = ok;
            record.exitCode = ok ? 0 : 1;
            nexus::system::finishTransaction(log, marker, record);
            std::cout << (ok ? "\nDone. The website shows the new files.\n"
                             : "\nNot done: the files could not be copied or it did not restart.\n");
            return ok ? 0 : 1;
        }

        std::cout << "What Nexus will do\n"
                  << "  Copy " << folder.string() << " to " << site << "\n"
                  << "  Download Caddy, the web server (~0.05 GB)\n";
        if (options.domain.empty()) {
            std::cout << "  Serve it on port " << options.port << " (plain http, your network)\n";
        } else {
            std::cout << "  Serve it as https://" << options.domain
                      << " on ports 80 and 443, with a certificate Caddy gets itself\n";
        }
        std::cout << "  Start it now and at every start: "
                  << nexus::system::serverUnit(options.name) << "\n";
        if (firewallRunning()) {
            std::cout << "  Open the port(s) in the firewall\n";
        }
        std::cout << "\nIt will not\n"
                     "  Change your folder: the site is a copy; run this again to update it\n"
                     "  Make it reachable from the internet (router or tunnel: your choice)\n";
        if (!options.domain.empty()) {
            std::cout << "\nThe certificate only comes if " << options.domain
                      << " points at this machine and\nports 80 and 443 reach it from the internet.\n";
        }
        if (!hasIndex) {
            std::cout << "\nNote: there is no index.html in that folder, so the address will\n"
                         "show a file list or nothing.\n";
        }
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation
                      << " --apply\n";
            return 0;
        }
        if (!nexus::system::haveRootPrivileges()) {
            return needRoot(invocation);
        }

        auto record = serverRecord("server add website", TransactionKind::Install,
                                   nexus::system::serverUnit(options.name) + ", from " +
                                       folder.string(),
                                   "website");
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);

        std::string error;
        bool ok = copyWebsite(folder, site, error);
        if (ok) {
            ok = startServer(options.name, pinnedImage("docker.io/library/caddy:2"),
                             nexus::system::websiteQuadlet(options), ports, error);
        }
        record.outcome = ok ? "started" : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);

        if (!ok) {
            std::cout << "\nNot done: " << error << "\n";
            return 1;
        }
        std::cout << "\nDone. Open it from any device on your network:\n    "
                  << (options.domain.empty()
                          ? "http://" + lanAddress() + ":" + std::to_string(options.port)
                          : "https://" + options.domain)
                  << "\n" << reachableNote(options.name, options.port);
        return 0;
    }

    std::cout << "Nexus can add: minecraft, website. (" << kind << " is not one of them.)\n";
    return 2;
}

}  // namespace nexus_cli
