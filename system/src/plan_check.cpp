#include <nexus/system/plan_check.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/wait.h>

namespace nexus::system {

PlanCheck checkPlanWithApt(
    const std::string& requested,
    const std::set<std::string>& expected
) {
    PlanCheck check;

    if (std::system("apt-get --version > /dev/null 2>&1") != 0) {
        check.agreement = PlanAgreement::Unavailable;
        return check;
    }

    // A dry run changes nothing and needs no privileges. Its stderr
    // is kept: discarding it turns a refusal into a statement with no
    // content, and the tool was saying exactly what was wrong.
    const std::string errors = "/tmp/nexus-plan-check-error";

    const std::string command =
        "apt-get install --dry-run --no-install-recommends '" +
        requested + "' 2>" + errors;

    std::FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        check.agreement = PlanAgreement::Unavailable;
        return check;
    }

    char line[4096];

    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        const std::string text(line);

        if (text.rfind("Inst ", 0) != 0) {
            continue;
        }

        const std::size_t start = 5;
        const std::size_t end = text.find(' ', start);

        check.theirs.insert(text.substr(start, end - start));
    }

    const int status = pclose(pipe);
    const int code = status == -1 ? -1 : WEXITSTATUS(status);

    if (code != 0 && check.theirs.empty()) {
        check.agreement = PlanAgreement::Refused;

        std::ifstream why(errors);

        if (why) {
            std::string message;

            while (std::getline(why, message)) {
                if (!message.empty()) {
                    check.refusal.push_back(message);
                }
            }
        }

        return check;
    }

    for (const std::string& name : expected) {
        if (check.theirs.count(name) == 0) {
            check.onlyOurs.push_back(name);
        }
    }

    for (const std::string& name : check.theirs) {
        if (expected.count(name) == 0) {
            check.onlyTheirs.push_back(name);
        }
    }

    check.agreement =
        (check.onlyOurs.empty() && check.onlyTheirs.empty())
            ? PlanAgreement::Agrees
            : PlanAgreement::Differs;

    return check;
}

}
