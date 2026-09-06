#include <nexus/system/process.hpp>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

namespace nexus::system {

bool commandExists(const std::string& name) {
    return std::system(
        ("command -v '" + name + "' > /dev/null 2>&1").c_str()) == 0;
}

ProcessResult runCommand(
    const std::string& command,
    bool captureErrors
) {
    ProcessResult result;

    const std::string full =
        captureErrors ? command + " 2>&1" : command;

    std::FILE* pipe = popen(full.c_str(), "r");

    if (pipe == nullptr) {
        return result;
    }

    result.ran = true;

    char buffer[65536];

    while (true) {
        errno = 0;

        if (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result.text += buffer;
            continue;
        }

        // Null means one of three things, and only one of them is the
        // end. A signal during the read sets EINTR and leaves more to
        // come; stopping there loses the rest.
        if (std::feof(pipe)) {
            break;
        }

        if (std::ferror(pipe) && errno == EINTR) {
            clearerr(pipe);
            continue;
        }

        break;
    }

    const int status = pclose(pipe);

    result.exitCode = status == -1 ? -1 : WEXITSTATUS(status);
    result.ok = result.exitCode == 0;

    // Split once, at the end, rather than line by line while reading.
    std::string current;

    for (char character : result.text) {
        if (character == '\n') {
            result.lines.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    if (!current.empty()) {
        result.lines.push_back(current);
    }

    return result;
}

}
