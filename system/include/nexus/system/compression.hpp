#pragma once

#include <string>

namespace nexus::system {

// Package metadata arrives compressed, in whichever format the tool
// that wrote it preferred. apt uses lz4, Fedora uses zstd for small
// repositories and zchunk for large ones, and older repositories use
// gzip or xz.
//
// The command to read one, or an empty string when the file can be
// opened directly. Note that these are not interchangeable: unzck
// takes -c where everything else takes -dc, which is the kind of
// detail that turns into a reader that works on a six-package
// repository and fails on a seventy-thousand-package one.
std::string decompressCommand(const std::string& path);

// The tool a command needs, for reporting when it is missing.
std::string decompressTool(const std::string& path);

// Read a file, decompressing if needed.
//
// Returns false and fills reason when it cannot -- a missing
// decompressor is reported by name rather than silently producing an
// empty repository.
bool readPossiblyCompressed(
    const std::string& path,
    std::string& contents,
    std::string& reason
);

}
