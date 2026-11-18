// nexus guide.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// How to work this system, by task rather than by command.
//
// The question somebody has is "how do I update this", not "what
// commands exist". Every distribution answers it differently and the
// answer is usually somewhere else -- a wiki, a forum, a video. It
// belongs in the tool.
//
// What Nexus does not do is listed too. A guide that only says what
// works leaves somebody searching for the rest.
int commandGuide(bool useRpm) {
    const std::string manager = useRpm ? "dnf" : "apt";

    // /run/ostree-booted exists on exactly the machines that boot a
    // deployment rather than an ordinary root: bootc, rpm-ostree,
    // Silverblue. Everything else that uses rpm is dnf's to update.
    const bool imageBased =
        std::filesystem::exists("/run/ostree-booted");

    std::cout << R"(Nexus - how to work this system

Nexus decides and explains; )" << manager << R"( does the work.
Every command reads by default. Only --apply changes anything.

This page is nexus help (or nexus guide). Every command is on it;
nexus --help lists them one per line, with their options. Two have
a second name: nexus cybersecurity is nexus cyber, and
nexus secureboot is nexus secure-boot.

WHAT IS ON THIS MACHINE

  nexus                         this machine in a few lines
  nexus doctor                  is anything wrong?
  nexus scan                    what is installed, in summary
  nexus hardware                what this machine is
  nexus specs                   its parts: processor, memory, disks, screen
  nexus services                what starts when it boots
  nexus largest 20              what is taking up the room
  nexus largest 20 --unused     what is big and needed by nothing
  nexus history                 what Nexus has changed

SECURITY AND PRIVACY

  nexus security                audit: firewall, encryption, updates,
                                kernel settings, what starts by itself
  sudo nexus security harden --apply
                                stricter kernel settings (undo: harden off)
  nexus secure-boot             is Secure Boot on, and how to turn it on
  nexus vpn                     is a VPN carrying the traffic; DNS and
                                IPv6 leaks; the kill switch
  nexus vpn keys                WireGuard keys, for your own VPN server

INSTALLING AND REMOVING

  nexus options web-browser     every way to get one, with costs
  nexus install firefox         what it would do, and nothing more
  nexus install firefox --commands
                                the exact commands, to run yourself
  sudo nexus install firefox --apply
  nexus remove firefox          what removing it would take with it
  sudo nexus remove firefox --apply

  nexus source nmap             what building it from source costs

  nexus app install org.videolan.VLC
                                an app from Flathub: what it can reach
                                first (sudo is not needed; add --apply)
  nexus app permissions org.videolan.VLC
                                only what it can reach
  nexus app remove org.videolan.VLC --apply

  nexus language                languages on this machine
  nexus language list           every one that can be added
  sudo nexus language add sr --apply
  sudo nexus language set sr --apply

  --commands works on install, remove and setup. Nexus works out
  what to do and hands you the commands; nothing is hidden and
  nothing is run. The appeal of assembling a system by hand is
  knowing what happened, and that does not require doing the
  resolving by hand as well.

  Nexus checks the plan with )" << manager << R"( before applying it,
  and refuses to apply one )" << manager << R"( will not agree to.

UPDATING
)";

    if (useRpm && imageBased) {
        std::cout << R"(
      nexus update                     what is new for this machine
      sudo nexus update --apply        take it: the new system and app
                                       updates, written to nexus history
      sudo reboot                      the new system starts here

  An image-based system updates by replacing the whole image rather
  than by changing packages one at a time, so the change arrives at
  a reboot and the previous one stays in the boot menu if it does not
  work. nexus update hands the work to rpm-ostree upgrade, which keeps
  what you added on top of the image. It never takes a build older
  than the one running, and what it stages is the build it showed.

      nexus rollback                   go back to the previous system
                                       version (says what you lose first)
      nexus rebase                     switch edition, stay on one build,
                                       or follow the newest again
      nexus update notify off --apply  no daily "update available" note

  Not bootc upgrade: it refuses a machine with packages added on top,
  and it does not write to nexus history. Not dnf upgrade either: /usr
  is part of the image, so dnf cannot change it in place.

  If there is nothing to pull, this machine was installed from an
  image that was never published, and cannot update. That is a
  property of how it was built, not a fault.
)";
    } else if (useRpm) {
        std::cout << R"(
  Nexus does not update this system; dnf does:

      sudo dnf upgrade --refresh       update everything
)";
    } else {
        std::cout << R"(
  Nexus does not update this system; apt does:

      sudo apt update                  refresh what is available
      sudo apt upgrade                 update everything
)";
    }

    std::cout << R"(
  Nexus never updates anything by its own means: the work is always
  the package manager's. A second implementation would only be a way
  to get it wrong.

SETTING UP A MACHINE

  nexus setup                   what this machine could be
  nexus setup gaming            what that would take
  sudo nexus setup gaming --apply

  Several at once: nexus setup school,gaming

  nexus profile list            every profile, and what is in each
  nexus profile show gaming     one profile in full
  nexus profile check gaming    what this machine still lacks for it

GAMES

  nexus gaming                  what this machine does for games
  nexus gamecheck <game>        will it run here, and what it can see
  nexus gamecheck --identifiers what a game could recognise this
                                computer by

SERVERS ON THIS MACHINE

  nexus server                  what runs here, and how to reach it
  sudo nexus server add minecraft --accept-eula --apply
  sudo nexus server add website <folder> --apply
  nexus server logs <name>      what it said last
  sudo nexus server backup <name> --apply
  sudo nexus server remove <name> --apply
  sudo nexus server cockpit --apply
                                manage the machine from a browser

OTHER SYSTEMS ON THIS COMPUTER

  sudo nexus dualboot           the other systems found on its disks
  sudo nexus dualboot add --apply
                                put them in the boot menu
  sudo nexus dualboot 1 --apply restart into one of them, once
  sudo nexus dualboot --disk /dev/sda
                                also look at a USB disk

PROGRAMMING

  nexus dev                     languages on request, in a container
  nexus dev help <topic>        how to work with one of them

SECURITY TESTING (only on systems you own or may test)

  nexus cyber                   Kali's tools in a container
  nexus cyber create small --apply
                                the toolbox (shares your home folder)
  nexus cyber create medium --isolated --apply
                                a toolbox that sees only its own folder
  nexus cyber shell --isolated  work inside it

BUILDING FROM SOURCE

  nexus source <package>        the build dependencies, and what
                                they would cost here

  Nexus prints the commands and does not run them: a build is yours
  to watch, and how long it takes depends on the package, the
  machine and your flags.

  On Debian and Ubuntu this needs source packages enabled, which
  they are not by default. nexus source says so and gives the line
  to fix it.

SOFTWARE FROM ELSEWHERE

  nexus options X --with-flatpak     also offer Flatpaks
  nexus options X --with-snap        also read installed snaps
  nexus container metasploit --apply run another distribution's
                                     package, in a container

  Packages from different distributions cannot share one filesystem.
  They can run beside each other in containers, and Nexus says what
  that costs before you agree to it.

WHEN SOMETHING IS WRONG

  nexus doctor                  start here
  nexus why <component>         what pulled this in
  nexus conflicts               what collides with what
  nexus solve X --explain       why every component was chosen
  nexus services                anything failing or restarting

FOR THE CURIOUS

  nexus what-provides <need>    which packages could satisfy a need
  nexus inspect <component>     everything known about one package
  nexus gaps                    needs nothing here satisfies
  nexus plan <need>             the full plan, before any install
  nexus image <profile>         the package list a fresh machine with
                                that profile would get

WHAT NEXUS DOES NOT DO

  It does not update, and it does not unpack, configure or remove
  files. )" << manager << R"( does those, correctly, and a second
  implementation would be a second set of bugs.

  It does not mix distributions in one filesystem. Nothing can.

  It does not know about services beyond what systemd reports, or
  about anything installed by a script rather than a package. Those
  show up in nexus services with no owner, which is worth looking at.

)";

    return 0;
}

}  // namespace nexus_cli
