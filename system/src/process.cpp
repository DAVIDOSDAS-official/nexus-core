#include <nexus/system/process.hpp>

#include <filesystem>
#include <vector>
#include <unistd.h>
#include <spawn.h>
#include <fcntl.h>
#include <poll.h>
#include <csignal>
#include <sstream>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>
#include <sys/stat.h>
#include <sys/wait.h>

extern char** environ;

namespace nexus::system {

std::string shellQuote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }
    return out + "'";
}

namespace {

// The length of an invisible or direction-changing character starting
// at text[i], or 0. These print as nothing, or turn the text after them
// around, so a name can look like another ("Trojan Source"):
//   U+00AD            soft hyphen                 C2 AD
//   U+061C            Arabic letter mark          D8 9C
//   U+115F, U+1160    Hangul fillers              E1 85 9F..A0
//   U+180E            Mongolian vowel separator   E1 A0 8E
//   U+200B..U+200F    zero-width, LRM, RLM        E2 80 8B..8F
//   U+2028, U+2029    line and paragraph sep.     E2 80 A8..A9
//   U+202A..U+202E    embeddings and overrides    E2 80 AA..AE
//   U+2060..U+2064    word joiner, invisible ops  E2 81 A0..A4
//   U+2066..U+2069    isolates                    E2 81 A6..A9
//   U+3164            Hangul filler               E3 85 A4
//   U+FEFF            zero-width no-break space   EF BB BF
//   U+FFA0            halfwidth Hangul filler     EF BE A0
//   U+E0000..U+E007F  tag characters              F3 A0 80 80..F3 A0 81 BF
// (0.1.99: the soft hyphen, separators, fillers, U+180E and the tag
// characters were round-4 audit L3.) U+FE0F, the emoji variation
// selector, stays: it changes how an emoji looks, not what text says.
std::size_t invisibleAt(const std::string& text, std::size_t i) {
    auto at = [&](std::size_t k) -> unsigned {
        return k < text.size() ? static_cast<unsigned char>(text[k]) : 0u;
    };
    const unsigned a = at(i), b = at(i + 1), c = at(i + 2), d = at(i + 3);
    if (a == 0xC2 && b == 0xAD) return 2;
    if (a == 0xD8 && b == 0x9C) return 2;
    if (a == 0xE1 && b == 0x85 && (c == 0x9F || c == 0xA0)) return 3;
    if (a == 0xE1 && b == 0xA0 && c == 0x8E) return 3;
    if (a == 0xE3 && b == 0x85 && c == 0xA4) return 3;
    if (a == 0xEF && b == 0xBE && c == 0xA0) return 3;
    if (a == 0xF3 && b == 0xA0 && (c == 0x80 || c == 0x81) && d >= 0x80 && d <= 0xBF) return 4;
    if (a == 0xE2 && b == 0x80 && ((c >= 0x8B && c <= 0x8F) || (c >= 0xA8 && c <= 0xAE))) return 3;
    if (a == 0xE2 && b == 0x81 && ((c >= 0xA0 && c <= 0xA4) || (c >= 0xA6 && c <= 0xA9))) return 3;
    if (a == 0xEF && b == 0xBB && c == 0xBF) return 3;
    return 0;
}

}  // namespace

std::string printable(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F) {
            out += '?';
        } else if (c == 0xC2 && i + 1 < text.size() &&
                   static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
                   static_cast<unsigned char>(text[i + 1]) <= 0x9F) {
            out += '?';   // U+0080..U+009F: C1 controls (CSI, OSC as one byte)
            ++i;
        } else if (const std::size_t n = invisibleAt(text, i); n > 0) {
            out += '?';   // direction overrides, zero-width (0.1.86, R2)
            i += n - 1;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool isPackageName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name[0] == '-') return false;
    for (unsigned char c : name) {
        const bool fine = std::isalnum(c) || c == '.' || c == '_' || c == '+' ||
                          c == '-' || c == ':' || c == '@' || c == '~';
        if (!fine) return false;
    }
    return true;
}

int exitCodeFromStatus(int status) {
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

namespace {

// A pipe with neither end on descriptor 0, 1 or 2. A process started
// with one of those closed gets it back from the next pipe(), and
// handing that end to a child as its stdin or stdout would then mean
// moving a descriptor onto itself (0.2.1).
bool pipeAboveStandard(int fds[2]) {
    if (::pipe2(fds, O_CLOEXEC) != 0) return false;
    for (int i = 0; i < 2; ++i) {
        if (fds[i] > 2) continue;
        const int moved = ::fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
        if (moved < 0) {
            ::close(fds[0]);
            ::close(fds[1]);
            return false;
        }
        ::close(fds[i]);
        fds[i] = moved;
    }
    return true;
}

void closeBoth(int fds[2]) {
    if (fds[0] >= 0) ::close(fds[0]);
    if (fds[1] >= 0) ::close(fds[1]);
    fds[0] = fds[1] = -1;
}

// This process's environment with the options applied: the plain
// locale first, then the caller's own variables over it.
std::vector<std::string> environmentFor(const ProgramOptions& options) {
    std::vector<std::pair<std::string, std::string>> set;
    if (options.plainLocale) {
        // What inPlainLocale exports: LANGUAGE is cleared, not removed,
        // because gettext consults it before LC_ALL.
        set.emplace_back("LC_ALL", "C.UTF-8");
        set.emplace_back("LANGUAGE", "");
    }
    for (const auto& [name, value] : options.environment) {
        // Not a variable's name: left out rather than passed on as
        // something else ("A=B" as a name would set A).
        if (name.empty() || name.find('=') != std::string::npos ||
            name.find('\0') != std::string::npos) {
            continue;
        }
        set.emplace_back(name, value);
    }

    std::vector<std::string> out;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string text = *entry;
        const std::string name = text.substr(0, text.find('='));
        bool replaced = false;
        for (const auto& [wanted, value] : set) {
            replaced = replaced || wanted == name;
        }
        if (!replaced) out.push_back(text);
    }
    // The last setting of a name wins.
    for (std::size_t i = 0; i < set.size(); ++i) {
        bool later = false;
        for (std::size_t k = i + 1; k < set.size(); ++k) {
            later = later || set[k].first == set[i].first;
        }
        if (!later) out.push_back(set[i].first + "=" + set[i].second);
    }
    return out;
}

// Whether a file of that name is there at all: at the path given, or
// in one of PATH's folders. posix_spawnp answers "permission denied"
// when a folder in PATH cannot be searched, also when the program is
// in none of them; a shell says "not found" then, and so does
// runProgram.
bool programIsThere(const std::string& name) {
    struct stat info {};
    if (name.find('/') != std::string::npos) {
        return ::stat(name.c_str(), &info) == 0;
    }
    const char* path = std::getenv("PATH");
    // What the C library searches when PATH is not set.
    std::istringstream folders(path != nullptr ? path : "/bin:/usr/bin");
    std::string folder;
    while (std::getline(folders, folder, ':')) {
        const std::string file = (folder.empty() ? std::string(".") : folder) + "/" + name;
        if (::stat(file.c_str(), &info) == 0 && !S_ISDIR(info.st_mode)) {
            return true;
        }
    }
    return false;
}

long long nowMilliseconds() {
    struct timespec now {};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

// One line per '\n'; a last line without one counts too.
void splitInto(const std::string& text, std::vector<std::string>& lines) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
}

}  // namespace

ProgramOptions mergedErrors() {
    ProgramOptions options;
    options.errors = ProgramOptions::Errors::Merge;
    return options;
}

ProgramOptions shownErrors() {
    ProgramOptions options;
    options.errors = ProgramOptions::Errors::Inherit;
    return options;
}

ProgramOptions nothingKept() {
    ProgramOptions options;
    options.output = ProgramOptions::Output::Discard;
    options.errors = ProgramOptions::Errors::Discard;
    return options;
}

ProgramOptions handedOver(bool plainLocale) {
    ProgramOptions options;
    options.output = ProgramOptions::Output::Inherit;
    options.errors = ProgramOptions::Errors::Inherit;
    options.plainLocale = plainLocale;
    options.waitThroughInterrupts = true;
    options.splitLines = false;
    return options;
}

std::string firstLines(const std::string& text, std::size_t count) {
    std::size_t end = 0;
    for (std::size_t line = 0; line < count && end < text.size(); ++line) {
        const std::size_t newline = text.find('\n', end);
        end = newline == std::string::npos ? text.size() : newline + 1;
    }
    return text.substr(0, end);
}

ProcessResult runProgram(const std::vector<std::string>& argv, const std::string& input) {
    // As it always was: the input through a pipe (an empty one too),
    // the output collected, errors dropped, this process's own locale.
    ProgramOptions options;
    options.input = input;
    options.plainLocale = false;
    return runProgram(argv, options);
}

ProcessResult runProgram(const std::vector<std::string>& argv, const ProgramOptions& options) {
    using Output = ProgramOptions::Output;
    using Errors = ProgramOptions::Errors;

    ProcessResult result;
    // Not run is never "exit 0" (0.2.1): callers read exitCode.
    result.exitCode = 127;
    if (argv.empty()) return result;

    const bool feed = options.input.has_value();
    const bool capture = options.output == Output::Capture;
    const bool separate = options.errors == Errors::Separate;

    int toChild[2] = {-1, -1}, fromChild[2] = {-1, -1}, errorsFromChild[2] = {-1, -1};
    if ((feed && !pipeAboveStandard(toChild)) ||
        (capture && !pipeAboveStandard(fromChild)) ||
        (separate && !pipeAboveStandard(errorsFromChild))) {
        closeBoth(toChild);
        closeBoth(fromChild);
        closeBoth(errorsFromChild);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (feed) posix_spawn_file_actions_adddup2(&actions, toChild[0], 0);
    if (capture) {
        posix_spawn_file_actions_adddup2(&actions, fromChild[1], 1);
    } else if (options.output == Output::Discard) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    }
    switch (options.errors) {
        case Errors::Discard:
            posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
            break;
        case Errors::Merge:
            // Wherever the output goes by now: the pipe, /dev/null or
            // this process's own stdout.
            posix_spawn_file_actions_adddup2(&actions, 1, 2);
            break;
        case Errors::Separate:
            posix_spawn_file_actions_adddup2(&actions, errorsFromChild[1], 2);
            break;
        case Errors::Inherit:
            break;
    }

    // Ctrl-C while the program has the foreground: ignored here, as
    // system(3) does, so this process outlives the program and can say
    // how it ended. The program gets the signals back as they were --
    // unless they were already ignored when nexus started (nohup, a
    // background job), which stays the caller's choice.
    struct sigaction ignore {}, savedInt {}, savedQuit {};
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    if (options.waitThroughInterrupts) {
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        ::sigaction(SIGINT, &ignore, &savedInt);
        ::sigaction(SIGQUIT, &ignore, &savedQuit);
        sigset_t standard;
        sigemptyset(&standard);
        if (savedInt.sa_handler != SIG_IGN) sigaddset(&standard, SIGINT);
        if (savedQuit.sa_handler != SIG_IGN) sigaddset(&standard, SIGQUIT);
        posix_spawnattr_setsigdefault(&attributes, &standard);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF);
    }
    const auto restoreInterrupts = [&] {
        if (!options.waitThroughInterrupts) return;
        ::sigaction(SIGINT, &savedInt, nullptr);
        ::sigaction(SIGQUIT, &savedQuit, nullptr);
    };

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    const std::vector<std::string> variables = environmentFor(options);
    std::vector<char*> envp;
    for (const auto& v : variables) envp.push_back(const_cast<char*>(v.c_str()));
    envp.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = ::posix_spawnp(&pid, argv[0].c_str(), &actions, &attributes,
                                       args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (toChild[0] >= 0) ::close(toChild[0]);
    if (fromChild[1] >= 0) ::close(fromChild[1]);
    if (errorsFromChild[1] >= 0) ::close(errorsFromChild[1]);
    if (spawned != 0) {
        if (toChild[1] >= 0) ::close(toChild[1]);
        if (fromChild[0] >= 0) ::close(fromChild[0]);
        if (errorsFromChild[0] >= 0) ::close(errorsFromChild[0]);
        restoreInterrupts();
        // What a shell would have answered with: 127 for a program
        // that is not there, 126 for one that cannot be started.
        const bool missing = spawned == ENOENT || spawned == ENOTDIR ||
                             (spawned == EACCES && !programIsThere(argv[0]));
        result.exitCode = missing ? 127 : 126;
        const std::string why = argv[0] +
            (missing ? ": no such program\n"
                     : std::string(": could not be started (") + std::strerror(spawned) + ")\n");
        if (options.errors == Errors::Merge && capture) {
            result.text = why;
        } else if (separate) {
            result.errors = why;
        } else if (options.errors == Errors::Inherit ||
                   (options.errors == Errors::Merge && options.output == Output::Inherit)) {
            // Where the shell's "not found" went: in front of the person.
            [[maybe_unused]] const ssize_t ignored = ::write(2, why.data(), why.size());
        }
        if (options.splitLines) splitInto(result.text, result.lines);
        return result;
    }
    result.ran = true;

    // Written and read at the same time (0.1.87, audit). Before, the
    // whole input was written first and the output read afterwards: a
    // program that answers as it reads (anything like `cat`) fills its
    // output pipe at 64 KiB, stops reading, and both sides wait for
    // ever. Keys are 45 bytes and never got there; nothing should have
    // to know that limit.
    //
    // A child that exits without reading would kill this process with
    // SIGPIPE on the next write. The signal is blocked for this thread
    // while writing and taken off the queue afterwards if a write did
    // raise it -- the process-wide handler is never touched, so another
    // thread, or a caller's own handler, sees no change (before: the
    // handler was swapped to "ignore" and back, for everyone).
    sigset_t pipeSignal, previousMask, pendingBefore;
    sigemptyset(&pipeSignal);
    sigaddset(&pipeSignal, SIGPIPE);
    sigemptyset(&pendingBefore);
    ::sigpending(&pendingBefore);
    const bool wasPending = sigismember(&pendingBefore, SIGPIPE) == 1;
    ::pthread_sigmask(SIG_BLOCK, &pipeSignal, &previousMask);

    int writeEnd = toChild[1];
    int readEnd = fromChild[0];
    int errorEnd = errorsFromChild[0];
    for (const int fd : {writeEnd, readEnd, errorEnd}) {
        if (fd >= 0) ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    }

    const std::string empty;
    const std::string& input = feed ? *options.input : empty;
    std::size_t done = 0;
    bool brokenPipe = false;
    if (writeEnd >= 0 && input.empty()) {
        ::close(writeEnd);   // the child sees the end of its input at once
        writeEnd = -1;
    }

    // The time limit: SIGTERM when it is up, SIGKILL two seconds on,
    // and one second more for what the program left in its pipes.
    const long long limit = options.timeoutSeconds > 0
        ? nowMilliseconds() + static_cast<long long>(options.timeoutSeconds) * 1000 : -1;
    long long killAt = -1;
    long long giveUpAt = -1;
    // Returns how long to wait at most, in milliseconds (-1: for ever),
    // and sends the signals that are due.
    const auto timeLeft = [&]() -> int {
        if (limit < 0) return -1;
        const long long now = nowMilliseconds();
        if (!result.timedOut) {
            if (now < limit) return static_cast<int>(limit - now);
            result.timedOut = true;
            ::kill(pid, SIGTERM);
            killAt = now + 2000;
        }
        if (giveUpAt < 0) {
            if (now < killAt) return static_cast<int>(killAt - now);
            ::kill(pid, SIGKILL);
            giveUpAt = now + 1000;
        }
        return now < giveUpAt ? static_cast<int>(giveUpAt - now) : 0;
    };

    char buffer[65536];
    while (readEnd >= 0 || errorEnd >= 0 || writeEnd >= 0) {
        const int wait = timeLeft();
        if (giveUpAt >= 0 && wait == 0) break;   // killed; something else holds the pipes
        struct pollfd fds[3];
        fds[0].fd = readEnd;    // -1 once closed: ignored by poll
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = writeEnd;
        fds[1].events = POLLOUT;
        fds[1].revents = 0;
        fds[2].fd = errorEnd;
        fds[2].events = POLLIN;
        fds[2].revents = 0;
        const int ready = ::poll(fds, 3, wait);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;   // the time limit: timeLeft acts on it
        if (writeEnd >= 0 && (fds[1].revents & (POLLOUT | POLLERR | POLLHUP)) != 0) {
            const ssize_t w = ::write(writeEnd, input.data() + done, input.size() - done);
            if (w > 0) done += static_cast<std::size_t>(w);
            const bool failed = w < 0 && errno != EINTR && errno != EAGAIN;
            if (failed && errno == EPIPE) brokenPipe = true;
            if (failed || done >= input.size()) {
                ::close(writeEnd);   // the child sees the end of its input
                writeEnd = -1;
            }
        }
        if (readEnd >= 0 && (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            const ssize_t n = ::read(readEnd, buffer, sizeof buffer);
            if (n > 0) {
                result.text.append(buffer, static_cast<std::size_t>(n));
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                ::close(readEnd);
                readEnd = -1;
            }
        }
        if (errorEnd >= 0 && (fds[2].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            const ssize_t n = ::read(errorEnd, buffer, sizeof buffer);
            if (n > 0) {
                result.errors.append(buffer, static_cast<std::size_t>(n));
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                ::close(errorEnd);
                errorEnd = -1;
            }
        }
    }
    if (writeEnd >= 0) ::close(writeEnd);
    if (readEnd >= 0) ::close(readEnd);
    if (errorEnd >= 0) ::close(errorEnd);

    if (brokenPipe && !wasPending) {
        // Ours: take it off the queue before the mask goes back.
        const struct timespec now {0, 0};
        while (::sigtimedwait(&pipeSignal, nullptr, &now) < 0 && errno == EINTR) {}
    }
    ::pthread_sigmask(SIG_SETMASK, &previousMask, nullptr);

    // How it ended -- or that this could not be found out (0.1.88,
    // review of patch 113). waitpid can fail for reasons other than a
    // signal arriving: with SIGCHLD ignored the kernel reaps children
    // itself and waitpid says ECHILD. 0.1.87 looped only on EINTR and
    // then read `status`, still 0 from its initialisation, as "exit
    // 0": a program whose ending nobody saw was called a success.
    int status = 0;
    pid_t waited = -1;
    if (limit < 0) {
        do {
            waited = ::waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
    } else {
        // With a time limit the wait is in short steps, so the signals
        // still go out on time when the program prints nothing.
        while (true) {
            waited = ::waitpid(pid, &status, WNOHANG);
            if (waited == pid || (waited < 0 && errno != EINTR)) break;
            const int wait = timeLeft();
            if (giveUpAt >= 0 && wait == 0) {
                // Killed and still not collected: wait for it for good;
                // SIGKILL cannot be refused.
                do {
                    waited = ::waitpid(pid, &status, 0);
                } while (waited < 0 && errno == EINTR);
                break;
            }
            ::poll(nullptr, 0, wait < 0 || wait > 20 ? 20 : wait);
        }
    }
    restoreInterrupts();
    result.exitCode = waited == pid ? exitCodeFromStatus(status) : -1;
    if (result.timedOut && waited == pid) result.exitCode = 124;
    result.ok = result.exitCode == 0;
    if (options.splitLines) splitInto(result.text, result.lines);
    return result;
}

namespace {

volatile std::sig_atomic_t heldSignal = 0;

void rememberSignal(int number) {
    heldSignal = number;
}

// SIGQUIT (Ctrl-\) with the others, as system(3) does for the program
// that is waiting (0.1.88).
const int heldSignals[] = {SIGINT, SIGHUP, SIGTERM, SIGQUIT};
const int heldCount = sizeof heldSignals / sizeof *heldSignals;

}  // namespace

struct HoldSignals::Saved {
    struct sigaction previous[heldCount];
};

HoldSignals::HoldSignals() : saved_(new Saved) {
    heldSignal = 0;
    struct sigaction action {};
    action.sa_handler = rememberSignal;
    sigemptyset(&action.sa_mask);
    // Restarted, so a read or a wait in progress is not cut short.
    action.sa_flags = SA_RESTART;
    for (int i = 0; i < heldCount; ++i) {
        ::sigaction(heldSignals[i], nullptr, &saved_->previous[i]);
        // One that was already ignored when nexus started (nohup, a
        // background job) stays ignored, for this process and for what
        // it starts: that was the caller's choice.
        if (saved_->previous[i].sa_handler == SIG_IGN) continue;
        ::sigaction(heldSignals[i], &action, nullptr);
    }
}

HoldSignals::~HoldSignals() {
    for (int i = 0; i < heldCount; ++i) {
        ::sigaction(heldSignals[i], &saved_->previous[i], nullptr);
    }
    delete saved_;
}

int HoldSignals::arrived() const {
    return heldSignal;
}

void endBySignal(int number) {
    // The default action, not blocked, then the signal itself: the
    // process ends as if it had never been held, and whoever waits for
    // it sees "ended by signal". If that somehow returns, the exit code
    // a shell would report for it.
    struct sigaction standard {};
    standard.sa_handler = SIG_DFL;
    sigemptyset(&standard.sa_mask);
    ::sigaction(number, &standard, nullptr);
    sigset_t one;
    sigemptyset(&one);
    sigaddset(&one, number);
    ::pthread_sigmask(SIG_UNBLOCK, &one, nullptr);
    ::raise(number);
    ::_exit(128 + number);
}

ScratchDirectory::ScratchDirectory() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base && *base == '/' ? base : "/tmp") + "/nexus-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (::mkdtemp(buffer.data()) != nullptr) path_ = buffer.data();
}

ScratchDirectory::~ScratchDirectory() {
    if (path_.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

std::string ScratchDirectory::file(const std::string& name) const {
    return path_.empty() ? "/dev/null" : path_ + "/" + name;
}

bool commandExists(const std::string& name) {
    return std::system(
        ("command -v " + shellQuote(name) + " > /dev/null 2>&1").c_str()) == 0;
}

std::string inPlainLocale(const std::string& command) {
    return "LC_ALL=C.UTF-8; LANGUAGE=; export LC_ALL LANGUAGE; " + command;
}

ProcessResult runCommand(
    const std::string& command,
    bool captureErrors,
    bool splitLines
) {
    ProcessResult result;

    const std::string full = inPlainLocale(
        captureErrors ? command + " 2>&1" : command);

    std::FILE* pipe = popen(full.c_str(), "r");

    if (pipe == nullptr) {
        return result;
    }

    result.ran = true;

    char buffer[65536];

    while (true) {
        errno = 0;

        if (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result.text += buffer;
            continue;
        }

        // Null means one of three things, and only one of them is the
        // end. A signal during the read sets EINTR and leaves more to
        // come; stopping there loses the rest.
        if (std::feof(pipe)) {
            break;
        }

        if (std::ferror(pipe) && errno == EINTR) {
            clearerr(pipe);
            continue;
        }

        break;
    }

    const int status = pclose(pipe);

    result.exitCode = exitCodeFromStatus(status);
    result.ok = result.exitCode == 0;

    // Split once, at the end, rather than line by line while reading.
    //
    // Not always, though. Every byte is already in text; the lines are
    // a second copy of all of it, in a string object per line, each
    // with its own allocation. A caller reading one whole document
    // pays for a vector it never looks at, and on a package index that
    // is the difference between running and being killed.
    if (!splitLines) {
        return result;
    }

    std::string current;

    for (char character : result.text) {
        if (character == '\n') {
            result.lines.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    if (!current.empty()) {
        result.lines.push_back(current);
    }

    return result;
}

}
