#include <nexus/system/protection.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <sys/utsname.h>

#include <nexus/system/version.hpp>

namespace nexus::system {

namespace {

// Pull the quoted strings out of a named block: NAME { "a"; "b"; };
std::vector<std::string> blockEntries(
    const std::string& text,
    const std::string& name
) {
    std::vector<std::string> entries;

    const std::size_t start = text.find(name);

    if (start == std::string::npos) {
        return entries;
    }

    const std::size_t open = text.find('{', start);

    if (open == std::string::npos) {
        return entries;
    }

    const std::size_t close = text.find('}', open);

    const std::string body = text.substr(
        open + 1,
        (close == std::string::npos ? text.size() : close) - open - 1
    );

    std::size_t position = 0;

    while (true) {
        const std::size_t first = body.find('"', position);

        if (first == std::string::npos) {
            break;
        }

        const std::size_t second = body.find('"', first + 1);

        if (second == std::string::npos) {
            break;
        }

        entries.push_back(body.substr(first + 1, second - first - 1));
        position = second + 1;
    }

    return entries;
}

// Compiling a POSIX regex is expensive, and these same patterns are
// otherwise recompiled for every package: ten patterns across three
// thousand packages is thirty thousand compilations of ten
// expressions. Compiled once here and kept.
//
// A pattern that will not compile is remembered as a failure rather
// than retried, so the cost is paid once either way.
//
// Returns nullptr when the pattern cannot be compiled.
const std::regex* compiledPattern(const std::string& pattern) {
    static std::map<std::string, std::optional<std::regex>> cache;

    const auto entry = cache.find(pattern);

    if (entry != cache.end()) {
        return entry->second ? &*entry->second : nullptr;
    }

    try {
        // apt writes POSIX extended regular expressions.
        const auto inserted = cache.emplace(
            pattern, std::regex(pattern, std::regex::extended));

        return &*inserted.first->second;
    } catch (const std::regex_error&) {
        // A pattern this implementation cannot compile is skipped
        // rather than aborting the whole rule set. One unreadable
        // line should not disable every protection.
        cache.emplace(pattern, std::nullopt);

        return nullptr;
    }
}

bool matchesAny(
    const std::string& name,
    const std::vector<std::string>& patterns
) {
    for (const std::string& pattern : patterns) {
        const std::regex* expression = compiledPattern(pattern);

        if (expression != nullptr &&
            std::regex_search(name, *expression)) {
            return true;
        }
    }

    return false;
}

}

bool ProtectionRules::empty() const {
    return neverRemove.empty() && kernelPatterns.empty() &&
           neverAutoSections.empty();
}

ProtectionRules parseProtectionRules(const std::string& text) {
    ProtectionRules rules;

    rules.neverRemove = blockEntries(text, "NeverAutoRemove");
    rules.kernelPatterns =
        blockEntries(text, "VersionedKernelPackages");
    rules.neverAutoSections =
        blockEntries(text, "Never-MarkAuto-Sections");

    return rules;
}

ProtectionRules readProtectionRules(const std::string& directory) {
    ProtectionRules rules;

    std::error_code error;

    if (!std::filesystem::is_directory(directory, error)) {
        return rules;
    }

    std::vector<std::filesystem::path> files;

    for (const auto& entry :
         std::filesystem::directory_iterator(directory, error)) {

        if (entry.is_regular_file(error)) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        std::ifstream input(file);

        if (!input) {
            continue;
        }

        std::ostringstream buffer;

        buffer << input.rdbuf();

        const ProtectionRules found =
            parseProtectionRules(buffer.str());

        for (const std::string& entry : found.neverRemove) {
            rules.neverRemove.push_back(entry);
        }

        for (const std::string& entry : found.kernelPatterns) {
            rules.kernelPatterns.push_back(entry);
        }

        for (const std::string& entry : found.neverAutoSections) {
            rules.neverAutoSections.push_back(entry);
        }
    }

    return rules;
}

std::string runningKernelRelease() {
    struct utsname information {};

    if (uname(&information) != 0) {
        return "";
    }

    return information.release;
}

std::set<std::string> protectedComponents(
    const std::vector<Component>& installed,
    const ProtectionRules& rules,
    const std::string& runningKernel
) {
    std::set<std::string> protectedIds;

    // Kernel packages, so the newest can be found among them.
    std::vector<const Component*> kernels;

    for (const Component& component : installed) {
        if (matchesAny(component.name(), rules.neverRemove)) {
            protectedIds.insert(component.id());
        }

        if (!matchesAny(component.name(), rules.kernelPatterns)) {
            continue;
        }

        kernels.push_back(&component);

        // The kernel that is running. Removing it leaves a machine
        // that does not boot, which is the worst thing this tool
        // could ever be wrong about.
        if (!runningKernel.empty() &&
            component.name().find(runningKernel) != std::string::npos) {
            protectedIds.insert(component.id());
        }
    }

    // The newest kernel, so there is a fallback if the running one
    // turns out to be broken. This is what apt does and the reason it
    // keeps one more than the graph says is needed.
    const Component* newest = nullptr;

    for (const Component* kernel : kernels) {
        if (newest == nullptr ||
            compareVersions(kernel->version(), newest->version()) > 0) {
            newest = kernel;
        }
    }

    if (newest != nullptr) {
        protectedIds.insert(newest->id());
    }

    return protectedIds;
}

}
