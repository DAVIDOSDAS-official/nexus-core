#include <nexus/system/compression.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <sstream>

namespace nexus::system {

namespace {

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(
               text.size() - suffix.size(),
               suffix.size(),
               suffix
           ) == 0;
}

}

std::string decompressTool(const std::string& path) {
    if (endsWith(path, ".zst")) {
        return "zstd";
    }

    if (endsWith(path, ".zck")) {
        return "unzck";
    }

    if (endsWith(path, ".lz4")) {
        return "lz4";
    }

    if (endsWith(path, ".gz")) {
        return "gzip";
    }

    if (endsWith(path, ".xz")) {
        return "xz";
    }

    if (endsWith(path, ".bz2")) {
        return "bzip2";
    }

    return "";
}

std::string decompressCommand(const std::string& path) {
    const std::string tool = decompressTool(path);

    if (tool.empty()) {
        return "";
    }

    // zchunk is the odd one out: -c, not -dc.
    if (tool == "unzck") {
        return "unzck -c";
    }

    // --long=31: zstd refuses to decompress a frame whose window is
    // larger than its default limit, and Fedora's full primary.xml is
    // compressed with a large one. Without this it fails on every
    // repository big enough to matter and works on the small ones.
    if (tool == "zstd") {
        return "zstd -dc --long=31";
    }

    return tool + " -dc";
}

bool readPossiblyCompressed(
    const std::string& path,
    std::string& contents,
    std::string& reason
) {
    const std::string command = decompressCommand(path);

    if (command.empty()) {
        std::ifstream input(path, std::ios::binary);

        if (!input) {
            reason = "cannot open file";
            return false;
        }

        std::ostringstream buffer;

        buffer << input.rdbuf();
        contents = buffer.str();

        return true;
    }

    const std::string tool = decompressTool(path);

    if (std::system(("command -v " + tool +
                     " > /dev/null 2>&1").c_str()) != 0) {
        reason = "needs '" + tool + "', which is not installed";
        return false;
    }

    // Keep stderr. Discarding it turns every decompression failure
    // into "produced no output", which says nothing about why.
    // Process-specific: a fixed name in a sticky /tmp cannot be
    // rewritten by a different user.
    const std::string errors =
        "/tmp/nexus-decompress-" + std::to_string(::getpid());

    std::FILE* pipe = popen(
        (command + " '" + path + "' 2>" + errors).c_str(), "r");

    if (pipe == nullptr) {
        reason = "could not run " + tool;
        return false;
    }

    std::string output;
    char buffer[65536];
    std::size_t read = 0;

    while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, read);
    }

    const int status = pclose(pipe);

    if (status != 0 || output.empty()) {
        reason = tool + " failed";

        std::ifstream why(errors);

        if (why) {
            std::string line;

            if (std::getline(why, line) && !line.empty()) {
                reason += ": " + line;
            }
        }

        return false;
    }

    contents = std::move(output);

    return true;
}

}
