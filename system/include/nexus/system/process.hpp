#pragma once

#include <string>
#include <vector>

namespace nexus::system {

struct ProcessResult {
    bool ok = false;
    int exitCode = 0;

    std::vector<std::string> lines;
    std::string text;

    bool ran = false;
};

// Run a command and read all of its output.
//
// All of it. fgets returns null at end of input and also when a read
// is interrupted by a signal, and treating the second as the first
// stops reading early -- on a clean line boundary, so the output
// looks complete. Asking a container to list a package's files
// produced 39,158 lines and this read 1,457 of them, silently, and
// the missing ones happened to be the ones being looked for.
//
// Every reader here shares this so the mistake cannot be made once
// per call site.
//
// splitLines is for readers that want the whole output as one string.
// The lines hold a second copy of every byte already in text, plus a
// string object and an allocation for each line. On a decompressed
// package index that is hundreds of megabytes of content and hundreds
// more of overhead, built for a vector the caller never reads.
ProcessResult runCommand(
    const std::string& command,
    bool captureErrors = true,
    bool splitLines = true
);

// Whether a command exists at all.
bool commandExists(const std::string& name);

}
