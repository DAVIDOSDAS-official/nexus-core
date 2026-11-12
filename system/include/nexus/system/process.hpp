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
// write the clipboard (0.1.85, outside review N3). Characters that
// print as nothing or reverse the text after them (zero-width spaces,
// direction overrides and isolates) become '?' too, so a name cannot
// pass for another (0.1.86, outside review R2). Ordinary UTF-8 letters,
// including right-to-left scripts themselves, pass unchanged.
std::string printable(const std::string& text);

// Runs a program directly -- no /bin/sh, so nothing is parsed and only
// argv is visible in /proc -- with `input` written to its stdin and its
// stdout (not stderr) returned. For secrets: they go through `input`,
// never argv (0.1.84, outside review: `printf '<key>' | wg pubkey` put
// the WireGuard private key in sh's arguments, which every local user
// can read). argv[0] is looked up in PATH only if it has no '/'.
// Input and output of any size: both are moved at the same time.
ProcessResult runProgram(const std::vector<std::string>& argv, const std::string& input = "");

// What a wait status means as one number: the program's exit code;
// 128 + the signal's number if a signal ended it (as a shell reports
// it, so 137 is "killed"); -1 if the status could not be read. Never 0
// for a program that was killed (0.1.87, audit: runCommand read the
// exit-code bits of a killed program, which are 0, and called a tar or
// an rpm-ostree that the kernel had killed for memory a success).
// runCommand and runProgram also report -1, never 0, when the ending
// could not be collected at all (0.1.88).
int exitCodeFromStatus(int status);

// While one of these exists, Ctrl-C, a closed terminal and a polite
// kill (SIGINT, SIGHUP, SIGTERM) do not end this process: they are
// remembered, and arrived() says which came. For the few stretches
// where stopping halfway is worse than finishing -- a server stopped
// for its backup has to be started again whatever happens to the copy
// (0.1.87, audit: Ctrl-C during `nexus server backup` left the server
// down and the record at "started").
//
// Programs started meanwhile are not shielded: a handler, unlike
// "ignore", does not survive exec, so the tar the terminal's Ctrl-C
// also reaches still stops, and its failure is what the caller sees.
// SIGQUIT (Ctrl-\) is held with them (0.1.88).
// One at a time; not for use from more than one thread.
class HoldSignals {
public:
    HoldSignals();
    ~HoldSignals();
    HoldSignals(const HoldSignals&) = delete;
    HoldSignals& operator=(const HoldSignals&) = delete;

    // The signal that arrived while held (the last, if several), or 0.
    int arrived() const;

private:
    struct Saved;
    Saved* saved_;
};

// Ends this process by the given signal, the way it would have ended
// had the signal not been held: default action restored, then raised.
// For after the held stretch is finished and cleaned up (0.1.88). A
// shell that ran `nexus` in a loop goes on to the next round if nexus
// merely exits 1 after Ctrl-C; it stops, as the person asked, only if
// nexus is seen to have ended by the signal.
[[noreturn]] void endBySignal(int number);

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
