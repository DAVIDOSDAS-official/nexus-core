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

WHAT IS ON THIS MACHINE

  nexus doctor                  is anything wrong?
  nexus scan                    what is installed, in summary
  nexus hardware                what this machine is
  nexus services                what starts when it boots
  nexus largest 20              what is taking up the room
  nexus largest 20 --unused     what is big and needed by nothing
  nexus history                 what Nexus has changed

INSTALLING AND REMOVING

  nexus options web-browser     every way to get one, with costs
  nexus install firefox         what it would do, and nothing more
  nexus install firefox --commands
                                the exact commands, to run yourself
  sudo nexus install firefox --apply
  nexus remove firefox          what removing it would take with it
  sudo nexus remove firefox --apply

  nexus source nmap             what building it from source costs

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

      sudo rpm-ostree rollback         make the previous one the default

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
