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

// The command, run in the plain C locale.
//
// Nexus reads what other tools print -- dnf's transaction table, apt's
// "Inst" lines, rpm-ostree's status -- and those are translated. On a
// machine installed in Serbian, dnf's table came back in Cyrillic, the
// parser found no packages in it, and every plan looked like a
// refusal: the first boot of that machine could install nothing
// (26 September). The tools' words are for people; to be read by a
// program they have to be in the one language it was written for.
//
// An exported assignment rather than a prefix, so it covers every
// command in a pipeline or a sequence, not only the first. LANGUAGE is
// cleared because gettext consults it before LC_ALL.
std::string inPlainLocale(const std::string& command);

// Whether a command exists at all.
bool commandExists(const std::string& name);

}
