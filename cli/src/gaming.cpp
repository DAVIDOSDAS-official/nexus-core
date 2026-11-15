// nexus gaming.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// nexus gaming: what this machine does for games, and the one thing
// Nexus switches on for them.
//
// Honest about size: none of this makes games twice as fast. NTSync
// is the one change with a measured gain -- Windows games under
// Proton/Wine synchronise threads through the kernel instead of a
// helper process, which helps CPU-heavy games most -- and the rest
// is reported so somebody can see what is there and what is not.
// Nothing changes without --apply, and then only one thing: NTSync
// loaded now and at every start.
//
// On the Asus (21 October, kernel 7.2.11-200.fc44) the module was
// there but not loaded; `modprobe ntsync` made /dev/ntsync, mode
// crw-rw-rw-, so games running as the person can use it. Only the
// loading at start was missing.
int commandGaming(
    const std::vector<std::string>& words,
    bool apply,
    const std::string& invocation
) {
    namespace fs = std::filesystem;
    using nexus::system::commandExists;
    using nexus::system::runCommand;
    using nexus::system::GamingFix;

    const std::string what = words.empty() ? "" : words[0];
    const bool known = what.empty() || what == "tune" || what == "doctor" ||
                       what == "ntsync" || what == "split-lock" || what == "off";
    if (!known || words.size() > 1) {
        std::cerr << "Use: nexus gaming                 what this machine does for games\n"
                     "     nexus gaming --apply         switch on what is missing\n"
                     "     nexus gaming ntsync --apply  only NTSync\n"
                     "     nexus gaming split-lock --apply   only the Intel split-lock fix\n"
                     "     nexus gaming off --apply     undo what Nexus switched on\n";
        return 2;
    }

    nexus::system::GamingState st;
    st.ntsyncDevice = fs::exists("/dev/ntsync");
    st.ntsyncModule = st.ntsyncDevice || runCommand("modinfo ntsync > /dev/null 2>&1", false).ok;
    st.ntsyncAtStart = fs::exists(kNtsyncConf) ||
        runCommand("grep -qsx ntsync /etc/modules-load.d/*.conf /usr/lib/modules-load.d/*.conf", false).ok;
    {
        std::ifstream in("/proc/sys/kernel/split_lock_mitigate");
        std::stringstream b; b << in.rdbuf();
        if (in || !b.str().empty()) st.splitLock = nexus::system::parseSysctlInt(b.str());
    }
    st.splitLockConf = fs::exists(nexus::system::kSplitLockConf);

    auto line = [](const char* state, const std::string& name, const std::string& detail) {
        std::cout << "  [" << state << "]  " << std::left << std::setw(13) << name << detail << "\n";
    };

    // ---- off: undo Nexus's own files, nothing else
    if (what == "off") {
        const bool nt = fs::exists(kNtsyncConf), sl = st.splitLockConf;
        if (!nt && !sl) {
            std::cout << "Nexus has switched nothing on for games here; nothing to undo.\n";
            return 0;
        }
        std::cout << "Undo:\n";
        if (nt) std::cout << "  NTSync at every start   (" << kNtsyncConf << ")\n";
        if (sl) std::cout << "  split-lock fix          (" << nexus::system::kSplitLockConf
                          << "; back to the kernel's 1)\n";
        if (!apply) { std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation << " --apply\n"; return 0; }
        if (!nexus::system::haveRootPrivileges()) { std::cout << "\nThis needs root:\n    sudo " << invocation << " --apply\n"; return 1; }
        nexus::system::TransactionRecord record;
        record.when = nexus::system::currentTimestamp();
        record.request = "gaming off";
        record.kind = nexus::system::TransactionKind::Remove;
        record.resolved = "gaming fixes";
        if (nt) record.packages.insert("ntsync");
        if (sl) record.packages.insert("split-lock");
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);
        std::error_code ec;
        bool ok = true;
        if (nt) ok = fs::remove(kNtsyncConf, ec) && ok;
        if (sl) {
            ok = fs::remove(nexus::system::kSplitLockConf, ec) && ok;
            runCommand("sysctl -q -w kernel.split_lock_mitigate=1 2>&1", true);
        }
        record.outcome = ok ? "removed" : "failed";
        record.succeeded = ok; record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);
        if (ok) std::cout << "\nUndone." << (nt ? " NTSync stays loaded until the next restart." : "") << "\n";
        else std::cout << "\nNot everything could be undone.\n";
        return ok ? 0 : 1;
    }

    // ---- the report (also `doctor` and the old `tune`)
    std::cout << "Games on this machine -- what is on, and what it is worth\n\n";

    if (!st.ntsyncModule) line(" -- ", "NTSync", "this kernel has no NTSync.");
    else if (st.ntsyncDevice && st.ntsyncAtStart) line(" ok ", "NTSync", "on, and loaded at every start.");
    else if (st.ntsyncDevice) line("note", "NTSync", "on now, but not loaded at the next start.");
    else line("off ", "NTSync", "in the kernel, not switched on.");

    if (!st.splitLock) {
        line(" -- ", "Split lock", "this CPU or kernel does not slow split locks down; nothing to fix.");
    } else if (*st.splitLock == 0) {
        line(" ok ", "Split lock", std::string("no slowdown") +
             (st.splitLockConf ? ", kept at every start." : " (set outside Nexus)."));
    } else {
        line("off ", "Split lock", "the kernel slows down games that do split locks (some Proton games).");
    }

    if (commandExists("gamemoded"))
        line(" ok ", "GameMode", "installed; helps when other programs compete for the CPU, not raw FPS.");
    else line(" -- ", "GameMode", "not installed (the gaming add-on brings it).");
    line(commandExists("mangohud") ? " ok " : " -- ", "MangoHud",
         commandExists("mangohud") ? "installed: measure every change with it (below)."
                                   : "not installed (the gaming add-on brings it).");
    line(commandExists("gamescope") ? " ok " : " -- ", "Gamescope",
         commandExists("gamescope") ? "installed: frame limit and FSR upscaling, the best levers on a laptop GPU."
                                    : "not installed (the gaming add-on brings it).");

    if (commandExists("powerprofilesctl")) {
        const std::string profile = firstLine(runCommand("powerprofilesctl get 2>/dev/null", false).text);
        if (profile == "power-saver")
            line("note", "Power", "power-saver: games run slower. powerprofilesctl set performance");
        else if (!profile.empty())
            line(" ok ", "Power", profile + (profile == "performance" ? "." : " (performance while playing: powerprofilesctl set performance)."));
    }
    {
        std::ifstream in("/proc/sys/vm/max_map_count");
        long long value = 0;
        if (in >> value)
            line(value >= 1048576 ? " ok " : "note", "Memory maps",
                 std::to_string(value) + (value >= 1048576 ? ": enough for every game." : ": some games need 1048576."));
    }

    std::cout << "\nMeasure, don't trust: in Steam, Properties -> Launch options:\n"
                 "    mangohud %command%                               FPS and frame times\n"
                 "    gamescope -r 60 -F fsr -w 1280 -h 720 -- %command%  cap at 60, upscale\n"
                 "Play the same scene before and after a change; keep what helps.\n"
                 "\nNo tweak doubles frame rates. NTSync helps Windows games under Proton\n"
                 "most (loading, CPU-heavy scenes); the split-lock fix matters only for\n"
                 "games that do split locks, and then a lot.\n";

    const std::string only = (what == "ntsync" || what == "split-lock") ? what : "";
    const auto plan = nexus::system::gamingPlan(st, only);
    if (plan.empty()) {
        std::cout << "\nNothing to switch on.\n";
        return 0;
    }

    std::cout << "\nWhat Nexus will do\n";
    for (auto f : plan) {
        if (f == GamingFix::NtsyncNow) std::cout << "  Load NTSync now (modprobe ntsync)\n";
        if (f == GamingFix::NtsyncAtStart) std::cout << "  Load NTSync at every start: " << kNtsyncConf << "\n";
        if (f == GamingFix::SplitLockOff)
            std::cout << "  Drop the split-lock slowdown, now and at every start: " << nexus::system::kSplitLockConf
                      << "\n    The slowdown is a guard: it stops one program from stalling the whole\n"
                         "    machine with split locks on purpose. Off, any program you run could do\n"
                         "    that. On a one-person desktop that is usually a fair trade for games.\n";
    }
    std::cout << "\nIt will not\n  Change any game, Steam, or how the system updates\n"
                 "Undo any time: sudo nexus gaming off --apply\n";

    if (!apply) {
        std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation << " --apply\n";
        return 0;
    }
    if (!nexus::system::haveRootPrivileges()) {
        std::cout << "\nThis needs root. Re-run with sudo:\n    sudo " << invocation
                  << " --apply\n\nNothing has been changed.\n";
        return 1;
    }

    nexus::system::TransactionRecord record;
    record.when = nexus::system::currentTimestamp();
    record.request = only.empty() ? "gaming" : "gaming " + only;
    record.kind = nexus::system::TransactionKind::Install;
    record.resolved = "gaming fixes";
    const std::string log = nexus::system::defaultTransactionLog();
    const std::string marker = nexus::system::beginTransaction(log, record);

    bool ok = true;
    std::string error;
    for (auto f : plan) {
        if (f == GamingFix::NtsyncAtStart) {
            record.packages.insert("ntsync");
            std::error_code ec;
            fs::create_directories("/etc/modules-load.d", ec);
            std::ofstream out(kNtsyncConf, std::ios::trunc);
            out << "# Written by nexus gaming --apply: Windows games under\n"
                   "# Proton/Wine use it for thread synchronisation.\n"
                   "ntsync\n";
            if (!out) { ok = false; error = std::string("could not write ") + kNtsyncConf; }
        }
        if (f == GamingFix::NtsyncNow) {
            record.packages.insert("ntsync");
            const auto loaded = runCommand("modprobe ntsync 2>&1", true);
            if (!loaded.ok || !fs::exists("/dev/ntsync")) { ok = false; error = "modprobe ntsync: " + firstLine(loaded.text); }
        }
        if (f == GamingFix::SplitLockOff) {
            record.packages.insert("split-lock");
            std::error_code ec;
            fs::create_directories("/etc/sysctl.d", ec);
            std::ofstream out(nexus::system::kSplitLockConf, std::ios::trunc);
            out << "# Written by nexus gaming --apply. Since Linux 6.2 the kernel slows down a\n"
                   "# program that does split locks; some Windows games under Proton do.\n"
                   "# 0 keeps the warning in the log and drops the slowdown.\n"
                   "# Undo: sudo nexus gaming off --apply\n"
                   "kernel.split_lock_mitigate = 0\n";
            if (!out) { ok = false; error = std::string("could not write ") + nexus::system::kSplitLockConf; }
            else {
                const auto set = runCommand("sysctl -q -w kernel.split_lock_mitigate=0 2>&1", true);
                if (!set.ok) { ok = false; error = "sysctl: " + firstLine(set.text); }
            }
        }
    }
    record.outcome = ok ? "switched on" : "failed";
    record.succeeded = ok; record.exitCode = ok ? 0 : 1;
    nexus::system::finishTransaction(log, marker, record);
    if (!ok) { std::cout << "\nNot done: " << error << "\n"; return 1; }

    std::cout << "\nNow:\n";
    // Only what this run changed (the Acer, 9 November: a split-lock-only
    // run printed "[ ok ] NTSync not on yet").
    const bool didNtsync = std::find(plan.begin(), plan.end(), GamingFix::NtsyncNow) != plan.end() ||
                           std::find(plan.begin(), plan.end(), GamingFix::NtsyncAtStart) != plan.end();
    if (didNtsync) {
        const bool on = fs::exists("/dev/ntsync");
        line(on ? " ok " : "note", "NTSync", std::string(on ? "on" : "not on yet") +
             (fs::exists(kNtsyncConf) ? ", and loaded at every start." : "."));
    }
    const bool didSplitLock = std::find(plan.begin(), plan.end(), GamingFix::SplitLockOff) != plan.end();
    if (didSplitLock && st.splitLock) {
        std::ifstream in("/proc/sys/kernel/split_lock_mitigate");
        std::stringstream b; b << in.rdbuf();
        const auto v = nexus::system::parseSysctlInt(b.str());
        line(v && *v == 0 ? " ok " : "note", "Split lock",
             v && *v == 0 ? "no slowdown, kept at every start." : "still on; check: sysctl kernel.split_lock_mitigate");
    }
    std::cout << "\nDone. Nothing needs a restart.\n";
    return 0;
}

}  // namespace nexus_cli
