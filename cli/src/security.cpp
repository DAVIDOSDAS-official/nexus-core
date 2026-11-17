// nexus security.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// ---------------------------------------------------------------------
// nexus security (0.1.75): the audit, and one tier of hardening.
int commandSecurity(const std::vector<std::string>& words, bool apply, const std::string& invocation) {
    namespace fs = std::filesystem;
    using nexus::system::commandExists;
    using nexus::system::runCommand;

    const std::string action = words.empty() ? "audit" : words[0];

    auto readSysctls = [] {
        std::map<std::string, int> now;
        for (const auto& s : nexus::system::hardeningTable()) {
            std::string fixed = "/proc/sys/";
            std::string k = s.key;
            for (auto& c : k) if (c == '.') c = '/';
            fixed += k;
            std::ifstream in(fixed);
            std::stringstream b; b << in.rdbuf();
            if (const auto v = nexus::system::parseSysctlInt(b.str())) now[s.key] = *v;
        }
        return now;
    };

    if (action == "harden") {
        const bool off = words.size() > 1 && words[1] == "off";
        if (off) {
            if (!fs::exists(nexus::system::kHardeningConf)) {
                std::cout << "Nexus has not hardened anything here; nothing to undo.\n";
                return 0;
            }
            std::cout << "Removes " << nexus::system::kHardeningConf
                      << "; the kernel's own values come back at the next restart.\n";
            if (!apply) { std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation << " --apply\n"; return 0; }
            if (!nexus::system::haveRootPrivileges()) { std::cout << "\nThis needs root:\n    sudo " << invocation << " --apply\n"; return 1; }
            nexus::system::TransactionRecord record;
            record.when = nexus::system::currentTimestamp();
            record.request = "security harden off";
            record.kind = nexus::system::TransactionKind::Remove;
            record.resolved = nexus::system::kHardeningConf;
            const std::string log = nexus::system::defaultTransactionLog();
            const std::string marker = nexus::system::beginTransaction(log, record);
            std::error_code ec;
            const bool ok = fs::remove(nexus::system::kHardeningConf, ec);
            record.outcome = ok ? "removed" : "failed"; record.succeeded = ok; record.exitCode = ok ? 0 : 1;
            nexus::system::finishTransaction(log, marker, record);
            std::cout << (ok ? "\nRemoved. Restart to get the kernel's own values back.\n" : "\nCould not remove it.\n");
            return ok ? 0 : 1;
        }
        const auto weak = nexus::system::weakSettings(readSysctls());
        if (weak.empty()) {
            std::cout << "Every setting in Nexus's default hardening tier is already on.\n";
            return 0;
        }
        std::cout << "Nexus's default hardening tier: settings that make attacks harder;\n"
                     "where one costs something, its line says what. These are weaker here:\n\n";
        std::size_t width = 0;
        for (const auto& w : weak) width = std::max(width, w.key.size() + 3 + std::to_string(w.wanted).size());
        for (const auto& w : weak)
            std::cout << "  " << std::left << std::setw(static_cast<int>(width + 2))
                      << (w.key + " = " + std::to_string(w.wanted)) << w.why << "\n";
        std::cout << "\nWritten to " << nexus::system::kHardeningConf << ", applied at once and at every start.\n"
                     "Undo any time: sudo nexus security harden off --apply\n"
                     "Not in this tier (they break things for some people): user-namespace\n"
                     "limits, and kernel lockdown (it refuses unsigned drivers, NVIDIA's too).\n";
        if (!apply) { std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation << " --apply\n"; return 0; }
        if (!nexus::system::haveRootPrivileges()) { std::cout << "\nThis needs root:\n    sudo " << invocation << " --apply\n"; return 1; }
        nexus::system::TransactionRecord record;
        record.when = nexus::system::currentTimestamp();
        record.request = "security harden";
        record.kind = nexus::system::TransactionKind::Install;
        record.resolved = nexus::system::kHardeningConf;
        for (const auto& w : weak) record.packages.insert(w.key);
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);
        bool ok = true;
        {
            std::error_code ec;
            fs::create_directories("/etc/sysctl.d", ec);
            std::ofstream out(nexus::system::kHardeningConf, std::ios::trunc);
            out << nexus::system::hardeningFile(weak);
            ok = static_cast<bool>(out);
        }
        if (ok) ok = runCommand(std::string("sysctl -q -p ") + nexus::system::kHardeningConf + " 2>&1", true).ok;
        record.outcome = ok ? "applied" : "failed"; record.succeeded = ok; record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);
        const auto still = nexus::system::weakSettings(readSysctls());
        std::cout << "\n" << (ok && still.empty() ? "Done: every setting in the tier is on, now and at every start.\n"
                                                  : "Not all applied; see: nexus security\n");
        return ok ? 0 : 1;
    }

    if (action != "audit") {
        std::cerr << "Use: nexus security                     the audit (changes nothing)\n"
                     "     nexus security harden --apply      Nexus's default hardening tier\n"
                     "     nexus security harden off --apply  undo it\n";
        return 2;
    }

    // ---- the audit
    int fails = 0, warns = 0, oks = 0;
    auto line = [&](const char* mark, const std::string& name, const std::string& detail,
                    const std::string& fix = "") {
        const std::string m = mark;
        if (m == "FAIL") ++fails; else if (m == "warn") ++warns; else if (m == " ok ") ++oks;
        std::cout << "  [" << mark << "]  " << std::left << std::setw(18) << name << detail << "\n";
        if (!fix.empty()) std::cout << std::string(28, ' ') << "fix: " << fix << "\n";
    };
    std::cout << "Security audit -- reads only, sends nothing, changes nothing\n\n";

    bool firewallOn = false;
    bool firewallKnown = false;
    if (commandExists("firewall-cmd")) {
        firewallKnown = true;
        firewallOn = firstLine(runCommand("firewall-cmd --state 2>&1", false).text) == "running";
        if (firewallOn) {
            // Every zone in use, as it runs now and as saved: the worse
            // of the two is what counts, and a difference is named
            // (0.1.77, second outside review).
            const auto zones = nexus::system::parseActiveZones(
                runCommand("firewall-cmd --get-active-zones 2>/dev/null", false).text);
            bool any = false;
            for (const auto& zone : zones) {
                const std::string z = shellQuote(zone);
                const auto now = nexus::system::parseZoneInfo(
                    runCommand("firewall-cmd --info-zone=" + z + " 2>/dev/null", false).text);
                const auto saved = nexus::system::parseZoneInfo(
                    runCommand("firewall-cmd --permanent --info-zone=" + z + " 2>/dev/null", false).text);
                std::string wide = nexus::system::wideOpenRange(now.ports);
                if (wide.empty()) wide = nexus::system::wideOpenRange(saved.ports);
                const bool acceptAll = now.target == "ACCEPT" || saved.target == "ACCEPT";
                const bool differs = now.target != saved.target || now.services != saved.services ||
                                     now.ports != saved.ports;
                std::string lets = now.services + (now.ports.empty() ? "" : (now.services.empty() ? "" : ", ") + now.ports);
                if (acceptAll) {
                    // What it lets everything in from is what counts.
                    using nexus::system::TrustedReach;
                    const auto reach = nexus::system::trustedReach(now, nexus::system::parseIpAddresses(
                        runCommand("ip -o -f inet addr show 2>/dev/null", false).text));
                    const std::string what = !now.sources.empty() ? "from " + now.sources
                                           : "on " + now.interfaces;
                    if (reach == TrustedReach::Network)
                        line("FAIL", "Firewall", "zone " + zone + " lets everything in " + what + ".",
                             // Saved and running rules both, nothing
                             // reloaded (0.2.0, ledger item 11): a
                             // reload drops every rule that was added
                             // for this session only.
                             !now.sources.empty()
                                 ? nexus::system::firewallFix(zone, "--remove-source=<range>")
                                 : nexus::system::firewallFix("public", "--change-interface=<name>"));
                    else if (reach == TrustedReach::Tunnel)
                        line("warn", "Firewall", "zone " + zone + " lets everything in " + what +
                             ": whoever is at the other end of that tunnel reaches every port.",
                             "if this machine serves nothing over it: " +
                             nexus::system::firewallFix("public", "--change-interface=<name>"));
                    else
                        line(" ok ", "Firewall", "zone " + zone + " lets everything in " + what +
                             " (this machine's own containers and VMs: they can reach every port here).");
                } else if (!wide.empty()) {
                    line("warn", "Firewall", "zone " + zone + " opens " + wide + ": any program listening there is reachable.",
                         nexus::system::firewallFix(zone, "--remove-port=" + wide));
                } else if (differs) {
                    line("warn", "Firewall", "zone " + zone + " runs with other rules than it has saved"
                         " (now: " + (lets.empty() ? std::string("nothing") : lets) + ").",
                         // The one reload that is meant: it says
                         // what goes with it (0.2.0, ledger item 11).
                         nexus::system::firewallBackToSaved());
                } else {
                    line(" ok ", "Firewall", "on, zone " + zone + (lets.empty() ? ": lets nothing in." : "; lets in: " + lets));
                }
                // ssh let through with remote login off (the Asus, 9 Nov:
                // a locally changed zone file keeps it past the image's
                // 0.1.71 change -- ostree keeps local /etc edits).
                {
                    std::istringstream sv(now.services + " " + saved.services);
                    std::string one;
                    bool ssh = false;
                    while (sv >> one) if (one == "ssh") ssh = true;
                    if (ssh && firstLine(runCommand("systemctl is-enabled sshd.service 2>/dev/null", false).text) != "enabled")
                        line("warn", "Firewall", "zone " + zone + " still lets ssh in, with remote login off:"
                             " turning sshd on would open it at once.",
                             nexus::system::firewallFix(zone, "--remove-service=ssh"));
                    // KDE Connect's ports with no KDE Connect (0.1.82).
                    std::istringstream kv(now.services + " " + saved.services);
                    bool kde = false;
                    while (kv >> one) if (one == "kdeconnect") kde = true;
                    if (kde && nexus::system::rpmDatabasePresent() &&
                        !runCommand("rpm -q kdeconnectd > /dev/null 2>&1", false).ok)
                        line("warn", "Firewall", "zone " + zone + " lets KDE Connect's ports in, and KDE Connect is"
                             " not installed.",
                             nexus::system::firewallFix(zone, "--remove-service=kdeconnect"));
                }
                any = true;
            }
            if (!any) line(" ok ", "Firewall", "on; no network connected right now.");
        } else {
            line("FAIL", "Firewall", "off: anything listening is reachable from the network.",
                 "sudo systemctl enable --now firewalld");
        }
    } else if (commandExists("ufw")) {
        // Pop!_OS, Ubuntu: ufw, installed and off unless turned on.
        // Its status needs root (the Acer, 9 November: without this the
        // audit said "not checked" and then "the firewall is off").
        if (nexus::system::haveRootPrivileges()) {
            const std::string st = firstLine(runCommand("ufw status 2>&1", false).text);
            firewallKnown = true;
            firewallOn = st == "Status: active";
            if (firewallOn) line(" ok ", "Firewall", "ufw is on.");
            else line("warn", "Firewall", "ufw is installed but off.", "sudo ufw enable");
        } else {
            line(" -- ", "Firewall", "ufw: not checked (needs sudo: sudo nexus security).");
        }
    } else {
        line(" -- ", "Firewall", "no firewalld or ufw; not checked.");
    }

    if (commandExists("ss")) {
        const auto listeners = nexus::system::parseListeners(runCommand("ss -tulnH 2>/dev/null", false).text);
        if (listeners.empty()) line(" ok ", "Listening", "nothing listens to the network.");
        else {
            std::string l;
            for (std::size_t i = 0; i < listeners.size() && i < 6; ++i) l += (i ? ", " : "") + listeners[i];
            if (listeners.size() > 6) l += ", ...";
            if (firewallOn) line(" ok ", "Listening", l + " (the firewall decides who reaches them)");
            else if (firewallKnown) line("warn", "Listening", l + " -- reachable, the firewall is off", "sudo ss -tulnp   (which program)");
            else line(" -- ", "Listening", l + " (firewall not checked)", "sudo ss -tulnp   (which program)");
        }
    }

    if (auto ssh = checkRemoteLogin()) {
        if (ssh->health == nexus::Health::Ok) line(" ok ", "Remote login", ssh->detail);
        else line("warn", "Remote login", ssh->detail, ssh->suggestion);
    }

    {
        const auto enc = nexus::hardware::EncryptionDetector("/").detect();
        // /home and swap on their own disks are checked too (0.1.76):
        // an encrypted system with a plain /home protects nothing personal.
        if (enc.root == nexus::hardware::Encrypted::Yes && enc.homeIsSeparate &&
            enc.home == nexus::hardware::Encrypted::No)
            line("warn", "Disk", "the system is encrypted but /home, on its own partition, is not.",
                 "only at install: reinstall with disk encryption on");
        else if (enc.swapLeaksMemory())
            line("warn", "Disk", "encrypted, but swap on disk is not: memory can reach it unencrypted.",
                 "use zram only, or encrypt the swap partition");
        else if (enc.root == nexus::hardware::Encrypted::Yes)
            line(" ok ", "Disk", "encrypted: a stolen laptop's files cannot be read.");
        else if (enc.root == nexus::hardware::Encrypted::No)
            line("warn", "Disk", "not encrypted: whoever has the laptop can read every file.",
                 "only at install: reinstall with disk encryption on");
    }

    {
        const auto sb = nexus::hardware::SecureBootDetector("/").detect();
        if (sb.state == nexus::hardware::SecureBootState::Enabled)
            line(sb.shimValidationDisabled ? "warn" : " ok ", "Secure Boot",
                 sb.shimValidationDisabled
                     ? "on, but shim's checking is turned off: it starts unsigned kernels."
                     : "on: the firmware starts only signed boot loaders and kernels.",
                 sb.shimValidationDisabled ? "sudo mokutil --enable-validation   (then restart)" : "");
        else if (sb.state == nexus::hardware::SecureBootState::Disabled)
            line("warn", "Secure Boot", "off: the firmware would start unsigned boot code too.",
                 "firmware settings -> Secure Boot; see: nexus secure-boot");
    }

    if (commandExists("getenforce")) {
        const std::string se = firstLine(runCommand("getenforce 2>/dev/null", false).text);
        if (se == "Enforcing") line(" ok ", "SELinux", "enforcing: programs are kept to what they may touch.");
        else if (se == "Permissive") line("warn", "SELinux", "permissive: only logs, blocks nothing.", "sudo setenforce 1");
        else if (se == "Disabled") line("FAIL", "SELinux", "disabled.", "enable it in /etc/selinux/config and restart");
    }

    {
        const auto deployments = readDeployments();
        for (const auto& d : deployments) {
            if (!d.booted) continue;
            // The prefix asks for a check; the policy says against
            // which key (0.1.76). Without the rule, signed: checks
            // nothing of Nexus's own. Decided in updateSigningOf, the
            // same answer the summary, doctor and specs give (0.1.95,
            // round-4 audit L1).
            const nexus::system::UpdateSigning signing = updateSigningOf(d.image);
            if (signing != nexus::system::UpdateSigning::NotChecked &&
                signing != nexus::system::UpdateSigning::NotAnImage) {
                if (signing == nexus::system::UpdateSigning::OtherSigned)
                    line(" ok ", "System image", "signature checked at every update (not a Nexus image).");
                else if (signing == nexus::system::UpdateSigning::Enforced) {
                    // containers-policy.json(5): a policy.json in the
                    // home of whoever pulls is read instead of /etc's
                    // (0.1.80). Updates are pulled as root, so root's.
                    std::error_code pe;
                    const bool rootHas = nexus::system::haveRootPrivileges() &&
                                         fs::exists("/root/.config/containers/policy.json", pe);
                    if (rootHas)
                        line("warn", "System image", "/root/.config/containers/policy.json exists and may be read instead of"
                             " /etc's, which requires the Nexus key.",
                             "sudo cat /root/.config/containers/policy.json   (remove it unless you put it there on purpose)");
                    else
                        line(" ok ", "System image", "every update must be signed with the Nexus key.");
                }
                else
                    line("FAIL", "System image", "signed: is set, but the policy does not require the Nexus key.",
                         "sudo python3 /usr/libexec/nexus/merge-policy");
            }
            else if (!d.image.empty())
                line("warn", "System image", "updates are not signature-checked: " + d.image,
                     "sudo nexus rebase signed --apply");
            const auto days = nexus::system::daysSince(d.date, static_cast<long>(std::time(nullptr)));
            if (days) {
                if (*days <= 14) line(" ok ", "Updates", *days == 0 ? std::string("system built today.")
                                                                    : "system built " + std::to_string(*days) + (*days == 1 ? " day ago." : " days ago."));
                else if (*days <= 30) line("warn", "Updates", "system built " + std::to_string(*days) + " days ago; security fixes come with updates.", "sudo nexus update --apply");
                else line("FAIL", "Updates", "system built " + std::to_string(*days) + " days ago: a month of security fixes missing.", "sudo nexus update --apply");
            }
        }
    }

    {
        // Containers by digest (0.1.81): what this image carries.
        std::ifstream pf(nexus::system::kPinsFile);
        if (pf) {
            std::stringstream pt; pt << pf.rdbuf();
            const auto pins = nexus::system::parsePins(pt.str());
            if (pins.empty())
                line("warn", "Containers", "this image pins no container images: Kali, servers and boxes come by tag.",
                     "sudo nexus update --apply   (a newer image may carry them)");
            else
                line(" ok ", "Containers", std::to_string(pins.size()) +
                     " images pinned by digest in this image (Kali, servers, boxes).");
        }
    }

    {
        const auto weak = nexus::system::weakSettings(readSysctls());
        const auto total = nexus::system::hardeningTable().size();
        if (weak.empty()) line(" ok ", "Kernel settings", "all " + std::to_string(total) + " of Nexus's hardening tier on.");
        else {
            std::string names;
            for (std::size_t i = 0; i < weak.size() && i < 2; ++i) names += (i ? ", " : "") + weak[i].key;
            if (weak.size() > 2) names += ", ...";
            line("warn", "Kernel settings", std::to_string(weak.size()) + " of " + std::to_string(total) +
                 " could be stricter (" + names + ").", "sudo nexus security harden --apply");
        }
    }

    {
        // Kernel lockdown (0.2.3): reported, never set by Nexus. On, it
        // stops even root from changing the running kernel -- and refuses
        // drivers that are not signed (NVIDIA's own among them), so it is
        // the owner's choice, not a default.
        std::ifstream in("/sys/kernel/security/lockdown");
        std::stringstream b; b << in.rdbuf();
        const std::string mode = nexus::system::parseLockdown(b.str());
        if (mode == "none")
            line(" -- ", "Kernel lockdown", "off: root can still change the running kernel.",
                 "kernel argument lockdown=integrity -- only if every driver here is signed; unsigned ones (NVIDIA's) stop loading");
        else if (!mode.empty())
            line(" ok ", "Kernel lockdown", mode + ": not even root can change the running kernel.");
    }

    {
        // The same name as every other Nexus machine (installs before
        // 0.1.80): two collide on one network, and the name is the same
        // for all of them. Information, not a fault.
        std::ifstream hn("/proc/sys/kernel/hostname");
        std::string host;
        std::getline(hn, host);
        if (host == "nexus")
            line(" -- ", "Name", "\"nexus\", as on every Nexus machine installed before 0.1.80.",
                 "sudo hostnamectl set-hostname nexus-$(od -An -N3 -tx1 /dev/urandom | tr -d ' \\n')");
    }

    if (nexus::system::haveRootPrivileges()) {
        std::ifstream sh("/etc/shadow");
        std::stringstream b; b << sh.rdbuf();
        const auto empty = nexus::system::emptyPasswordAccounts(b.str());
        if (empty.empty()) line(" ok ", "Passwords", "no account without a password.");
        else {
            std::string n;
            for (std::size_t i = 0; i < empty.size(); ++i) n += (i ? ", " : "") + empty[i];
            line("FAIL", "Passwords", "no password on: " + n, "sudo passwd <account>   (or: sudo passwd -l <account>)");
        }
    } else {
        line(" -- ", "Passwords", "not checked (needs sudo: sudo nexus security).");
    }

    {
        // What starts by itself for this person: where unwanted things
        // hide. Every place a login or a new shell runs something from
        // (0.2.0, ledger R4-L2; until then two folders, and "ok" about
        // the rest) -- see findAutoStarts for what is read and what is
        // not followed.
        // Under sudo, HOME may be root's: look at the person who ran sudo.
        std::string home;
        if (const char* su = std::getenv("SUDO_USER")) {
            if (const passwd* pw = getpwnam(su)) home = pw->pw_dir;
        } else if (const char* hh = std::getenv("HOME")) {
            home = hh;
        }
        if (!home.empty()) {
            bool nothing = false;
            const auto said = nexus::system::autoStartLines(
                nexus::system::findAutoStarts(home), nothing);
            line(nothing ? " ok " : " -- ", "Starts by itself", said.front());
            // The rest under it, where the fix line would be.
            for (std::size_t i = 1; i < said.size(); ++i) {
                std::cout << std::string(28, ' ') << said[i] << "\n";
            }
        }
    }

    std::cout << "\n" << oks << " ok, " << warns << " to look at, " << fails << " to fix.\n"
                 "\nNo virus scan: on Linux the risk is rarely a virus file; it is what is\n"
                 "open, what is out of date, and what starts by itself -- the lines above.\n";
    return fails > 0 ? 1 : 0;
}

}  // namespace nexus_cli
