#include <nexus/system/process.hpp>

#include <filesystem>
#include <vector>
#include <unistd.h>
#include <spawn.h>
#include <fcntl.h>
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

    // Small inputs (keys): written whole, then closed so the child sees
    // the end.
    // A child that exits without reading would otherwise kill us with
    // SIGPIPE; the write just fails instead.
    struct sigaction ignore {}, previous {};
    ignore.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &ignore, &previous);
    std::size_t done = 0;
    while (done < input.size()) {
        const ssize_t w = ::write(toChild[1], input.data() + done, input.size() - done);
        if (w < 0) { if (errno == EINTR) continue; break; }
        done += static_cast<std::size_t>(w);
    }
    ::close(toChild[1]);
    ::sigaction(SIGPIPE, &previous, nullptr);

    char buffer[4096];
    while (true) {
        const ssize_t n = ::read(fromChild[0], buffer, sizeof buffer);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        result.text.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fromChild[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    result.ok = result.exitCode == 0;
    std::istringstream lines(result.text);
    std::string line;
    while (std::getline(lines, line)) result.lines.push_back(line);
    return result;
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

    result.exitCode = status == -1 ? -1 : WEXITSTATUS(status);
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
