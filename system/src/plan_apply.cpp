#include <nexus/system/plan_apply.hpp>

#include <cstdio>
#include <cstdlib>
#include <nexus/system/process.hpp>
#include <sys/statvfs.h>
#include <unistd.h>

namespace nexus::system {

bool haveRootPrivileges() {
    return ::geteuid() == 0;
}

std::string toString(ApplyOutcome outcome) {
    switch (outcome) {
        case ApplyOutcome::Applied:
            return "applied";
        case ApplyOutcome::Staged:
            // Not "applied". The deployment exists and the running
            // system is unchanged until it reboots into it, and a
            // record saying otherwise would be false for as long as
            // the machine stays up.
            return "staged";
        case ApplyOutcome::NeedsRoot:
            return "needs-root";
        case ApplyOutcome::Refused:
            return "refused";
        case ApplyOutcome::Failed:
            return "failed";
        case ApplyOutcome::Unavailable:
            return "unavailable";
    }

    return "unavailable";
}

namespace {

ApplyResult runApt(const std::string& arguments) {
    ApplyResult result;

    if (!commandExists("apt-get")) {
        result.outcome = ApplyOutcome::Unavailable;
        return result;
    }

    if (!haveRootPrivileges()) {
        result.outcome = ApplyOutcome::NeedsRoot;
        return result;
    }

    const ProcessResult ran = runCommand("apt-get " + arguments);

    if (!ran.ran) {
        result.outcome = ApplyOutcome::Failed;
        return result;
    }

    result.output = ran.lines;
    result.exitCode = ran.exitCode;
    result.outcome = ran.ok
        ? ApplyOutcome::Applied
        : ApplyOutcome::Failed;

    return result;
}

}

namespace {

ApplyResult runDnf(const std::string& arguments) {
    ApplyResult result;

    if (!commandExists("dnf")) {
        result.outcome = ApplyOutcome::Unavailable;
        return result;
    }

    if (!haveRootPrivileges()) {
        result.outcome = ApplyOutcome::NeedsRoot;
        return result;
    }

    const ProcessResult ran = runCommand("dnf " + arguments);

    if (!ran.ran) {
        result.outcome = ApplyOutcome::Failed;
        return result;
    }

    result.output = ran.lines;
    result.exitCode = ran.exitCode;
    result.outcome = ran.ok
        ? ApplyOutcome::Applied
        : ApplyOutcome::Failed;

    return result;
}

}

ApplyResult applyWithDnf(const std::string& requested) {
    return runDnf(
        "install -y --setopt=install_weak_deps=False " +
        shellQuote(requested));
}

ApplyResult removeWithDnf(
    const std::vector<std::string>& packages
) {
    if (packages.empty()) {
        ApplyResult result;
        result.outcome = ApplyOutcome::Refused;
        return result;
    }

    std::string arguments = "remove -y";

    for (const std::string& name : packages) {
        arguments += " " + shellQuote(name);
    }

    return runDnf(arguments);
}

ApplyResult removeWithApt(
    const std::vector<std::string>& packages
) {
    if (packages.empty()) {
        ApplyResult result;
        result.outcome = ApplyOutcome::Refused;
        return result;
    }

    std::string arguments = "remove -y";

    for (const std::string& name : packages) {
        arguments += " " + shellQuote(name);
    }

    return runApt(arguments);
}

ApplyResult applyWithApt(const std::string& requested) {
    ApplyResult result;

    if (!commandExists("apt-get")) {
        result.outcome = ApplyOutcome::Unavailable;
        return result;
    }

    if (!haveRootPrivileges()) {
        result.outcome = ApplyOutcome::NeedsRoot;
        return result;
    }

    // -y is deliberate: the person was asked already, by Nexus, with
    // the plan in front of them. Asking twice trains people to say
    // yes without reading.
    return runApt(
        "install -y --no-install-recommends " + shellQuote(requested));
}

}

namespace nexus::system {

bool isImageBased() {
    return isImageBasedAt("");
}

bool isImageBasedAt(const std::string& root) {
    // rpm-ostree being present is not enough: a machine can have the
    // tool and a writable /usr, and there layering would be the wrong
    // answer.
    if (!commandExists("rpm-ostree")) {
        return false;
    }
    if (::access((root + "/run/ostree-booted").c_str(), F_OK) == 0) {
        return true;
    }
    struct statvfs info {};
    return ::statvfs((root + "/usr").c_str(), &info) == 0 &&
           (info.f_flag & ST_RDONLY) != 0;
}

ApplyResult layerWithRpmOstree(const std::vector<std::string>& names) {
    ApplyResult result;

    std::string quoted;
    for (const auto& name : names) {
        if (!isPackageName(name)) {
            result.outcome = ApplyOutcome::Refused;
            result.exitCode = 2;
            result.output.push_back("Not a package name, nothing done: " + name);
            return result;
        }
        quoted += " " + shellQuote(name);
    }
    if (quoted.empty()) {
        result.outcome = ApplyOutcome::Refused;
        result.exitCode = 2;
        result.output.push_back("No packages given.");
        return result;
    }

    // --idempotent: asking for something already layered is not a
    // failure, and treating it as one turns a repeated setup into an
    // error the user cannot act on.
    const std::string command =
        "rpm-ostree install --idempotent --allow-inactive" + quoted;

    const ProcessResult ran = runCommand(command, true);

    result.exitCode = ran.exitCode;
    result.output.push_back(ran.text);

    if (!ran.ran) {
        result.outcome = ApplyOutcome::Unavailable;
        return result;
    }

    // Staged, not applied. The deployment exists; the running system
    // is unchanged until it reboots into it.
    result.outcome = ran.exitCode == 0
        ? ApplyOutcome::Staged
        : ApplyOutcome::Failed;

    return result;
}

}
