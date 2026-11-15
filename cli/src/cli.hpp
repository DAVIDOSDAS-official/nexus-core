#pragma once

// What the files of the `nexus` tool share (0.1.96).
//
// cli/src/main.cpp had grown past 11,000 lines, and every audit named it
// as the hardest part to review. It is now one file per command area;
// this header declares what more than one of them uses, and nothing
// else. It is not installed and not part of any library: only the files
// beside it include it.
//
// Everything is in namespace nexus_cli, a namespace of its own rather
// than nexus::cli on purpose: the code was written in an unnamed
// namespace at the top level, where a plain name is looked up among its
// own and then the global ones. Inside nexus:: every plain name would
// be looked up in nexus first -- `system`, `find`, `Version` -- and the
// move would have changed what some of them mean. What only one file
// uses stays in an unnamed namespace in that file.

#include <nexus_version.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <istream>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>
#include <sys/statvfs.h>
#include <ctime>
#include <cctype>
#include <cmath>
#include <sstream>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>
#include <nexus/profile_check.hpp>
#include <nexus/alias.hpp>
#include <nexus/composition.hpp>
#include <nexus/diagnosis.hpp>
#include <nexus/options.hpp>
#include <nexus/system/container.hpp>
#include <nexus/system/process.hpp>
#include <nexus/system/services.hpp>
#include <nexus/system/nix_source.hpp>
#include <nexus/system/snap_source.hpp>
#include <nexus/system/portage_source.hpp>
#include <nexus/system/vpn.hpp>
#include <nexus/system/vpn_check.hpp>
#include <nexus/system/cyber.hpp>
#include <nexus/system/dev.hpp>
#include <nexus/system/rollback.hpp>
#include <nexus/system/gaming.hpp>
#include <nexus/system/security_audit.hpp>
#include <nexus/system/pins.hpp>
#include <nexus/system/site_copy.hpp>

#include <pwd.h>
#include <sys/stat.h>
#include <grp.h>
#include <cstring>

using nexus::system::shellQuote;   // process.hpp (0.1.76)
using nexus::system::pinnedImage;  // pins.hpp (0.1.81)
#include <nexus/system/source_build.hpp>
#include <nexus/system/alias_file.hpp>
#include <nexus/removal.hpp>
#include <nexus/transaction.hpp>
#include <nexus/system/auto_installed.hpp>
#include <nexus/solver.hpp>
#include <nexus/hardware/encryption.hpp>
#include <nexus/hardware/secure_boot.hpp>
#include <nexus/hardware/hardware.hpp>
#include <nexus/system/apt_source.hpp>
#include <nexus/system/flatpak_source.hpp>
#include <nexus/system/pacman_source.hpp>
#include <nexus/system/rpm_database.hpp>
#include <nexus/system/plan_apply.hpp>
#include <nexus/system/transaction_log.hpp>
#include <nexus/system/plan_check.hpp>
#include <nexus/system/protection.hpp>
#include <nexus/system/rpm_version.hpp>
#include <nexus/system/rpm_repo.hpp>
#include <nexus/system/profile_file.hpp>
#include <nexus/system/dpkg_source.hpp>
#include <nexus/system/version.hpp>
#include <nexus/system/gamecheck.hpp>
#include <nexus/system/specs.hpp>
#include <nexus/system/clock.hpp>
#include <nexus/system/suggest.hpp>
#include <nexus/system/language.hpp>
#include <nexus/system/update.hpp>
#include <nexus/system/catalogue.hpp>
#include <nexus/system/json.hpp>
#include <nexus/system/dualboot.hpp>
#include <nexus/system/server.hpp>
#include <chrono>

namespace nexus_cli {

using nexus::Capability;
using nexus::Component;
using nexus::system::DpkgSourceResult;
using nexus::system::ModelGap;

// ---------------------------------------------------------------------
// Types and constants more than one file uses.

// What a package source reported, without saying which one it was.
//
// Nexus reasons about capabilities, so the parts of scan that are
// interesting -- how much of the metadata the model holds, what it
// could not represent -- are the same question whichever ecosystem
// answered it.
struct SourceSummary {
    std::string name;
    std::vector<Component> components;

    std::size_t recordsRead = 0;
    std::size_t recordsSkipped = 0;
    std::size_t dependencyClauses = 0;
    std::size_t representableClauses = 0;

    std::map<std::string, std::size_t> gaps;
    std::vector<std::string> notes;

    // Why this read may not be the whole database; empty when it is.
    // See RpmDatabaseResult::incomplete.
    std::string incomplete;
};

// ---------------------------------------------------------------------
// What the machine is running, as rpm-ostree reports it.
//
// Read from `rpm-ostree status` (text, no root needed) rather than the
// JSON form, which is not the same in every version. Used by `nexus`
// with no arguments and by `nexus history`.
struct Deployment {
    bool booted = false;
    std::string image;      // what it was installed from
    std::string version;    // e.g. 44.20260930.0
    std::string date;       // e.g. 2026-09-30T17:36:58Z
    std::string digest;     // sha256:... of the image
    std::vector<std::string> layered;
};

const char* const kNtsyncConf = "/etc/modules-load.d/nexus-ntsync.conf";

// ---------------------------------------------------------------------
// Set once in main(), read by the commands.
extern bool gUseRpmVersions;   // common.cpp
extern bool gAvailableLoaded;   // install.cpp

// ---------------------------------------------------------------------
// common.cpp
bool closeSpelling(const std::string& a, const std::string& b);
const Component* find(
    const std::vector<Component>& components,
    const std::string& id
);
nexus::VersionComparator versionComparator();
nexus::ConflictDetector buildDetector();
nexus::Solver buildSolver(const std::vector<Component>& components);
std::vector<Deployment> readDeployments();
std::string osReleaseValueIn(const std::string& file, const std::string& name);
std::string osReleaseValue(const std::string& name);
nexus::system::UpdateSigning updateSigningOf(const std::string& origin);
std::string signingWords(nexus::system::UpdateSigning signing);
std::string describeImage(const std::string& image);
std::string dayOf(const std::string& iso);
std::string readWholeFile(const std::string& path);
bool confirmed(bool assumeYes);
std::string firstLine(const std::string& text);
std::string shortDigest(const std::string& d);

// ---------------------------------------------------------------------
// inspect.cpp
int commandScan(const SourceSummary& result);
int commandWhatProvides(
    const std::vector<Component>& components,
    const std::string& capability
);
int commandWhy(
    const std::vector<Component>& components,
    const std::string& id
);
int commandInspect(
    const std::vector<Component>& components,
    const std::string& id
);
int commandConflicts(const std::vector<Component>& components);
int commandLargest(
    const std::vector<Component>& installed,
    std::size_t howMany,
    bool onlyUnused,
    const std::set<std::string>& unused
);
int commandServices(bool showAll, bool useRpm);
int commandSecureBoot(const std::string& root);
int commandHardware(const nexus::hardware::HardwareInfo& info);
int commandGaps(const SourceSummary& result);

// ---------------------------------------------------------------------
// profile.cpp
int commandSolve(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::string& capability,
    const std::string& prefer,
    const std::string& require,
    const std::string& architecture,
    bool explain
);
bool findComposition(
    const std::vector<nexus::Profile>& available,
    const std::string& name,
    nexus::Composition& composition
);
void reportComposition(const nexus::Composition& composition);
int commandImage(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::string& profileName,
    const std::string& directory,
    const std::string& architecture
);
int commandProfile(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& installed,
    const std::vector<Component>& universe,
    bool haveAvailable,
    const std::string& action,
    const std::string& name,
    const std::string& directory,
    const std::string& architecture
);

// ---------------------------------------------------------------------
// install.cpp
int commandInstall(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture,
    bool useRpm,
    bool apply,
    bool assumeYes,
    bool showCommands,
    const std::string& invocation,
    const std::string& profileDirectory,
    bool explain
);
int commandOptions(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture,
    const std::optional<nexus::Source>& only
);
int commandPlan(
    const std::vector<Component>& universe,
    const std::string& capability,
    const std::string& architecture,
    bool explain
);

// ---------------------------------------------------------------------
// remove.cpp
int commandRemove(
    const std::vector<Component>& installed,
    const std::string& target,
    const std::string& statesPath,
    const std::string& architecture,
    const std::set<std::string>& protectedIds,
    bool apply,
    bool assumeYes,
    bool useRpm,
    bool showCommands,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// source.cpp
int commandSource(
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& package,
    const std::string& architecture,
    bool showCommands
);

// ---------------------------------------------------------------------
// container.cpp
int commandContainerInstall(
    const std::vector<Component>& universe,
    const std::string& requested,
    const std::string& distribution,
    bool apply,
    bool assumeYes
);

// ---------------------------------------------------------------------
// setup.cpp
std::string setupChoice(const std::vector<std::string>& positional);
std::set<std::string> installedFlatpakApps();
void countInstalledFlatpaks(const nexus::Profile& profile,
                            nexus::ProfileReport& report,
                            const std::set<std::string>& apps);
int commandSetup(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& installed,
    const std::vector<Component>& universe,
    const nexus::hardware::HardwareInfo& hardware,
    const std::string& directory,
    const std::string& architecture,
    const std::string& chosenAsTyped,
    bool apply,
    bool assumeYes,
    bool showCommands,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// app.cpp
int commandApp(
    const std::vector<std::string>& words,
    bool apply
);

// ---------------------------------------------------------------------
// language.cpp
int commandLanguage(
    const std::vector<std::string>& words,
    bool apply,
    bool assumeYes,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// summary.cpp
int commandSummary();

// ---------------------------------------------------------------------
// doctor.cpp
std::optional<nexus::Finding> checkRemoteLogin();
int commandDoctor(
    const SourceSummary& source,
    const std::vector<Component>& installed,
    const nexus::hardware::HardwareInfo& hardware,
    const nexus::hardware::EncryptionReport& encryption,
    const std::set<std::string>& roots,
    bool havePackages,
    const std::set<std::string>& protectedIds
);

// ---------------------------------------------------------------------
// history.cpp
int commandHistory(const std::string& path);

// ---------------------------------------------------------------------
// update.cpp
int commandUpdate(
    const std::vector<std::string>& words,
    bool apply,
    bool assumeYes,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// rollback.cpp
int commandRollback(bool apply, const std::string& invocation);
int commandRebase(const std::vector<std::string>& raw, bool apply, const std::string& invocation);

// ---------------------------------------------------------------------
// security.cpp
int commandSecurity(const std::vector<std::string>& words, bool apply, const std::string& invocation);

// ---------------------------------------------------------------------
// server.cpp
int commandServer(
    const std::vector<std::string>& raw,
    bool apply,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// dualboot.cpp
int commandDualboot(

    const std::vector<std::string>& words,
    bool apply,
    bool assumeYes,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// gaming.cpp
int commandGaming(
    const std::vector<std::string>& words,
    bool apply,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// gamecheck.cpp
int commandGamecheck(const std::vector<std::string>& words);

// ---------------------------------------------------------------------
// cyber.cpp
int commandCyber(
    const std::vector<std::string>& raw,
    bool apply,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// dev.cpp
int commandDev(
    const std::vector<std::string>& raw,
    bool apply,
    const std::string& invocation
);

// ---------------------------------------------------------------------
// vpn.cpp
int commandVpn(
    const std::string& action,
    const nexus::system::TunnelSettings& settings,
    const std::string& path,
    bool overwrite,
    bool apply
);

// ---------------------------------------------------------------------
// specs.cpp
int commandSpecs(const std::string& sysfsRoot, bool plain, bool json);

// ---------------------------------------------------------------------
// guide.cpp
int commandGuide(bool useRpm);

}  // namespace nexus_cli
