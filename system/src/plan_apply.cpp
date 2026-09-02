#include <nexus/system/plan_apply.hpp>

#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace nexus::system {

bool haveRootPrivileges() {
    return ::geteuid() == 0;
}

std::string toString(ApplyOutcome outcome) {
    switch (outcome) {
        case ApplyOutcome::Applied:
            return "applied";
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

    if (std::system("apt-get --version > /dev/null 2>&1") != 0) {
        result.outcome = ApplyOutcome::Unavailable;
        return result;
    }

    if (!haveRootPrivileges()) {
        result.outcome = ApplyOutcome::NeedsRoot;
        return result;
    }

    const std::string command = "apt-get " + arguments + " 2>&1";

    std::FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        result.outcome = ApplyOutcome::Failed;
        return result;
    }

    char line[4096];

    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        std::string text(line);

        if (!text.empty() && text.back() == '\n') {
            text.pop_back();
        }

        result.output.push_back(text);
    }

    const int status = pclose(pipe);

    result.exitCode = status == -1 ? -1 : WEXITSTATUS(status);
    result.outcome = result.exitCode == 0
        ? ApplyOutcome::Applied
        : ApplyOutcome::Failed;

    return result;
}

}

namespace {

ApplyResult runDnf(const std::string& arguments) {
    ApplyResult result;

    if (std::system("dnf --version > /dev/null 2>&1") != 0) {
        result.outcome = ApplyOutcome::Unavailable;
        return result;
    }

    if (!haveRootPrivileges()) {
        result.outcome = ApplyOutcome::NeedsRoot;
        return result;
    }

    const std::string command = "dnf " + arguments + " 2>&1";

    std::FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        result.outcome = ApplyOutcome::Failed;
        return result;
    }

    char line[4096];

    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        std::string text(line);

        if (!text.empty() && text.back() == '\n') {
            text.pop_back();
        }

        result.output.push_back(text);
    }

    const int status = pclose(pipe);

    result.exitCode = status == -1 ? -1 : WEXITSTATUS(status);
    result.outcome = result.exitCode == 0
        ? ApplyOutcome::Applied
        : ApplyOutcome::Failed;

    return result;
}

}

ApplyResult applyWithDnf(const std::string& requested) {
    return runDnf(
        "install -y --setopt=install_weak_deps=False '" +
        requested + "'");
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
        arguments += " '" + name + "'";
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
        arguments += " '" + name + "'";
    }

    return runApt(arguments);
}

ApplyResult applyWithApt(const std::string& requested) {
    ApplyResult result;

    if (std::system("apt-get --version > /dev/null 2>&1") != 0) {
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
        "install -y --no-install-recommends '" + requested + "'");
}

}
