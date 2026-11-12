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
//   U+061C            Arabic letter mark          D8 9C
//   U+200B..U+200F    zero-width, LRM, RLM        E2 80 8B..8F
//   U+202A..U+202E    embeddings and overrides    E2 80 AA..AE
//   U+2060..U+2064    word joiner, invisible ops  E2 81 A0..A4
//   U+2066..U+2069    isolates                    E2 81 A6..A9
//   U+FEFF            zero-width no-break space   EF BB BF
std::size_t invisibleAt(const std::string& text, std::size_t i) {
    auto at = [&](std::size_t k) -> unsigned {
        return k < text.size() ? static_cast<unsigned char>(text[k]) : 0u;
    };
    const unsigned a = at(i), b = at(i + 1), c = at(i + 2);
    if (a == 0xD8 && b == 0x9C) return 2;
    if (a == 0xE2 && b == 0x80 && ((c >= 0x8B && c <= 0x8F) || (c >= 0xAA && c <= 0xAE))) return 3;
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

ProcessResult runProgram(const std::vector<std::string>& argv, const std::string& input) {
    ProcessResult result;
    if (argv.empty()) return result;

    int toChild[2], fromChild[2];
    if (::pipe2(toChild, O_CLOEXEC) != 0) return result;
    if (::pipe2(fromChild, O_CLOEXEC) != 0) {
        ::close(toChild[0]); ::close(toChild[1]);
        return result;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, toChild[0], 0);
    posix_spawn_file_actions_adddup2(&actions, fromChild[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = ::posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(toChild[0]);
    ::close(fromChild[1]);
    if (spawned != 0) {
        ::close(toChild[1]);
        ::close(fromChild[0]);
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

    ::fcntl(toChild[1], F_SETFL, ::fcntl(toChild[1], F_GETFL) | O_NONBLOCK);
    ::fcntl(fromChild[0], F_SETFL, ::fcntl(fromChild[0], F_GETFL) | O_NONBLOCK);

    int writeEnd = toChild[1];
    int readEnd = fromChild[0];
    std::size_t done = 0;
    bool brokenPipe = false;
    if (input.empty()) {
        ::close(writeEnd);
        writeEnd = -1;
    }
    char buffer[65536];
    while (readEnd >= 0) {
        struct pollfd fds[2];
        fds[0].fd = readEnd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = writeEnd;   // -1 once the input is written: ignored
        fds[1].events = POLLOUT;
        fds[1].revents = 0;
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
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
        if ((fds[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            const ssize_t n = ::read(readEnd, buffer, sizeof buffer);
            if (n > 0) {
                result.text.append(buffer, static_cast<std::size_t>(n));
            } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                ::close(readEnd);
                readEnd = -1;
            }
        }
    }
    if (writeEnd >= 0) ::close(writeEnd);
    if (readEnd >= 0) ::close(readEnd);

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
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    result.exitCode = waited == pid ? exitCodeFromStatus(status) : -1;
    result.ok = result.exitCode == 0;
    std::istringstream lines(result.text);
    std::string line;
    while (std::getline(lines, line)) result.lines.push_back(line);
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
