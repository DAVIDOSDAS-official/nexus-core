// nexus doctor, and the machine checks it shares.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// ---------------------------------------------------------------------
// Machine checks for doctor that need no package database.

// The clock. A machine whose clock is behind treats every package
// signed after that date as signed "in the future", and rpm silently
// skips it: on the Asus (1 October), with the clock days behind,
// Firefox and hunspell vanished from the package list, doctor reported
// four broken dependencies, and `why firefox` said "unknown".
nexus::Finding checkClock() {
    nexus::Finding f;
    f.check = "Clock";

    // Two signs. rpm's own complaint -- "signature is not alive ...
    // Not live until <date>" -- is the one that shows: the skipped
    // packages are exactly the newer ones, so the build dates of the
    // packages it does show can all look fine. The newest build date
    // is the second sign, for an rpm that skips without saying so.
    long newest = 0;
    bool notAlive = false;
    // Only where rpm has a database: on Debian or Ubuntu with rpm
    // installed as a tool, a query would create one in ~/.rpmdb.
    const auto built = nexus::system::rpmDatabasePresent()
        ? nexus::system::runCommand("rpm -qa --qf '%{BUILDTIME}\\n' 2>&1", false)
        : nexus::system::ProcessResult{};
    {
        std::istringstream in(built.text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("not alive") != std::string::npos ||
                line.find("Not live until") != std::string::npos) {
                notAlive = true;
                continue;
            }
            try {
                newest = std::max(newest, std::stol(line));
            } catch (...) {
            }
        }
    }

    const long now = static_cast<long>(std::time(nullptr));

    std::string ntp, synced, localRtc;
    if (nexus::system::commandExists("timedatectl")) {
        const auto show = nexus::system::runCommand(
            "timedatectl show -p NTP -p NTPSynchronized -p LocalRTC "
            "2>/dev/null", false);
        std::istringstream in(show.text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("NTP=", 0) == 0) ntp = line.substr(4);
            if (line.rfind("NTPSynchronized=", 0) == 0) synced = line.substr(16);
            if (line.rfind("LocalRTC=", 0) == 0) localRtc = line.substr(9);
        }
    }

    // How far off network time the clock is, as chrony sees it
    // (0.1.61). Network time "on" is not the same as right: Fedora's
    // chrony only set the clock straight in its first three updates,
    // so the Asus woke from sleep three days behind on 1 November,
    // with NTP on, and stayed there -- Firefox refused every HTTPS
    // site. Images from 0.1.61 set it straight whenever it is off;
    // this says so on machines that are off now.
    std::optional<double> offset;
    if (ntp == "yes" && nexus::system::commandExists("chronyc")) {
        const std::string prefix =
            nexus::system::commandExists("timeout") ? "timeout 5 " : "";
        const auto tracking = nexus::system::runCommand(
            prefix + "chronyc -n tracking 2>/dev/null", false);
        offset = nexus::system::parseChronyOffset(tracking.text);
    }
    constexpr double kOffBy = 60.0;  // seconds: past drift, a wrong clock
    // `chronyc makestep` sets it straight now; only with chrony there.
    const std::string stepNow =
        nexus::system::commandExists("chronyc")
            ? "sudo chronyc makestep"
            : "sudo timedatectl set-ntp true";

    if (notAlive || (newest > 0 && now + 3600 < newest)) {
        f.health = nexus::Health::Problem;
        f.detail = "The clock is behind: some installed packages were"
                   " built after the date it shows, and the package"
                   " database skips them.";
        if (offset && *offset > kOffBy) {
            f.detail += " It is " + nexus::system::describeOffset(*offset) +
                        " behind network time.";
        }
        f.examples.push_back("this makes installed programs look missing"
                             " and requirements look broken");
        f.total = 1;
        f.suggestion = ntp == "yes" ? stepNow
                                    : "sudo timedatectl set-ntp true";
    } else if (offset && *offset > kOffBy) {
        f.health = nexus::Health::Problem;
        f.detail = "The clock is " +
                   nexus::system::describeOffset(*offset) +
                   " behind network time and only catching up slowly."
                   " Websites' certificates look not yet valid, and"
                   " newer packages look signed in the future.";
        f.suggestion = stepNow;
    } else if (offset && *offset < -kOffBy) {
        f.health = nexus::Health::Warning;
        f.detail = "The clock is " +
                   nexus::system::describeOffset(*offset) +
                   " ahead of network time and only catching up slowly.";
        f.suggestion = stepNow;
    } else if (ntp == "no") {
        f.health = nexus::Health::Warning;
        f.detail = "Automatic time is off; the clock is only as right as"
                   " whoever last set it.";
        f.suggestion = "sudo timedatectl set-ntp true";
    } else if (localRtc == "yes") {
        f.health = nexus::Health::Warning;
        f.detail = "The hardware clock is kept in local time, as Windows"
                   " does. Fine with Windows on the same machine;"
                   " otherwise it shifts by an hour at daylight-saving"
                   " changes.";
        f.suggestion = "sudo timedatectl set-local-rtc 0   (no Windows"
                       " on this machine)";
    } else if (ntp == "yes" && synced == "no") {
        f.health = nexus::Health::Ok;
        f.detail = "Automatic time is on, not synchronised yet (offline?).";
    } else if (ntp == "yes") {
        f.health = nexus::Health::Ok;
        f.detail = "Right, and kept right automatically.";
    } else {
        f.health = nexus::Health::Unknown;
        f.detail = "Could not be checked.";
    }
    return f;
}

}  // namespace

// The time zone (0.1.61). With nothing chosen in the installer, the
// machine is on New York time -- hours off almost everywhere else, and
// nothing said so (the Asus, 12 October and 1 November). Not guessed:
// looking it up online stays opt-in (`sudo nexus-timezone auto on`).
// Empty when it cannot be read: a line that says "?" about the time
// zone helps nobody.
// Remote login (0.1.70). Fedora's base image starts sshd, and the
// firewall's public zone lets SSH in: on the Asus (8 November) anyone
// on the same Wi-Fi could try passwords -- "passwordauthentication
// yes". Not SSHole (that needs musl without PAM; Nexus is glibc with
// PAM), but a door a desktop does not need open. Images from 0.1.70
// ship with it closed; this says so on machines where it is open.
std::optional<nexus::Finding> checkRemoteLogin() {
    if (!nexus::system::commandExists("systemctl")) return std::nullopt;
    const auto active = nexus::system::runCommand(
        "systemctl is-active sshd.service 2>/dev/null", false).text;
    nexus::Finding f;
    f.check = "Remote login";
    if (active.rfind("active", 0) != 0) {
        f.health = nexus::Health::Ok;
        f.detail = "Off: nobody can log in to this machine over the network.";
        return f;
    }
    bool firewallLetsIn = true;
    if (nexus::system::commandExists("firewall-cmd")) {
        const auto q = nexus::system::runCommand(
            "firewall-cmd --query-service=ssh 2>/dev/null", false);
        if (q.text.rfind("no", 0) == 0) firewallLetsIn = false;
    }
    f.health = firewallLetsIn ? nexus::Health::Warning : nexus::Health::Ok;
    f.detail = firewallLetsIn
        ? "SSH is on and the firewall lets it in: anyone on your network can try to log in"
          " with your password. Fine if you log in remotely; otherwise close it."
        : "SSH is running, but the firewall keeps it closed to the network.";
    if (firewallLetsIn) f.suggestion = "sudo systemctl disable --now sshd";
    return f;
}

namespace {

// The boot check (0.1.72): did a new system version fail to show the
// desktop, and did Nexus go back? Read from what the check wrote.
std::optional<nexus::Finding> checkBootCheck() {
    const std::string dir = "/var/lib/nexus/boot-check";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return std::nullopt;

    nexus::Finding f;
    f.check = "Boot check";
    std::string last;
    {
        std::ifstream in(dir + "/failures.log");
        std::string line;
        while (std::getline(in, line)) if (!line.empty()) last = line;
    }
    // "2026-11-08T16:44:29Z <commit> desktop did not start ...; went back to <commit>"
    bool recent = false;
    if (last.size() >= 10) {
        std::tm tm{};
        std::istringstream ts(last.substr(0, 10));
        ts >> std::get_time(&tm, "%Y-%m-%d");
        if (!ts.fail()) {
            const std::time_t when = timegm(&tm);
            recent = std::time(nullptr) - when < 30L * 24 * 3600;
        }
    }
    // A version marked good (last-good written) after the last failure.
    bool goodSince = false;
    if (recent) {
        struct stat good {}, failures {};
        if (::stat((dir + "/last-good").c_str(), &good) == 0 &&
            ::stat((dir + "/failures.log").c_str(), &failures) == 0) {
            goodSince = good.st_mtime > failures.st_mtime;
        }
    }
    if (recent && last.find("TEST:") != std::string::npos) {
        // The test switch (sudo touch /var/lib/nexus/boot-check/test-fail):
        // nothing was wrong; the rollback was asked for.
        f.health = nexus::Health::Ok;
        f.detail = "Test rollback on " + last.substr(0, 10) +
                   " worked: Nexus went back by itself. Nothing was wrong.";
    } else if (recent && last.find("went back") != std::string::npos && goodSince) {
        // A version has started fine since then: the problem is behind
        // (the Asus, 11 November: still warned three days and several
        // good starts after the 8 November rollback).
        f.health = nexus::Health::Ok;
        f.detail = "On " + last.substr(0, 10) + " a version without a desktop was rolled back;"
                   " every version since has started fine.";
    } else if (recent && last.find("went back") != std::string::npos) {
        f.health = nexus::Health::Warning;
        f.detail = "On " + last.substr(0, 10) + " a new system version started without a desktop,"
                   " so Nexus went back to the one that worked. Updating again pulls the same"
                   " version until a fixed one is built.";
        f.suggestion = "nexus update   (check the date of what is offered before taking it)";
    } else if (recent) {
        f.health = nexus::Health::Warning;
        f.detail = "On " + last.substr(0, 10) + " the desktop did not start, and no earlier"
                   " version was known to work, so nothing was changed.";
    } else {
        f.health = nexus::Health::Ok;
        f.detail = "Each new system version is checked at its first start; if the desktop"
                   " does not come up, Nexus goes back to the one that worked.";
    }
    return f;
}

std::optional<nexus::Finding> checkTimezone() {
    if (!nexus::system::commandExists("timedatectl")) return std::nullopt;

    auto zone = nexus::system::runCommand(
        "timedatectl show -p Timezone --value 2>/dev/null", false).text;
    while (!zone.empty() && std::isspace(static_cast<unsigned char>(zone.back()))) {
        zone.pop_back();
    }
    if (zone.empty()) return std::nullopt;

    std::error_code ec;
    const bool chosen =
        std::filesystem::exists("/etc/nexus/timezone-chosen", ec);

    nexus::Finding f;
    f.check = "Time zone";
    if (nexus::system::timezoneNotChosen(zone, chosen)) {
        f.health = nexus::Health::Warning;
        f.detail = zone + ", what the installer sets when nothing is"
                   " chosen. Anywhere else the clock shows the wrong"
                   " hour.";
        f.suggestion = "nexus-timezone pick   (New York is right: "
                       "sudo nexus-timezone keep)";
    } else {
        f.health = nexus::Health::Ok;
        f.detail = zone + ".";
    }
    return f;
}

nexus::Finding checkMemory() {
    nexus::Finding f;
    f.check = "Memory";
    std::ifstream in("/proc/meminfo");
    std::string key;
    long value = 0;
    std::string unit;
    long total = 0, available = -1;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") total = value;
        if (key == "MemAvailable:") available = value;
    }
    if (total <= 0 || available < 0) {
        f.health = nexus::Health::Unknown;
        f.detail = "Could not be read.";
        return f;
    }
    const long usedMb = (total - available) / 1024;
    const long totalMb = total / 1024;
    const long percentFree = available * 100 / total;
    f.detail = std::to_string(usedMb) + " MB in use of " +
               std::to_string(totalMb) + " MB (" +
               std::to_string(percentFree) + "% free).";
    if (percentFree < 5) {
        f.health = nexus::Health::Problem;
        f.detail += " Almost none left; programs will be slow or closed.";
        f.suggestion = "close something, or: nexus services";
    } else if (percentFree < 15) {
        f.health = nexus::Health::Warning;
        f.detail += " Running low.";
    } else {
        f.health = nexus::Health::Ok;
    }
    return f;
}

// Storage. Not "/": on an image-based system that is the read-only
// image itself and always shows 100% full. Your files and everything
// added live under /var; old system versions under /boot.
nexus::Finding checkStorage() {
    nexus::Finding f;
    f.check = "Storage";

    struct Place { const char* path; const char* name; };
    const Place places[] = {{"/var", "your files and apps"},
                            {"/boot", "system versions"}};

    nexus::Health worst = nexus::Health::Ok;
    std::string details;
    std::set<unsigned long> seen;

    for (const auto& p : places) {
        struct statvfs st{};
        if (::statvfs(p.path, &st) != 0 || st.f_blocks == 0) {
            continue;
        }
        if (!seen.insert(st.f_fsid).second) {
            continue;
        }
        const double size = double(st.f_blocks) * st.f_frsize;
        const double free = double(st.f_bavail) * st.f_frsize;
        const int pct = int(free * 100.0 / size);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s: %.1f GB free of %.1f GB",
                      p.name, free / 1e9, size / 1e9);
        if (!details.empty()) details += "; ";
        details += buf;
        // /boot is small by design (2 GB on the Asus) and holds only
        // a few kernels, so it is judged by share alone; /var by share
        // and by size. The Asus's 1.6 GB free of 2.0 GB was a warning
        // under the old size rule (1 October).
        const bool small = std::string(p.path) == "/boot";
        if (pct < 3 || (!small && free < 1e9)) {
            worst = nexus::Health::Problem;
        } else if ((pct < (small ? 15 : 10) || (!small && free < 5e9)) &&
                   worst == nexus::Health::Ok) {
            worst = nexus::Health::Warning;
        }
    }

    if (details.empty()) {
        f.health = nexus::Health::Unknown;
        f.detail = "Could not be read.";
        return f;
    }
    f.health = worst;
    f.detail = details + ".";
    if (worst != nexus::Health::Ok) {
        f.suggestion = "nexus largest";
    }
    return f;
}

nexus::Finding checkServices() {
    nexus::Finding f;
    f.check = "Services";
    auto result = nexus::system::readServices();
    if (!result.error.empty() || !result.systemdRunning) {
        f.health = nexus::Health::Unknown;
        f.detail = "Could not be read.";
        return f;
    }
    if (result.failedCount == 0) {
        f.health = nexus::Health::Ok;
        f.detail = std::to_string(result.enabled) +
                   " start at boot; none has failed.";
        return f;
    }
    f.health = nexus::Health::Warning;
    f.detail = std::to_string(result.failedCount) + " service(s) failed.";
    for (const auto& s : result.services) {
        if (s.failed && f.examples.size() < 5) {
            f.examples.push_back(s.name);
        }
    }
    f.total = result.failedCount;
    f.suggestion = "systemctl --failed";
    return f;
}

}  // namespace

int commandDoctor(
    const SourceSummary& source,
    const std::vector<Component>& installed,
    const nexus::hardware::HardwareInfo& hardware,
    const nexus::hardware::EncryptionReport& encryption,
    const std::set<std::string>& roots,
    bool havePackages,
    const std::set<std::string>& protectedIds
) {
    std::vector<nexus::Finding> findings;

    // What is running comes before what it reports. Every finding
    // below describes a machine as seen by this binary, and knowing
    // which binary that is has to come first -- a report from an
    // unknown build cannot be checked against anything.
    nexus::Finding build;

    build.check = "Nexus build";
    build.detail =
        std::string("nexus ") + NEXUS_VERSION +
        " (" + NEXUS_COMMIT + ").";

    // A build that does not know its own commit cannot be matched to
    // the source it came from, which is exactly the question worth
    // asking when something here looks wrong.
    if (std::string(NEXUS_COMMIT).find("unknown") != std::string::npos) {
        build.health = nexus::Health::Warning;
        build.detail += " This build carries no commit, so it cannot"
                        " be matched to a source tree.";
    } else if (std::string(NEXUS_COMMIT).find("-dirty") !=
               std::string::npos) {
        build.health = nexus::Health::Warning;
        build.detail += " Built from a modified tree.";
    } else {
        build.health = nexus::Health::Ok;
    }

    findings.push_back(std::move(build));

    // The clock before the package database: when it is behind, the
    // database is read with packages missing, and everything below
    // that depends on it is wrong in ways that look like real faults.
    const nexus::Finding clock = checkClock();
    const bool clockBehind = clock.health == nexus::Health::Problem;
    findings.push_back(clock);
    if (auto zone = checkTimezone()) findings.push_back(std::move(*zone));
    if (auto ssh = checkRemoteLogin()) findings.push_back(std::move(*ssh));
    if (auto boot = checkBootCheck()) findings.push_back(std::move(*boot));

    // Whether the system can be examined at all: every finding
    // below this one is worthless if it failed.
    nexus::Finding source_;

    source_.check = "Package database";

    if (havePackages && !source.incomplete.empty()) {
        source_.health = nexus::Health::Warning;
        source_.detail =
            std::to_string(source.components.size()) +
            " components read from " + source.name +
            ", but the read was incomplete.";
        source_.examples.push_back(source.incomplete);
        source_.total = 1;
        source_.suggestion = "nexus doctor   (again; if it persists, "
                             "rpm -qa | wc -l)";
    } else if (havePackages) {
        source_.health = nexus::Health::Ok;
        source_.detail =
            std::to_string(source.components.size()) +
            " components read from " + source.name + ".";
    } else {
        source_.health = nexus::Health::Problem;
        source_.detail = "No package database could be read.";
        source_.suggestion = "nexus scan";
    }

    findings.push_back(std::move(source_));

    nexus::Finding metal;

    metal.check = "Hardware";

    if (hardware.unreadable.empty()) {
        metal.health = nexus::Health::Ok;
        metal.detail =
            std::to_string(hardware.capabilities().size()) +
            " capabilities detected.";
    } else {
        // Unreadable is not broken. A container has no EFI variables
        // and that is correct, not a fault.
        metal.health = nexus::Health::Warning;
        metal.detail =
            std::to_string(hardware.unreadable.size()) +
            " thing(s) could not be read.";
        metal.examples = hardware.unreadable;
        metal.total = hardware.unreadable.size();
        metal.suggestion = "nexus hardware";
    }

    findings.push_back(std::move(metal));

    // What is protected when the machine is off. Full-disk encryption
    // does nothing while it is running and nothing against root, so
    // the finding says what it covers rather than declaring the
    // system encrypted.
    nexus::Finding locked;

    locked.check = "Encryption";

    if (!encryption.unreadable.empty()) {
        locked.health = nexus::Health::Unknown;
        locked.detail = "Could not be determined.";
        locked.examples = encryption.unreadable;
        locked.total = encryption.unreadable.size();
    } else if (encryption.root == nexus::hardware::Encrypted::Unknown) {
        // Not known is not "no". This used to fall into the branch
        // below, so a root the detector could not see through was
        // announced as readable by anyone with the disk -- a warning
        // stated as fact, on a machine that had asked for its disk
        // passphrase on the same boot.
        locked.health = nexus::Health::Unknown;
        locked.detail =
            "Could not tell what the root filesystem is stored on.";
        locked.suggestion = "lsblk -o NAME,TYPE,FSTYPE,MOUNTPOINTS";
    } else if (encryption.root != nexus::hardware::Encrypted::Yes) {
        locked.health = nexus::Health::Warning;
        locked.detail =
            "The root filesystem is not encrypted; anyone with the "
            "disk can read it.";
    } else if (encryption.swapLeaksMemory()) {
        // The gap almost nobody notices.
        locked.health = nexus::Health::Warning;
        locked.detail =
            "Root is encrypted, swap is not.";
        locked.examples.push_back(
            "swap holds memory contents in plain text on disk");
        locked.total = 1;
    } else {
        locked.health = nexus::Health::Ok;

        locked.detail = "Root";

        if (encryption.homeIsSeparate) {
            locked.detail +=
                encryption.home == nexus::hardware::Encrypted::Yes
                    ? " and home"
                    : " but not home";
        }

        if (encryption.hasSwap) {
            locked.detail +=
                encryption.swap == nexus::hardware::Encrypted::Yes
                    ? " and swap"
                    : "";
        }

        locked.detail += " encrypted";

        if (!encryption.method.empty()) {
            locked.detail += " (" + encryption.method + ")";
        }

        locked.detail +=
            ". This protects the machine when it is off, not while it "
            "is running.";
    }

    findings.push_back(std::move(locked));

    findings.push_back(checkMemory());
    findings.push_back(checkStorage());
    findings.push_back(checkServices());

    if (havePackages && !source.incomplete.empty()) {
        // Not judged. Every "nothing installed satisfies this" would be
        // a claim about packages this read never saw -- 305 of them,
        // on the laptop where this was found, libsystemd included.
        nexus::Finding deps;

        deps.check = "Dependencies";
        deps.health = nexus::Health::Unknown;
        deps.detail =
            "Not judged: the package list was read incompletely, so "
            "anything it lacks may be missing from the read rather "
            "than from the machine.";

        findings.push_back(std::move(deps));
    } else if (havePackages) {
        const auto detail = nexus::diagnose(
            installed, roots,
            buildDetector(),
            protectedIds);

        for (nexus::Finding finding : detail.findings) {
            // With the clock behind, "nothing installed satisfies this"
            // is the database skipping packages, not the machine
            // missing them (the Asus, 1 October). Said as what it is.
            if (clockBehind && finding.health == nexus::Health::Problem) {
                finding.health = nexus::Health::Unknown;
                finding.detail =
                    "Not judged while the clock is behind (see Clock): " +
                    finding.detail;
            }
            // "Unused" with nothing to judge it against is a question
            // mark on a healthy machine (the Asus, 2 November): on an
            // image-based system nothing records what was wanted, so
            // the line could never say anything. Shown only when it can.
            if (finding.check == "Unused" &&
                finding.health == nexus::Health::Unknown) {
                continue;
            }
            findings.push_back(finding);
        }
    }

    // Whether this machine can receive updates at all.
    //
    // An image-based system updates by pulling a newer image from
    // where it was installed from. Installed from a local build,
    // there is nowhere to pull from and no fix can ever arrive --
    // which nothing else reports, because everything works.
    if (nexus::system::commandExists("bootc")) {
        // --format=json is not understood by every version, and a
        // check that gives up without saying why is a check that
        // reports a healthy machine it never examined.
        auto status = nexus::system::runCommand(
            "bootc status --format=yaml 2>/dev/null", false);

        if (!status.ok || status.text.empty()) {
            status = nexus::system::runCommand(
                "bootc status 2>/dev/null", false);
        }

        // bootc status needs root. Run as a normal user -- which is
        // how anybody runs doctor -- it printed nothing, and this
        // check reported "did not report a status" on every machine,
        // including the one it exists for: installed from localhost
        // and unable ever to update. Found in the VM on 22 September.
        //
        // rpm-ostree reads the same deployment and answers without
        // root, and its output names the image the machine came from,
        // which is all the localhost test below looks for.
        if (!status.ok || status.text.empty()) {
            status = nexus::system::runCommand(
                "rpm-ostree status 2>/dev/null", false);
        }

        nexus::Finding updates;

        updates.check = "Updates";

        if (!status.ok || status.text.empty()) {
            updates.health = nexus::Health::Unknown;
            updates.detail =
                "bootc is present but did not report a status.";

            if (::geteuid() != 0) {
                updates.suggestion = "sudo nexus doctor";
            }
        } else if (status.text.find("localhost/") !=
                   std::string::npos) {
            updates.health = nexus::Health::Warning;
            updates.detail =
                "Installed from a local image, so there is nowhere "
                "to update from.";
            updates.examples.push_back(
                "no fix can reach this machine, however urgent");
            updates.total = 1;
        } else {
            // Which command updates this machine depends on what has
            // been added to it.
            //
            // bootc upgrade refuses a deployment with packages layered
            // on top ("contains local rpm-ostree modifications"), and
            // layering is exactly what `nexus install` and first-boot
            // setup do on an image-based system. So the advice used to
            // be right for a machine nobody had set up and wrong for
            // every machine that had been -- found on a laptop on 23
            // September with six packages layered at first boot.
            // rpm-ostree upgrade pulls the new image and puts the
            // layered packages back on top of it.
            const auto ostree = nexus::system::runCommand(
                "rpm-ostree status 2>/dev/null", false);
            const bool layered =
                ostree.ok &&
                (ostree.text.find("LayeredPackages:") !=
                     std::string::npos ||
                 ostree.text.find("LocalPackages:") !=
                     std::string::npos);

            updates.health = nexus::Health::Ok;

            // `nexus update` is the way in, either way (0.1.63): it
            // runs rpm-ostree upgrade, which keeps layered packages,
            // and it writes the update to `nexus history`. Suggesting
            // `bootc upgrade` sent people around Nexus, so the update
            // never showed in history (seen on the Asus, 2 November).
            //
            // "Signed" is said only where it is checked (0.1.95,
            // round-4 audit L1): this line said "new signed images" on
            // every machine, also one that follows its image without a
            // signature check, while `nexus security` on the same
            // machine warned about exactly that. One answer now
            // (updateSigningOf), and a warning here too.
            std::string origin;
            for (const auto& d : readDeployments()) {
                if (d.booted) {
                    origin = d.image;
                    break;
                }
            }
            const nexus::system::UpdateSigning signing = updateSigningOf(origin);
            if (signing == nexus::system::UpdateSigning::PolicyMissing) {
                updates.health = nexus::Health::Warning;
                updates.detail =
                    "Image-based, but updates are not checked against the "
                    "Nexus key: signed: is set and the policy does not "
                    "require the key.";
                updates.suggestion = "sudo python3 /usr/libexec/nexus/merge-policy";
            } else if (signing == nexus::system::UpdateSigning::NotChecked) {
                updates.health = nexus::Health::Warning;
                updates.detail =
                    "Image-based, but updates are not signature-checked.";
                updates.suggestion = "sudo nexus rebase signed --apply";
            } else {
                const bool checked =
                    signing == nexus::system::UpdateSigning::Enforced ||
                    signing == nexus::system::UpdateSigning::OtherSigned;
                updates.detail =
                    layered ? "Image-based, with packages added on top;"
                              " updating keeps them."
                            : checked
                                ? "Image-based; updates are new signed images."
                                : "Image-based; updates are new images.";
                updates.suggestion = "nexus update   (take it: sudo nexus update --apply)";
            }
        }

        findings.push_back(std::move(updates));
    }

    // A change that started and never finished. The machine may have
    // been left part-way through one, and nothing else will say so.
    //
    // Root's log decides that, whoever asks (0.2.0, ledger I3). Before,
    // this read the caller's own log: without sudo that is a file in
    // the home folder, where a line saying "started" can be written by
    // anyone logged in as that person -- and a change root really left
    // half-done was not looked for at all. What the person's own log
    // says is shown on a line of its own, marked, and does not count
    // towards the verdict.
    {
        const auto interruptedNow = nexus::system::interruptedChanges(
            nexus::system::readAllTransactions());

        if (!interruptedNow.machine.empty()) {
            const auto& unfinished = interruptedNow.machine;
            nexus::Finding interrupted;

            interrupted.check = "Changes";
            interrupted.health = nexus::Health::Warning;
            interrupted.total = unfinished.size();
            interrupted.detail =
                std::to_string(unfinished.size()) +
                " change(s) started and never finished.";

            for (const auto& record : unfinished) {
                if (interrupted.examples.size() >= 5) {
                    break;
                }

                interrupted.examples.push_back(
                    nexus::system::printable(record.when) + "  " +
                    nexus::system::printable(record.request));
            }

            interrupted.suggestion = "nexus history";

            findings.push_back(std::move(interrupted));
        }

        if (!interruptedNow.own.empty()) {
            const auto& unfinished = interruptedNow.own;
            nexus::Finding own;

            own.check = "Your own log";
            // Not a warning: Nexus cannot tell a change you began
            // without sudo from a line some program wrote there.
            own.health = nexus::Health::Unknown;
            own.total = unfinished.size();
            own.detail =
                std::to_string(unfinished.size()) +
                " change(s) started and never finished, going by your own"
                " log (made without sudo, or written by a program you ran)."
                " Not this machine's history.";

            for (const auto& record : unfinished) {
                if (own.examples.size() >= 5) {
                    break;
                }

                own.examples.push_back(
                    nexus::system::printable(record.when) + "  " +
                    nexus::system::printable(record.request));
            }

            own.suggestion = "nexus history";

            findings.push_back(std::move(own));
        }
    }

    // What the package metadata says that Nexus does not model
    // (boolean dependencies and the like) used to be a warning here.
    // It is a note about Nexus, not about the machine, and it made a
    // healthy machine read "Usable, with things worth looking at".
    // `nexus gaps` still lists it.

    nexus::Health worst = nexus::Health::Ok;

    for (const nexus::Finding& finding : findings) {
        if (finding.health == nexus::Health::Problem) {
            worst = nexus::Health::Problem;
        } else if (finding.health == nexus::Health::Warning &&
                   worst == nexus::Health::Ok) {
            worst = nexus::Health::Warning;
        }
    }

    for (const nexus::Finding& finding : findings) {
        std::string mark;

        switch (finding.health) {
            case nexus::Health::Ok:      mark = " ok "; break;
            case nexus::Health::Warning: mark = "warn"; break;
            case nexus::Health::Problem: mark = "FAIL"; break;
            case nexus::Health::Unknown: mark = " ?  "; break;
        }

        std::string name = finding.check;

        while (name.size() < 18) {
            name.push_back(' ');
        }

        std::cout
            << "  [" << mark << "]  " << name
            << finding.detail << "\n";

        for (const std::string& example : finding.examples) {
            std::cout << "              " << example << "\n";
        }

        if (finding.total > finding.examples.size()) {
            std::cout
                << "              ... and "
                << (finding.total - finding.examples.size())
                << " more\n";
        }

        if (!finding.suggestion.empty()) {
            std::cout
                << "              try: " << finding.suggestion << "\n";
        }
    }

    std::cout << "\n";

    switch (worst) {
        case nexus::Health::Ok:
            std::cout << "Nothing wrong that can be seen from here.\n";
            break;
        case nexus::Health::Warning:
            std::cout
                << "Usable, with things worth looking at.\n";
            break;
        case nexus::Health::Problem:
            std::cout
                << "Something is wrong. The findings above say what.\n";
            break;
        case nexus::Health::Unknown:
            std::cout << "Not enough information to judge.\n";
            break;
    }

    std::cout
        << "Nothing has been changed; this command only reports.\n";

    return worst == nexus::Health::Problem ? 1 : 0;
}

}  // namespace nexus_cli
