#include <nexus/system/nix_source.hpp>

#include <sstream>

#include <nexus/system/process.hpp>

namespace nexus::system {

namespace {

// Remove terminal escape sequences.
//
// nix colours its output, and a value that reads as "hyprland" on a
// terminal arrives as "\033[1mhyprland\033[0m" through a pipe. The
// parse succeeds, the count is right, and every name is unfindable --
// which looks like the components never being loaded at all.
std::string withoutEscapes(const std::string& text) {
    std::string clean;

    clean.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\033') {
            clean.push_back(text[index]);
            continue;
        }

        // CSI sequences end at the first byte in @ to ~.
        std::size_t end = index + 1;

        if (end < text.size() && text[end] == '[') {
            end += 1;

            while (end < text.size() &&
                   (text[end] < '@' || text[end] > '~')) {
                end += 1;
            }
        }

        index = end;
    }

    return clean;
}

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");

    if (begin == std::string::npos) {
        return "";
    }

    const std::size_t end = text.find_last_not_of(" \t\r\n");

    return text.substr(begin, end - begin + 1);
}

}

std::string versionFromStorePath(
    const std::string& path,
    const std::string& name
) {
    // /nix/store/<32-char hash>-<name>-<version>
    const std::size_t slash = path.rfind('/');

    const std::string base =
        slash == std::string::npos ? path : path.substr(slash + 1);

    const std::size_t dash = base.find('-');

    if (dash == std::string::npos) {
        return "";
    }

    const std::string rest = base.substr(dash + 1);

    if (name.empty() || rest.rfind(name, 0) != 0) {
        return "";
    }

    const std::string tail = rest.substr(name.size());

    if (tail.empty() || tail.front() != '-') {
        return "";
    }

    const std::string version = tail.substr(1);

    // Outputs are suffixed -man, -doc, -dev. Those name a part of the
    // derivation rather than a version, and the suffix sits at the
    // end of the whole string rather than being the whole of it:
    // hyprland's man output is "0.54.0+date=2026-04-24_e3c9b64-man".
    // Checking for equality only caught the short case and reported
    // the man pages' path as the version.
    // Two shapes: the output can be the whole of what follows the
    // name ("hyprland-man" gives "man"), or the tail of a real
    // version ("...e3c9b64-man"). Checking one and not the other
    // fixed the long case and broke the short one.
    for (const char* output :
         {"man", "doc", "dev", "lib", "out", "info", "debug"}) {

        const std::string name(output);

        if (version == name) {
            return "";
        }

        const std::string suffix = "-" + name;

        if (version.size() > suffix.size() &&
            version.compare(
                version.size() - suffix.size(),
                suffix.size(),
                suffix) == 0) {

            return "";
        }
    }

    return version;
}

NixSourceResult parseNixProfile(const std::string& text) {
    NixSourceResult result;

    const std::string plain = withoutEscapes(text);

    std::istringstream input(plain);
    std::string line;

    std::string name;
    std::string storePaths;

    const auto finish = [&]() {
        if (name.empty()) {
            return;
        }

        std::string version;

        // The first store path names the derivation; later ones are
        // its other outputs.
        std::istringstream paths(storePaths);
        std::string path;

        while (paths >> path) {
            version = versionFromStorePath(path, name);

            if (!version.empty()) {
                break;
            }
        }

        Component component(
            name, name, version, ComponentType::Application);

        component.setSource(Source::Nix);
        component.setArchitecture(kArchitectureAll);
        component.addProvidedCapability(Capability(name));

        result.components.push_back(std::move(component));
        result.installed += 1;

        name.clear();
        storePaths.clear();
    };

    while (std::getline(input, line)) {
        const std::size_t colon = line.find(':');

        if (colon == std::string::npos) {
            if (trim(line).empty()) {
                finish();
            }

            continue;
        }

        const std::string field = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));

        if (field == "Name") {
            // A new entry begins where its name does.
            finish();
            name = value;
        } else if (field == "Store paths") {
            storePaths = value;
        }
    }

    finish();

    return result;
}

NixSourceResult readNixProfile() {
    NixSourceResult result;

    if (!commandExists("nix")) {
        result.error = "nix is not installed";
        return result;
    }

    // nix profile, not nix-env: a profile written by the newer
    // command is rejected by the older one, and the error says so
    // rather than reporting an empty profile.
    //
    // NO_COLOR is the convention rather than a flag, so it works on
    // versions that would reject one -- an unrecognised flag would
    // print an error of its own and leave the user reading about a
    // problem Nexus caused.
    //
    // The parser strips escapes regardless: asking is not the same as
    // being obeyed.
    const ProcessResult listed =
        runCommand("NO_COLOR=1 nix profile list", false);

    if (!listed.ran) {
        result.error = "could not run nix";
        return result;
    }

    if (listed.text.empty()) {
        // An empty profile is not a failure.
        return result;
    }

    return parseNixProfile(listed.text);
}

}
