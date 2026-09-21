#include <nexus/system/compression.hpp>

#include <nexus/system/process.hpp>

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

    if (!commandExists(tool)) {
        reason = "needs '" + tool + "', which is not installed";
        return false;
    }

    // Keep stderr. Discarding it turns every decompression failure
    // into "produced no output", which says nothing about why.
    // Process-specific: a fixed name in a sticky /tmp cannot be
    // rewritten by a different user.
    const std::string errors =
        "/tmp/nexus-decompress-" + std::to_string(::getpid());

    // Not split into lines: this is one document, and the caller
    // wants it whole. Splitting it held every byte twice.
    ProcessResult ran = runCommand(
        command + " '" + path + "' 2>" + errors, false, false);

    if (!ran.ran) {
        reason = "could not run " + tool;
        return false;
    }

    if (ran.exitCode != 0 || ran.text.empty()) {
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

    // Moved, and this time it is a move. std::move on a const
    // reference yields a const rvalue, which cannot bind to move
    // assignment and quietly binds to the copy instead -- so the line
    // that said "move" was holding a third copy of the document.
    contents = std::move(ran.text);

    return true;
}

}
