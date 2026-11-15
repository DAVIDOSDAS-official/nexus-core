// nexus update.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// What this machine knows about the build it runs, for holding an
// offered update against (0.1.95, round-4 audit M5). Nothing here is
// asked of the registry. BUILD_ID is read from /usr/lib/os-release,
// which belongs to the image; /etc/os-release is a link to it that a
// person can replace.
nexus::system::RunningBuild runningBuild(const nexus::system::SystemUpdate& system) {
    nexus::system::RunningBuild running;
    running.version = NEXUS_VERSION;
    running.built = system.built;
    running.buildId = osReleaseValueIn("/usr/lib/os-release", "BUILD_ID");
    return running;
}

// A sentence under a row's label: wrapped at 78 columns, each line
// after the first indented to where the row's text starts.
std::string wrappedUnderLabel(const std::string& text) {
    const std::string indent(13, ' ');
    std::string out;
    std::size_t column = indent.size();
    std::istringstream words(text);
    std::string word;
    bool first = true;
    while (words >> word) {
        if (!first && column + 1 + word.size() > 78) {
            out += "\n" + indent;
            column = indent.size();
        } else if (!first) {
            out += " ";
            ++column;
        }
        out += word;
        column += word.size();
        first = false;
    }
    return out;
}

}  // namespace

// nexus update: what is new for this machine, and taking it.
// See system/include/nexus/system/update.hpp for how updates work here.
int commandUpdate(
    const std::vector<std::string>& words,
    bool apply,
    bool assumeYes,
    const std::string& invocation
) {
    bool notify = false;
    bool checkNow = false;
    // For Nexus Shop: the same answer as below, one fact per line,
    // fields split by tabs. Read by a program, so never translated
    // and never reworded without changing the Shop with it.
    bool lines = false;
    // nexus update notify off|on: the daily notification for this
    // account (0.1.86, outside review round 3, P5).
    std::string notifySwitch;
    for (const std::string& word : words) {
        if (word == "notify" || word == "--notify") {
            notify = true;
        } else if (notify && notifySwitch.empty() &&
                   (word == "off" || word == "on" || word == "status")) {
            notifySwitch = word;
        } else if (word == "check" || word == "--check") {
            checkNow = true;
        } else if (word == "--lines") {
            lines = true;
        } else {
            std::cerr << "Unknown: nexus update " << word
                      << "\nUse: nexus update [--apply]"
                         "\n     nexus update notify off|on|status [--apply]\n";
            return 2;
        }
    }

    const char* homeVar = std::getenv("HOME");
    const std::string home = homeVar ? homeVar : "";

    if (!notifySwitch.empty()) {
        const bool off = nexus::system::updateNotifyIsOff(home);
        if (notifySwitch == "status") {
            std::cout << "The daily update notification is "
                      << (off ? "off" : "on") << " for this account.\n"
                      << (off ? "Turn it on:  nexus update notify on --apply\n"
                              : "Turn it off: nexus update notify off --apply\n");
            return 0;
        }
        // It is this account's setting: under sudo it would land in
        // root's home and change nothing for the person.
        if (nexus::system::haveRootPrivileges()) {
            std::cout << "This is a setting of your own account; run it without sudo:\n"
                         "    nexus update notify " << notifySwitch << " --apply\n";
            return 1;
        }
        const bool wantOn = notifySwitch == "on";
        if (off != wantOn) {
            std::cout << "The daily update notification is already "
                      << notifySwitch << " for this account.\n";
            return 0;
        }
        if (wantOn) {
            std::cout << "Turns the daily update check back on for this account: once a\n"
                         "day, ghcr.io (GitHub) and Flathub are asked whether anything is\n"
                         "new, and a notification says so if it is. Nothing is installed.\n";
        } else {
            std::cout << "Turns the daily update check off for this account: no daily\n"
                         "question to ghcr.io (GitHub) or Flathub, and no notification.\n"
                         "Updates still come whenever you ask: nexus update, or the Shop.\n"
                         "Writes " << nexus::system::updateNotifyOffFile(home) << ".\n";
        }
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    "
                      << invocation << " --apply\n";
            return 0;
        }
        nexus::system::TransactionRecord record;
        record.when = nexus::system::currentTimestamp();
        record.request = "update notify " + notifySwitch;
        record.kind = wantOn ? nexus::system::TransactionKind::Remove
                             : nexus::system::TransactionKind::Install;
        record.resolved = nexus::system::updateNotifyOffFile(home);
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);
        std::string error;
        const bool ok = nexus::system::setUpdateNotify(home, wantOn, error);
        record.outcome = ok ? (wantOn ? "removed" : "added") : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        nexus::system::finishTransaction(log, marker, record);
        if (!ok) {
            std::cout << "\nCould not do it: " << error << "\n";
            return 1;
        }
        std::cout << "\nDone: the daily update notification is " << notifySwitch
                  << " for this account.\n";
        return 0;
    }

    // Turned off for this account: the unit does not even start, but a
    // `nexus update notify` run by hand respects it too.
    if (notify && nexus::system::updateNotifyIsOff(home)) {
        return 0;
    }

    const bool image = nexus::system::commandExists("rpm-ostree") &&
                       std::filesystem::exists("/run/ostree-booted");
    const bool haveFlatpak = nexus::system::commandExists("flatpak");
    const bool root = nexus::system::haveRootPrivileges();

    // What the machine runs, then what the registry has now. Comparing
    // the two fingerprints needs no password and downloads nothing but
    // the image's description. ("check" is accepted for habit: every
    // run looks fresh.)
    (void)checkNow;
    nexus::system::SystemUpdate system;
    bool reached = false;
    if (image) {
        system = nexus::system::parseSystemUpdate(
            nexus::system::runCommand("rpm-ostree status 2>/dev/null",
                                      false).text);
        if (!system.origin.empty() &&
            nexus::system::commandExists("skopeo")) {
            const auto remote = nexus::system::parseImageInspect(
                nexus::system::runCommand(
                    "skopeo inspect --no-tags " + shellQuote("docker://" + system.origin) +
                    " 2>/dev/null", false).text);
            reached = remote.ok;
            // Judged before it is called new: an older build than the
            // one running is not an update, whatever its signature
            // (0.1.95, round-4 audit M5).
            nexus::system::compareWithRegistry(system, remote,
                                               runningBuild(system));
            // The weekly build makes a new image even when Nexus itself
            // did not change: same version, newer Fedora packages.
            if (system.available && system.version == NEXUS_VERSION) {
                system.version = std::string(NEXUS_VERSION) +
                    ", rebuilt with Fedora's latest updates";
            }
        }
    }

    std::vector<nexus::system::AppUpdate> apps;
    if (haveFlatpak) {
        apps = nexus::system::parseFlatpakUpdates(
            nexus::system::runCommand(
                "flatpak remote-ls --updates --app "
                "--columns=application,version 2>/dev/null",
                false).text);
    }

    const std::string summary = nexus::system::updateSummary(system, apps);

    if (lines) {
        if (image) {
            for (const auto& d : readDeployments()) {
                if (d.booted) {
                    std::cout << "running\t" << NEXUS_VERSION << "\t"
                              << (d.date.empty() ? "" : dayOf(d.date))
                              << "\n";
                }
            }
            if (system.staged) {
                std::cout << "staged\t" << system.stagedVersion << "\n";
            }
            if (system.refused) {
                // A state of its own (0.1.95): not "new", so the Shop
                // neither counts nor offers it, and not "current",
                // which would hide that something was turned away. The
                // reason goes where the Shop shows a line of text.
                std::cout << "system\trefused\t" << system.refusedVersion
                          << "\t"
                          << (system.refusedDate.empty()
                                  ? "" : dayOf(system.refusedDate))
                          << "\t" << system.refusedWhy << "\n";
            } else {
                std::cout << "system\t"
                          << (system.available ? "new"
                              : reached ? "current" : "unreachable")
                          << "\t" << system.version << "\t"
                          << (system.date.empty() ? "" : dayOf(system.date))
                          << "\t" << system.diff << "\n";
            }
        } else {
            std::cout << "system\tnot-image\t\t\t\n";
        }
        for (const auto& app : apps) {
            std::cout << "app\t" << app.id << "\t" << app.version << "\n";
        }
        return 0;
    }

    // From the daily timer, in the person's session: a notification
    // when something new appears, once per new thing -- not every day
    // for the same version. Silent otherwise.
    if (notify) {
        if (summary.empty() ||
            !nexus::system::commandExists("notify-send")) {
            return 0;
        }
        // No home, nowhere of its own to remember what was already
        // said -- and so nothing is said. (Before 0.1.87 the note went
        // to /tmp/.cache/nexus instead: a fixed name in a folder every
        // user can write to, where a link put there first would have
        // been written through.)
        if (home.empty() || home[0] != '/') {
            return 0;
        }
        const std::string seenFile = home + "/.cache/nexus/update-notified";
        std::string seen;
        // The first notification on this account says how to stop them.
        const bool first = !std::filesystem::exists(seenFile);
        {
            std::ifstream in(seenFile);
            std::getline(in, seen);
        }
        if (seen == summary) {
            return 0;
        }
        // A home that cannot be written to is not a reason to fail the
        // daily check: the notification is shown, just not remembered.
        std::error_code seenError;
        std::filesystem::create_directories(
            std::filesystem::path(seenFile).parent_path(), seenError);
        std::ofstream(seenFile) << summary << "\n";

        auto quoted = [](const std::string& text) {
            std::string out = "'";
            for (char c : text) {
                out += c == '\'' ? std::string("'\\''") : std::string(1, c);
            }
            return out + "'";
        };
        // -t 0: it stays until it is clicked away. A notification
        // that vanishes while nobody looks is one nobody saw (Asus,
        // 4 October). `nexus update` says the same any time.
        //
        // With the Shop installed it carries a button that opens it at
        // Updates. notify-send waits for the click (or the close) and
        // prints the button's name; LXQt shows the button (Asus, 10
        // October). The Shop is started in a unit of its own: started
        // from here it would belong to this service and be stopped with
        // it. The session's display is known to the user manager
        // (WAYLAND_DISPLAY, checked the same day).
        const bool shop = nexus::system::commandExists("nexus-shop");
        const auto answer = nexus::system::runCommand(
            "notify-send -a Nexus -t 0 -i system-software-update " +
            std::string(shop ? "-A open='Open Nexus Shop' " : "") +
            quoted("Update available") + " " +
            quoted(summary + ". Nothing is installed until you ask: " +
                   (shop ? std::string("open Nexus Shop, Updates")
                         : std::string("open a terminal and run  "
                                       "nexus update")) +
                   (first ? std::string(". Not wanted? In a terminal:  "
                                        "nexus update notify off --apply")
                          : std::string())),
            false);
        if (shop && answer.text.find("open") != std::string::npos) {
            nexus::system::runCommand(
                "systemd-run --user --quiet --collect --no-block "
                "nexus-shop --updates >/dev/null 2>&1", false);
        }
        return 0;
    }

    // The account.
    if (image) {
        const auto deployments = readDeployments();
        for (const auto& d : deployments) {
            if (d.booted) {
                // The tool's own version is the image's: both are built
                // together. d.version is Fedora's base number.
                std::cout << "Running      Nexus " << NEXUS_VERSION
                          << (d.date.empty() ? ""
                                  : "  (built " + dayOf(d.date) + ")")
                          << "\n";
            }
        }
        if (system.staged) {
            std::cout << "Ready        " << system.stagedVersion
                      << " is downloaded and waits for a restart\n";
        }
        if (system.available) {
            const bool numbered = !system.version.empty() &&
                std::isdigit(static_cast<unsigned char>(system.version[0]));
            std::cout << "New          "
                      << (numbered ? "Nexus " + system.version
                                   : std::string("a newer Nexus image"))
                      << (system.date.empty() ? ""
                              : "  (built " + dayOf(system.date) + ")")
                      << "\n";
            if (!system.diff.empty()) {
                std::cout << "             " << system.diff << "\n";
            }
            // Which build, exactly: the version and date above are
            // read from the registry's newest build at this moment,
            // and --apply holds what rpm-ostree stages against this
            // digest (0.1.95, round-4 audit L9).
            if (nexus::system::validDigest(system.offeredDigest)) {
                std::cout << "             build "
                          << shortDigest(system.offeredDigest)
                          << ": --apply installs exactly this one, or nothing\n";
            }
        } else if (system.refused) {
            std::cout << "Not taken    "
                      << wrappedUnderLabel(system.refusedWhy) << "\n"
                      << "             An update never goes backwards: an old build's signature stays\n"
                         "             good, so a mirror that is behind, or someone answering in the\n"
                         "             registry's place, could offer one. Nothing was downloaded.\n"
                         "             Going back on purpose is yours to do: nexus rollback, or the\n"
                         "             boot menu.\n";
        } else if (!system.staged) {
            std::cout << (reached
                ? "System       up to date (checked just now)\n"
                : "System       could not reach the registry to compare "
                  "(offline?)\n");
        }
    }

    if (haveFlatpak) {
        if (apps.empty()) {
            std::cout << "Apps         up to date\n";
        } else {
            std::cout << "Apps         " << apps.size() << " to update\n";
            for (const auto& app : apps) {
                std::cout << "                 " << app.id
                          << (app.version.empty() ? "" : "  " + app.version)
                          << "\n";
            }
        }
    }

    const bool anything = system.available || !apps.empty();

    if (!apply) {
        if (anything) {
            std::cout << "\nNothing has been changed. To update:\n"
                      << "    sudo nexus update --apply\n";
        } else if (system.staged) {
            std::cout << "\nRestart to use it: systemctl reboot\n";
        }
        return 0;
    }

    if (!anything) {
        std::cout << "\nNothing to update.\n";
        return 0;
    }

    if (!root) {
        std::cout << "\nUpdating needs root. Re-run with sudo:\n    sudo "
                  << invocation << "\n";
        return 1;
    }

    if (image && system.available) {
        std::cout << "\nThe system update is staged: it takes effect at the "
                     "next restart,\nand the version running now stays "
                     "in the boot menu to go back to.\n";
    }

    if (!assumeYes) {
        std::cout << "\nProceed? [Y/n] ";
        std::string answer;
        std::getline(std::cin, answer);
        if (!(answer.empty() || answer == "y" || answer == "Y" ||
              answer == "yes")) {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    // One system change at a time. A second `nexus update` (the Shop's
    // and a terminal's, Asus 14 October) waited silently behind the
    // first; say what is happening instead.
    if (image && system.available) {
        const auto state = nexus::system::runCommand(
            "rpm-ostree status 2>/dev/null | head -2", false);
        if (state.text.find("State: busy") != std::string::npos) {
            std::cout << "\nAn update is already running (from Nexus Shop, "
                         "or another terminal).\nLet it finish; "
                         "`rpm-ostree status` shows when it is idle again.\n"
                         "Nothing has been changed.\n";
            return 1;
        }
    }

    // The build that was shown and confirmed, if the registry named
    // one (0.1.95, round-4 audit L9). The record carries it: "Nexus
    // 0.1.96" alone does not say which of that version's weekly builds.
    const std::string confirmed =
        image && system.available &&
        nexus::system::validDigest(system.offeredDigest)
            ? system.offeredDigest : std::string();

    nexus::system::TransactionRecord record;
    record.when = nexus::system::currentTimestamp();
    record.request = "update";
    record.resolved = summary;
    if (!confirmed.empty()) {
        record.resolved += "; system build " + shortDigest(confirmed);
    }
    const std::string log = nexus::system::defaultTransactionLog();
    const std::string marker = nexus::system::beginTransaction(log, record);

    bool ok = true;
    // Why the system part did not end as confirmed, for the record and
    // the last lines; empty when it did. leftStaged: something this run
    // did not confirm still waits for a restart.
    std::string systemNote;
    bool leftStaged = false;

    if (image && system.available) {
        std::cout << "\nSystem: handing over to rpm-ostree.\n";
        // Not captured: rpm-ostree prints what it downloads as it goes,
        // and a new system plus the packages added on top of it can be
        // a gigabyte. Captured, nothing showed until it was over -- two
        // hours of "Updating..." and an empty terminal on the Asus
        // (14 October), which looked like a hang.
        std::cout << "Downloading the new version; this can take a while on "
                     "a slow\nconnection. Its progress is below.\n\n"
                  << std::flush;
        const int status = std::system(
            nexus::system::inPlainLocale("rpm-ostree upgrade").c_str());
        std::cout << "\n";
        ok = ok && status == 0;

        // What rpm-ostree staged, held against what was shown and
        // confirmed (0.1.95, round-4 audit L9 and M5). The upgrade
        // follows the tag, and the tag can point somewhere else by the
        // time the download starts -- a new weekly build released in
        // that minute, or someone answering in the registry's place.
        // rpm-ostree cannot be told "this digest, and keep following
        // the tag", so the check is made afterwards, on its own status:
        // the deployment that would start next must be the confirmed
        // digest and must not be older than the running build. If it
        // is anything else it is removed again; it has not started and
        // never will.
        if (status == 0) {
            const auto after = nexus::system::parseSystemUpdate(
                nexus::system::runCommand("rpm-ostree status 2>/dev/null",
                                          false).text);
            // Removes what this run staged; true only when rpm-ostree's
            // status, read again, no longer lists it.
            auto discard = [](const std::string& digest) {
                std::cout << std::flush;
                if (std::system(nexus::system::inPlainLocale(
                        "rpm-ostree cleanup --pending").c_str()) != 0) {
                    // Its own message is above; what counts is what
                    // the status says next.
                    std::cout << "(rpm-ostree cleanup reported a problem.)\n";
                }
                const auto now = nexus::system::parseSystemUpdate(
                    nexus::system::runCommand(
                        "rpm-ostree status 2>/dev/null", false).text);
                return !now.digest.empty() && now.pendingDigest != digest;
            };
            auto removeUnconfirmed = [&](const std::string& what) {
                std::cout << what
                          << "\nWhat was not confirmed is not kept: removing it "
                             "(rpm-ostree cleanup --pending).\n\n";
                if (discard(after.pendingDigest)) {
                    std::cout << "\nRemoved. See what is offered now: nexus update\n";
                    return true;
                }
                std::cout << "\nIt could NOT be removed, and would start at the next "
                             "restart.\nBefore restarting, remove it yourself:\n"
                             "    sudo rpm-ostree cleanup --pending\n";
                return false;
            };

            if (after.digest.empty()) {
                // No status, no check: say so rather than "Done".
                ok = false;
                leftStaged = true;
                systemNote = "staged, but rpm-ostree's status could not be "
                             "read back to check it";
                std::cout << "rpm-ostree finished, but its status could not be read "
                             "back, so what it staged\nwas not checked against what "
                             "was shown. Look before restarting:\n"
                             "    rpm-ostree status\n";
            } else {
                switch (nexus::system::judgeStaged(confirmed, system, after)) {
                    case nexus::system::StagedResult::AsConfirmed:
                        if (!confirmed.empty()) {
                            std::cout << "Staged: build " << shortDigest(confirmed)
                                      << ", the one shown above.\n";
                        } else {
                            // The registry named no digest to confirm
                            // (skopeo missing): say which build it is.
                            std::cout << "Staged: build "
                                      << shortDigest(after.pendingDigest) << ".\n";
                        }
                        break;
                    case nexus::system::StagedResult::NothingNew:
                        ok = false;
                        systemNote = "nothing was staged";
                        std::cout << "rpm-ostree staged nothing new: the registry no "
                                     "longer offers the build shown\nabove. See what "
                                     "is offered now: nexus update\n";
                        break;
                    case nexus::system::StagedResult::Different:
                        ok = false;
                        systemNote = removeUnconfirmed(
                            "rpm-ostree staged another build than the one shown "
                            "above:\n    shown and confirmed   " + confirmed +
                            "\n    staged                " + after.pendingDigest +
                            "\nThe registry's answer changed between the check and "
                            "the download.")
                            ? "rpm-ostree staged another build than the one "
                              "confirmed (" + shortDigest(after.pendingDigest) +
                              "); it was removed"
                            : "another build than the one confirmed (" +
                              shortDigest(after.pendingDigest) + ") is still staged";
                        leftStaged = systemNote.find("still staged") != std::string::npos;
                        break;
                    case nexus::system::StagedResult::Older:
                        ok = false;
                        systemNote = removeUnconfirmed(
                            "rpm-ostree staged a build made " +
                            dayOf(after.pendingBuilt) + ", before the one running "
                            "here (" + dayOf(after.built) + ").\nAn update never "
                            "goes backwards.")
                            ? "rpm-ostree staged an older build (" +
                              shortDigest(after.pendingDigest) + "); it was removed"
                            : "an older build (" + shortDigest(after.pendingDigest) +
                              ") is still staged";
                        leftStaged = systemNote.find("still staged") != std::string::npos;
                        break;
                }
            }
        }
    }

    if (!apps.empty()) {
        std::cout << "\nApps: handing over to flatpak.\n\n";
        std::cout << std::flush;
        // Not captured either, for the same reason as the system.
        const int status = std::system(nexus::system::inPlainLocale(
            "flatpak update -y --noninteractive").c_str());
        std::cout << "\n";
        ok = ok && status == 0;
    }

    record.outcome = ok ? (image && system.available ? "staged" : "applied")
                        : "failed";
    // The record says what really happened (0.1.95).
    if (!systemNote.empty()) {
        record.outcome += ": " + systemNote;
    }
    record.succeeded = ok;
    record.exitCode = ok ? 0 : 1;
    nexus::system::finishTransaction(log, marker, record);

    if (!ok) {
        if (!systemNote.empty()) {
            std::cout << "\nThe system update did not go through as confirmed:\n    "
                      << systemNote << ".\n"
                      << (leftStaged
                              ? "Look before restarting: rpm-ostree status\n"
                              : "The running system is unchanged, and nothing "
                                "waits for a restart that was not\nthere before.\n")
                      << "This is in the history (nexus history).\n";
            return 1;
        }
        std::cout << "\nSomething did not complete; the messages above are "
                     "the tools' own.\nThe running system is unchanged.\n";
        return 1;
    }
    if (image && system.available) {
        std::cout << "\nDone. Restart when it suits you: systemctl reboot\n";
    } else {
        std::cout << "\nDone.\n";
    }
    return 0;
}

}  // namespace nexus_cli
