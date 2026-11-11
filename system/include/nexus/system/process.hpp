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

// One argument for /bin/sh, whatever it contains: wrapped in single
// quotes, each ' written as '\''. Every value that is not a fixed
// part of a command goes through this -- a package name, a path a
// package owns, a locale. (0.1.76, after an outside review: names
// were hand-wrapped in '...', and a ' in one ended the quoting.)
std::string shellQuote(const std::string& text);

// A package name as dnf, apt, pacman and flatpak write them: letters,
// digits and . _ + - : @ ~, not starting with - (which a tool would
// read as an option), 1 to 255 characters. Checked where a name comes
// in from the command line, before it is used anywhere.
bool isPackageName(const std::string& name);

// Text from outside Nexus (another disk's os-release, Steam, ProtonDB,
// a repository) made safe to print: every control character -- C0
// including newline and tab, DEL, and the C1 range in UTF-8 -- becomes
// '?'. A terminal then cannot be told to hide lines, retitle itself or
// write the clipboard (0.1.85, outside review N3). Ordinary UTF-8
// letters pass unchanged.
std::string printable(const std::string& text);

// Runs a program directly -- no /bin/sh, so nothing is parsed and only
// argv is visible in /proc -- with `input` written to its stdin and its
// stdout (not stderr) returned. For secrets: they go through `input`,
// never argv (0.1.84, outside review: `printf '<key>' | wg pubkey` put
// the WireGuard private key in sh's arguments, which every local user
// can read). argv[0] is looked up in PATH only if it has no '/'.
ProcessResult runProgram(const std::vector<std::string>& argv, const std::string& input = "");

// A scratch folder only this process can enter: mkdtemp, mode 0700,
// owned by whoever runs it -- root under sudo. A file inside it cannot
// be planted beforehand by anyone else, as a symlink or otherwise.
// Removed with everything in it when this goes out of scope.
//
// 0.1.78, after a third outside review: child stderr went to
// /tmp/nexus-...-<pid>, a name anyone could guess and pre-create as a
// symlink that a run under sudo would follow. (Fedora's
// fs.protected_symlinks=1 already blocks that; this does not rely on it.)
class ScratchDirectory {
public:
    ScratchDirectory();
    ~ScratchDirectory();
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    bool ok() const { return !path_.empty(); }
    // A path for a file in the folder, or /dev/null if there is no
    // folder -- never a guessable name in /tmp.
    std::string file(const std::string& name) const;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}
