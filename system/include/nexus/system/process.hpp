#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nexus::system {

struct ProcessResult {
    bool ok = false;
    int exitCode = 0;

    std::vector<std::string> lines;
    std::string text;

    bool ran = false;

    // runProgram only (0.2.1). What the program wrote to its error
    // output, when that was asked for on its own (Errors::Separate).
    std::string errors;
    // The time limit ran out and the program was stopped.
    bool timedOut = false;
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
// The program keeps this process's own language settings (use the
// options below for the plain locale).
ProcessResult runProgram(const std::vector<std::string>& argv, const std::string& input = "");

// How runProgram runs a program, for everything a command line handed
// to /bin/sh used to say with `2>&1`, `2>/dev/null`, `> /dev/null`,
// `VAR=value command` and `timeout N` (0.2.1, ledger item 4).
//
// Why: a command built as one string is parsed by a shell, and one
// value joined in without shellQuote is a command of somebody else's
// choosing, run as root. With an argument vector there is nothing to
// parse: each value is one argument, whatever it holds.
//
// The defaults are what runCommand(command, false) did with
// "2>/dev/null" at the end, the most common call: stdin shared with
// this process, output collected, errors dropped, the plain C locale.
struct ProgramOptions {
    // Written to the program's stdin through a pipe; the program sees
    // the end of its input after it. Not set: the program shares this
    // process's stdin, as one run through the shell did (apt and dpkg
    // ask their questions there).
    std::optional<std::string> input;

    // Where what the program prints goes.
    enum class Output {
        Capture,   // into ProcessResult::text (and ::lines)
        Discard,   // nowhere                                  "> /dev/null"
        Inherit    // this process's stdout, as it is written: for work
                   // whose progress a person watches (rpm-ostree upgrade)
    };
    Output output = Output::Capture;

    // Where its error output goes.
    enum class Errors {
        Discard,   // nowhere                                  "2>/dev/null"
        Merge,     // with the output, in the order written    "2>&1"
        Separate,  // into ProcessResult::errors
        Inherit    // this process's stderr: the person sees it
    };
    Errors errors = Errors::Discard;

    // LC_ALL=C.UTF-8 and an empty LANGUAGE for the program, as
    // runCommand always set them (inPlainLocale says why).
    bool plainLocale = true;

    // Variables for the program on top of that, replacing any of the
    // same name: {"LC_ALL", "sr_RS.UTF-8"}.
    std::vector<std::pair<std::string, std::string>> environment;

    // Seconds the program may take; 0 for as long as it needs. When
    // they are up it is sent SIGTERM, and SIGKILL two seconds later if
    // it is still there. ProcessResult::timedOut is then set and the
    // exit code is 124, the number timeout(1) gives. Only the program
    // itself is stopped, not what it started.
    int timeoutSeconds = 0;

    // ProcessResult::lines is filled (runCommand's splitLines).
    bool splitLines = true;

    // While the program runs, Ctrl-C and Ctrl-\ are the program's and
    // not this process's, the way system(3) arranges it: for work
    // handed over in the foreground, so that nexus is still there
    // afterwards to write down how it ended.
    bool waitThroughInterrupts = false;
};

// As above, with options. Never 0 for a program that did not run:
// exit code 127 when it could not be found, 126 when it was found and
// could not be started -- the numbers a shell gives -- and `ran` false.
// With Errors::Merge the reason is put in `text`, with Separate in
// `errors`, where the shell's own "not found" line used to be.
ProcessResult runProgram(const std::vector<std::string>& argv, const ProgramOptions& options);

// The usual sets of options, named for what the command line handed
// to a shell used to end in (0.2.1). A plain ProgramOptions() is
// "2>/dev/null": output collected, errors dropped.
ProgramOptions mergedErrors();   // "2>&1": errors collected with the output
ProgramOptions shownErrors();    // nothing: errors go to the person's terminal
ProgramOptions nothingKept();    // "> /dev/null 2>&1": only the exit code matters
// Not collected at all: the program writes to the terminal itself, and
// Ctrl-C is its own while it runs. What std::system(command) did.
// plainLocale: whether the command was wrapped in inPlainLocale.
ProgramOptions handedOver(bool plainLocale);

// The first `count` lines of a text, each with its line end: what
// `| head -N` kept (0.2.1: pipes are done here, not by a shell).
std::string firstLines(const std::string& text, std::size_t count);

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
