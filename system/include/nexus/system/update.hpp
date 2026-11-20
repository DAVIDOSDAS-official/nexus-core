#pragma once

// nexus update: is there something new, and taking it.
//
// How updates work on Nexus (decided 4 October): the machine checks by
// itself once a day and only says so -- a notification -- and nothing
// is downloaded or changed until the person asks (`sudo nexus update
// --apply`, later a button in the Shop). It never restarts by itself.
//
// The check compares fingerprints: the Digest of the image this
// machine runs (`rpm-ostree status`) against the Digest the registry
// has now (`skopeo inspect`, no password, no download). rpm-ostree's
// own --check was tried first and is not used: on an image-based
// machine it looks at added packages, and it said "No updates
// available" with a new Nexus image on the registry (Asus, 4 October).
// bootc's timer is off: it applies and restarts by itself, and with
// any package added on the machine it fails quietly ("cannot upgrade
// via bootc", Asus, 4 October).
//
// Pure functions over command output, tested without the commands.

#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

struct SystemUpdate {
    std::string origin;        // ghcr.io/x/nexus-core:minimal
    std::string digest;        // what the booted deployment runs
    std::string stagedDigest;
    bool available = false;    // the registry has something newer
    std::string version;       // 44.20261005.0
    std::string date;          // 2026-10-05T...
    std::string diff;          // rpm-ostree's one-line summary, if any
    bool staged = false;       // a new deployment waits for a restart
    std::string stagedVersion;
    std::string lastCheck;     // "rpm-ostreed-automatic.timer: last run 4h ago"
    std::string policy;        // "check", "stage", "disabled", ...

    // 0.1.95 (round-4 audit M5, L9).
    std::string built;         // when the booted build was made: 2026-10-04T10:00:00Z
    // The deployment that starts at the next restart when that is not
    // the running one: rpm-ostree lists it above the booted one. Read
    // from the order, not from a "Staged:" line, which not every
    // rpm-ostree prints without -v.
    std::string pendingDigest;
    std::string pendingBuilt;
    // What the registry offered, whatever was decided about it.
    std::string offeredDigest;
    // The registry offered a build that is not taken (older than the
    // one running, or not saying when it was built): never "available",
    // and why is in refusedWhy, in plain words.
    bool refused = false;
    std::string refusedWhy;
    std::string refusedVersion;   // what it called itself, if it said
    std::string refusedDate;
};

// Reads `rpm-ostree status` text.
SystemUpdate parseSystemUpdate(const std::string& statusText);

// "ostree-image-signed:docker://ghcr.io/x/y:tag" -> "ghcr.io/x/y:tag".
// Empty when the origin is not a container image.
std::string imageReference(const std::string& origin);

struct RemoteImage {
    bool ok = false;
    std::string digest;
    std::string created;       // 2026-10-04T14:18:53.528859441Z
    std::string nexusVersion;  // the nexus.version label, if there
};

// Reads `skopeo inspect --no-tags docker://...` (JSON).
RemoteImage parseImageInspect(const std::string& json);

// Fills in available / version / date from what the registry has:
// newer when its digest is neither the running one nor the staged one.
void compareWithRegistry(SystemUpdate& system, const RemoteImage& remote);

// ---------------------------------------------------------------------
// An update never goes backwards (0.1.95, round-4 audit M5).
//
// Every image signed with the Nexus key verifies, however old. Whoever
// answers this machine's registry requests -- or a mirror that is
// behind -- could offer last month's build, with last month's holes, as
// "the update", and its signature would be good. So an offer is held
// against the build that is running, by two values the signature
// covers (both are in the image's configuration, whose digest is in
// the signed manifest):
//   - the nexus.version label: lower than the running version is older;
//   - Created, when the image was built: earlier than the running
//     build is older.
// What the machine knows about itself needs no registry: its version
// is the tool's own, and when it was built is rpm-ostree's timestamp
// of the booted deployment (the same value rpm-ostree's own
// "chronologically older" check goes by), with BUILD_ID from
// os-release, written by image/identity.sh, beside it.
//
// Going back on purpose is not an update and is not judged here:
// `nexus rollback`, `nexus rebase sha256:...`, `rpm-ostree rollback`
// and the boot menu work as before.
struct RunningBuild {
    std::string version;   // 0.1.95 -- the tool's, which is the image's
    std::string built;     // 2026-11-10T03:41:07Z, from rpm-ostree status
    std::string buildId;   // 20261110, BUILD_ID in /usr/lib/os-release
};

enum class OfferOrder {
    NotOlder,       // may be taken
    OlderVersion,   // a lower Nexus version than the one running
    OlderBuild,     // built before the build that is running
    Undated,        // does not say when it was built: cannot be shown newer
};

struct OfferVerdict {
    OfferOrder order = OfferOrder::NotOlder;
    std::string reason;    // one plain sentence; empty for NotOlder
    bool refused() const { return order != OfferOrder::NotOlder; }
};

// Seconds since 1970 for "2026-10-04T14:18:53.528859441Z" (skopeo),
// "2026-10-04T13:19:59Z" (rpm-ostree), with or without a +hh:mm
// offset. Nothing for anything that is not such a time.
std::optional<long long> parseBuildTime(const std::string& iso);

// The start of the day BUILD_ID names ("20261110"), in the same
// seconds. Nothing unless it is eight digits naming a real day.
std::optional<long long> parseBuildId(const std::string& buildId);

OfferVerdict judgeOffer(const RunningBuild& running, const RemoteImage& offered);

// compareWithRegistry, with the offer judged first: an older one is
// never "available" (so it is neither announced nor installed) and
// system.refused says why.
void compareWithRegistry(SystemUpdate& system, const RemoteImage& remote,
                         const RunningBuild& running);

// ---------------------------------------------------------------------
// What was shown is what is installed (0.1.95, round-4 audit L9).
//
// `nexus update` shows the registry's newest build -- version, date,
// digest -- and `rpm-ostree upgrade` then pulls whatever the tag points
// at by that moment. rpm-ostree has no way to be told "this digest, and
// keep following the tag", so the result is held against what was
// shown instead: after the upgrade, the deployment that would start
// next must be the digest that was confirmed, and must not be older
// than the running one by rpm-ostree's own, signature-checked account.
// Anything else is removed again before it can ever start.
enum class StagedResult {
    AsConfirmed,   // the build that was shown waits for a restart
    NothingNew,    // the upgrade left nothing it had not found there
    Different,     // another build than the one shown was staged
    Older,         // what was staged was built before the running build
};

// before/after: `rpm-ostree status` read before and after the upgrade.
// confirmed: the digest that was shown; empty when none could be read
// (then only "not older" can be checked).
StagedResult judgeStaged(const std::string& confirmed,
                         const SystemUpdate& before,
                         const SystemUpdate& after);

// ---------------------------------------------------------------------
// Is this machine's update signature-checked? One answer, for `nexus`,
// `nexus doctor`, `nexus security`, `nexus specs` and `nexus history`
// (0.1.95, round-4 audit L1: they decided it in three different ways --
// the prefix alone, the prefix and the policy, and not at all).
enum class UpdateSigning {
    NotAnImage,     // nothing to judge: no origin
    Enforced,       // a Nexus image, signed: set, the policy requires
                    // the Nexus key and the key is there
    OtherSigned,    // signed: set, not a Nexus image -- checked against
                    // whatever that image's policy says
    PolicyMissing,  // a Nexus image, signed: set, but the policy does
                    // not require the Nexus key (or the key is gone):
                    // the prefix alone checks nothing of Nexus's
    NotChecked,     // followed without a signature check
};

// origin: the deployment's line in rpm-ostree status
// ("ostree-image-signed:docker://ghcr.io/..."). policyJson: the text of
// /etc/containers/policy.json. keyPresent: whether the Nexus public key
// file is on the machine.
UpdateSigning judgeUpdateSigning(const std::string& origin,
                                 const std::string& policyJson,
                                 bool keyPresent);

// Where the Nexus images and their key are; said once.
inline constexpr const char* kNexusImageScope = "ghcr.io/davidosdas-official";
inline constexpr const char* kNexusPublicKey = "/etc/pki/containers/nexus-core.pub";
inline constexpr const char* kContainersPolicy = "/etc/containers/policy.json";

struct AppUpdate {
    std::string id;       // org.mozilla.firefox
    std::string version;  // may be empty
    // Not updated without a yes, and why (0.2.6, NX-02): the new version
    // would get a permission that needs one. Empty when it goes ahead.
    std::string held;
};

// Reads `flatpak remote-ls --updates --app --columns=application,version`.
std::vector<AppUpdate> parseFlatpakUpdates(const std::string& text);

// One line for a notification, or empty when there is nothing new:
// "Nexus 44.20261005.0 and 2 app updates".
std::string updateSummary(const SystemUpdate& system,
                          const std::vector<AppUpdate>& apps);

// What `nexus update --lines` prints, read back by Nexus Shop. One
// fact per line, tab-separated, first field the kind of line:
//   running <version> <day>
//   staged  <version>
//   system  new|current|unreachable|not-image <version> <day> <diff>
//   app     <id> <version> [held: why it waits for a yes (0.2.6)]
// Unknown kinds are skipped, so a newer nexus can add lines without
// breaking an older Shop.
struct UpdateLines {
    std::string running;
    std::string runningDay;
    std::string staged;
    std::string system;   // empty when no system line was printed
    std::string version;
    std::string day;
    std::string diff;
    std::vector<AppUpdate> apps;
};

UpdateLines parseUpdateLines(const std::string& text);

// The daily "update available" notification can be turned off per
// account (0.1.86, outside review round 3, P5): a file in the person's
// own configuration, which the user unit checks before it starts
// (ConditionPathExists=!%h/.config/nexus/update-notify-off). Nothing
// is checked or contacted while it is there.
std::string updateNotifyOffFile(const std::string& home);
bool updateNotifyIsOff(const std::string& home);
// on: removes the file; off: writes it. False, with the reason in
// error, if that could not be done.
bool setUpdateNotify(const std::string& home, bool on, std::string& error);

}
