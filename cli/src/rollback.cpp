// nexus rollback and nexus rebase.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// ---------------------------------------------------------------------
// nexus rollback / nexus rebase (0.1.73)

std::string describeDeployment(const Deployment& d) {
    std::string out = d.version.empty() ? "?" : d.version;
    if (!d.date.empty()) {
        std::string when = d.date;
        if (when.size() >= 16) when = when.substr(0, 10) + " " + when.substr(11, 5) + " UTC";
        out += ", built " + when;
    }
    if (!d.digest.empty()) out += "  (" + shortDigest(d.digest) + ")";
    return out;
}

struct EtcChange {
    std::string path;
    bool added = false;   // not in the other version at all
};

// Files in /etc changed while this version ran, that the other version
// does not have in the same form. Going back boots the other version's
// /etc, so these would not be there.
std::vector<EtcChange> etcChangesSince(const std::string& deployDir,
                                       const std::string& otherEtc,
                                       bool& unreadable) {
    namespace fs = std::filesystem;
    std::vector<EtcChange> out;
    std::error_code ec;
    const auto since = fs::last_write_time(deployDir + ".origin", ec);
    if (ec) return out;

    static const std::vector<std::string> noise = {
        "/etc/.updated", "/etc/.pwd.lock", "/etc/ld.so.cache", "/etc/machine-info",
        "/etc/pki/ca-trust/extracted/"};

    auto it = fs::recursive_directory_iterator(
        "/etc", fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        std::error_code e2;
        if (!entry.is_regular_file(e2) || entry.is_symlink(e2)) continue;
        const std::string path = entry.path().string();
        bool skip = false;
        for (const auto& n : noise) if (path.rfind(n, 0) == 0) skip = true;
        if (skip) continue;
        const auto when = entry.last_write_time(e2);
        if (e2 || when <= since) continue;

        const std::string other = otherEtc + path.substr(4);
        if (!fs::exists(other, e2)) { out.push_back({path, true}); continue; }
        std::ifstream a(path, std::ios::binary), b(other, std::ios::binary);
        if (!a || !b) { unreadable = true; continue; }
        std::stringstream sa, sb; sa << a.rdbuf(); sb << b.rdbuf();
        if (sa.str() != sb.str()) out.push_back({path, false});
    }
    return out;
}

// Whether two files hold exactly the same bytes.
bool sameContents(const std::string& a, const std::string& b) {
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> ba(65536), bb(65536);
    while (true) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        const auto na = fa.gcount(), nb = fb.gcount();
        if (fa.bad() || fb.bad() || na != nb ||
            std::memcmp(ba.data(), bb.data(), static_cast<std::size_t>(na)) != 0) return false;
        if (na < static_cast<std::streamsize>(ba.size())) return true;   // both ended here
    }
}

}  // namespace

int commandRollback(bool apply, const std::string& invocation) {
    using namespace nexus::system;
    namespace fs = std::filesystem;

    const auto deployments = readDeployments();
    const auto ostree = commandExists("ostree")
        ? parseOstreeAdminStatus(runCommand("ostree admin status 2>/dev/null", false).text)
        : std::vector<OstreeDeployment>{};
    if (deployments.size() < 2 || ostree.size() != deployments.size()) {
        std::cout << "There is no earlier system version on this machine to go back to.\n";
        return deployments.empty() ? 2 : 0;
    }

    std::size_t now = 0, back = 0;
    bool haveNow = false, haveBack = false, staged = false;
    for (std::size_t i = 0; i < ostree.size(); ++i) {
        if (ostree[i].booted) { now = i; haveNow = true; }
        else if (ostree[i].flag == "staged" || ostree[i].flag == "pending") staged = true;
        else if (!haveBack) { back = i; haveBack = true; }
    }
    if (!haveNow || !haveBack) {
        std::cout << "There is no earlier system version on this machine to go back to.\n";
        return 0;
    }
    if (staged) {
        std::cout << "An update is waiting for a restart. To drop it instead of going back:\n\n"
                     "    sudo rpm-ostree cleanup --pending\n\n"
                     "Then run this again.\n";
        return 1;
    }

    const Deployment& cur = deployments[now];
    const Deployment& prev = deployments[back];
    std::cout << "Now:         " << describeDeployment(cur) << "\n"
              << "Go back to:  " << describeDeployment(prev) << "\n";

    // apps added on top that the other version does not have
    std::vector<std::string> notThere;
    for (const auto& p : cur.layered)
        if (std::find(prev.layered.begin(), prev.layered.end(), p) == prev.layered.end()) notThere.push_back(p);
    if (!notThere.empty()) {
        std::cout << "Not there:   ";
        for (std::size_t i = 0; i < notThere.size(); ++i) std::cout << (i ? ", " : "") << notThere[i];
        std::cout << " (added on top since; reinstall after going back if you want them)\n";
    }

    const std::string root = "/ostree/deploy/" + ostree[now].stateroot + "/deploy/";
    bool unreadable = false;
    const auto changes = etcChangesSince(root + ostree[now].id, root + ostree[back].id + "/etc", unreadable);
    if (!changes.empty()) {
        std::cout << "\nSettings:    " << changes.size()
                  << " file(s) in /etc changed while this version ran. The other\n"
                     "             version starts with its own /etc, so they would not be there:\n";
        for (std::size_t i = 0; i < changes.size() && i < 8; ++i)
            std::cout << "               " << changes[i].path << (changes[i].added ? "  (new)" : "") << "\n";
        if (changes.size() > 8) std::cout << "               ... and " << changes.size() - 8 << " more\n";
        std::cout << "             With --apply they are saved first, to /var/lib/nexus/etc-saved/.\n";
    } else {
        std::cout << "\nSettings:    nothing in /etc changed while this version ran.\n";
    }
    if (unreadable && ::geteuid() != 0)
        std::cout << "             (Some files need root to compare: sudo nexus rollback shows all.)\n";
    std::cout << "Your files:  untouched (your home folder is not part of the system version).\n";

    if (!apply) {
        std::cout << "\nNothing has been changed. To go back:\n    sudo nexus rollback --apply\n"
                     "Going back later the other way is the same command again.\n";
        return 0;
    }
    if (::geteuid() != 0) {
        std::cout << "\nGoing back needs root:\n    sudo nexus rollback --apply\n";
        return 1;
    }

    // Every file is saved, and checked, before anything is rolled back:
    // a disk that is full or a file that will not copy stops it here.
    // (An outside review, November 2026: the copies' errors were not
    // read, so a failed save still went on to roll back.)
    std::string savedTo;
    if (!changes.empty()) {
        savedTo = "/var/lib/nexus/etc-saved/" + currentTimestamp();
        std::vector<std::string> notSaved;
        for (const auto& c : changes) {
            const fs::path dest = fs::path(savedTo) / c.path.substr(1);
            std::error_code ec;
            fs::create_directories(dest.parent_path(), ec);
            if (!ec) fs::copy_file(c.path, dest, fs::copy_options::overwrite_existing, ec);
            // Read back and compared byte for byte (0.1.77, second
            // outside review: equal sizes do not mean equal files).
            const bool same = !ec && sameContents(c.path, dest.string());
            if (!same) notSaved.push_back(c.path + (ec ? " (" + ec.message() + ")" : ""));
        }
        if (!notSaved.empty()) {
            std::cout << "\nNot rolled back: these could not be saved first --\n";
            for (const auto& n : notSaved) std::cout << "    " << n << "\n";
            std::cout << "Nothing has been changed. Free some space (df -h /var) and try again.\n";
            return 1;
        }
    }

    TransactionRecord record;
    record.when = currentTimestamp();
    record.request = "rollback";
    record.kind = TransactionKind::Install;
    record.resolved = "system version " + describeDeployment(prev);
    const std::string log = defaultTransactionLog();
    const std::string marker = beginTransaction(log, record);
    std::cout << "\n" << std::flush;
    const bool ok = std::system("rpm-ostree rollback") == 0;
    record.outcome = ok ? "staged" : "failed";
    record.succeeded = ok; record.exitCode = ok ? 0 : 1;
    finishTransaction(log, marker, record);

    if (!ok) { std::cout << "\nNothing changed: rpm-ostree could not reorder the versions.\n"; return 1; }
    std::cout << "\nDone: the earlier version starts next time.\n\n    systemctl reboot\n";
    if (!savedTo.empty())
        std::cout << "\nSaved settings: " << savedTo << "\n"
                     "Copy back what you need after the restart, e.g.\n"
                     "    sudo cp -a " << savedTo << "/etc/<file> /etc/<file>\n";
    return 0;
}

int commandRebase(const std::vector<std::string>& raw, bool apply, const std::string& invocation) {
    using namespace nexus::system;
    const auto deployments = readDeployments();
    const Deployment* booted = nullptr;
    for (const auto& d : deployments) if (d.booted) booted = &d;
    if (!booted) {
        std::cout << "This is not an image-based system, so there is nothing to rebase.\n";
        return 2;
    }
    const auto ref = parseImageRef(booted->image);
    if (!ref) {
        std::cout << "Could not read the image this system follows: " << booted->image << "\n";
        return 1;
    }
    std::string edition = editionOf(*ref);
    if (edition.empty()) edition = osReleaseValue("VARIANT_ID");

    const std::string target = raw.empty() ? "" : raw[0];
    if (target.empty()) {
        std::cout << "Follows:   " << imageRefString(*ref) << "\n"
                  << "Running:   " << describeDeployment(*booted) << "\n"
                  << (ref->digest.empty() ? "Updates:   yes, every new build of this edition\n"
                                          : "Updates:   no -- pinned to one build\n")
                  << "\nCommands:\n"
                     "  nexus rebase kde | minimal            switch edition (apps you added stay)\n"
                     "  nexus rebase sha256:<digest>          pin one exact build (stops updates)\n"
                     "  nexus rebase latest                   follow the newest build again\n"
                     "  nexus rebase signed                   check every update's signature\n"
                     "Older builds and their digests: github.com/DAVIDOSDAS-official/nexus-core/pkgs/container/nexus-core/versions\n";
        return 0;
    }

    ImageRef next = *ref;
    std::string what;
    if (target == "kde" || target == "minimal") {
        next.digest.clear(); next.tag = target;
        what = "the " + target + " edition";
    } else if (target == "latest") {
        if (edition.empty()) { std::cout << "Which edition? nexus rebase kde | minimal\n"; return 2; }
        next.digest.clear(); next.tag = edition;
        what = "the newest " + edition + " build, and its updates";
    } else if (target == "signed") {
        what = "the same image, with every update's signature checked";
    } else if (validDigest(target)) {
        next.tag.clear(); next.digest = target;
        what = "exactly build " + shortDigest(target) + " (no updates until: nexus rebase latest)";
    } else {
        std::cout << "Not something to rebase to: " << target << "\n"
                     "    kde | minimal | latest | sha256:<64 hex characters>\n";
        return 2;
    }
    // A Nexus image is always followed signature-checked from here on,
    // whatever the machine was installed with (0.1.84).
    next.transport = transportFor(next);
    if (next.transport.rfind("ostree-image-signed:", 0) == 0) {
        std::ifstream pol("/etc/containers/policy.json");
        std::stringstream pb;
        pb << pol.rdbuf();
        if (!policyRequiresKey(pb.str(), "ghcr.io/davidosdas-official", "/etc/pki/containers/nexus-core.pub")) {
            std::cout << "This machine's /etc/containers/policy.json does not require the Nexus key,\n"
                         "so a signature-checked switch would be refused. First:\n"
                         "    sudo python3 /usr/libexec/nexus/merge-policy\n";
            return 1;
        }
    }
    const std::string nextRef = imageRefString(next);
    if (nextRef == imageRefString(*ref)) {
        std::cout << "Already following " << nextRef << ".\n";
        return 0;
    }

    std::cout << "Now:       " << imageRefString(*ref) << "\n"
              << "Switch to: " << what << "\n"
              << "           " << nextRef << "\n"
              << "Kept:      your files, your settings (/etc), apps you added on top\n"
              << "Starts:    after a restart; the current version stays in the boot menu\n";
    if (target == "kde" || target == "minimal")
        std::cout << "Note:      the other edition has a different desktop and add-ons;\n"
                     "           its first start is checked like any update.\n";
    if (!apply) {
        std::cout << "\nNothing has been changed. To do it:\n    sudo " << invocation << " --apply\n";
        return 0;
    }
    if (::geteuid() != 0) { std::cout << "\nThis needs root:\n    sudo " << invocation << " --apply\n"; return 1; }

    TransactionRecord record;
    record.when = currentTimestamp();
    record.request = "rebase " + target;
    record.kind = TransactionKind::Install;
    record.resolved = nextRef;
    const std::string log = defaultTransactionLog();
    const std::string marker = beginTransaction(log, record);
    std::cout << "\n" << std::flush;
    const bool ok = std::system(("rpm-ostree rebase " + shellQuote(nextRef)).c_str()) == 0;
    record.outcome = ok ? "staged" : "failed";
    record.succeeded = ok; record.exitCode = ok ? 0 : 1;
    finishTransaction(log, marker, record);
    if (ok) std::cout << "\nStaged. Restart to use it:\n    systemctl reboot\n";
    return ok ? 0 : 1;
}

}  // namespace nexus_cli
