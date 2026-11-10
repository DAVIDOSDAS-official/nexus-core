#include <nexus/system/site_copy.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexus::system {

namespace {

constexpr int kMaxDepth = 64;

struct Fd {
    int fd = -1;
    explicit Fd(int f) : fd(f) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

std::string join(const std::string& a, const std::string& b) {
    return a.empty() ? b : a + "/" + b;
}

struct PendingLink {
    std::string dir;     // the link's folder, relative to the root
    std::string name;
    std::string target;
};

// Whether `target`, read from the folder `dir`, reaches something in the
// finished copy (`root`, root-owned, nothing else writes to it) through
// real folders only. Checked in the copy, not in the source, so nothing
// can be swapped underneath; a component that is itself a link is
// refused, because only then does the path mean what it says (0.1.83,
// outside review: "up -> ." made "up/up/../../data/..." leave /srv).
bool reachesInside(int root, const std::string& dir, const std::string& target) {
    if (target.empty() || target[0] == '/') return false;
    std::vector<std::string> parts;
    auto split = [](const std::string& path) {
        std::vector<std::string> out;
        std::size_t i = 0;
        while (i <= path.size()) {
            const std::size_t j = path.find('/', i);
            out.push_back(path.substr(i, j == std::string::npos ? std::string::npos : j - i));
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return out;
    };
    for (const auto& part : split(dir)) if (!part.empty() && part != ".") parts.push_back(part);
    const auto steps = split(target);
    for (std::size_t k = 0; k < steps.size(); ++k) {
        const std::string& part = steps[k];
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
            continue;
        }
        parts.push_back(part);
        std::string here;
        for (const auto& p : parts) here = here.empty() ? p : here + "/" + p;
        struct stat st {};
        if (::fstatat(root, here.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) return false;  // not copied
        if (S_ISLNK(st.st_mode)) return false;                                         // a link in the path
        bool last = true;
        for (std::size_t m = k + 1; m < steps.size(); ++m)
            if (!steps[m].empty() && steps[m] != ".") { last = false; break; }
        if (!last && !S_ISDIR(st.st_mode)) return false;
    }
    return true;
}

bool copyFile(int from, int to, std::string& error) {
    char buffer[65536];
    while (true) {
        const ssize_t n = ::read(from, buffer, sizeof buffer);
        if (n == 0) return true;
        if (n < 0) {
            if (errno == EINTR) continue;
            error = std::strerror(errno);
            return false;
        }
        for (ssize_t done = 0; done < n;) {
            const ssize_t w = ::write(to, buffer + done, static_cast<std::size_t>(n - done));
            if (w < 0) {
                if (errno == EINTR) continue;
                error = std::strerror(errno);
                return false;
            }
            done += w;
        }
    }
}

bool copyDir(int src, int dst, const std::string& rel, const Reader& reader,
             SiteCopyResult& result, int depth, std::vector<PendingLink>& links) {
    if (depth > kMaxDepth) {
        result.error = "folders nested deeper than " + std::to_string(kMaxDepth) + ": " + rel;
        return false;
    }
    const int listFd = ::dup(src);
    if (listFd < 0) {
        result.error = "cannot read " + (rel.empty() ? std::string(".") : rel);
        return false;
    }
    DIR* dir = ::fdopendir(listFd);
    if (dir == nullptr) {
        ::close(listFd);
        result.error = "cannot read " + (rel.empty() ? std::string(".") : rel);
        return false;
    }
    std::vector<std::string> names;
    while (const dirent* e = ::readdir(dir)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    ::closedir(dir);
    std::sort(names.begin(), names.end());

    for (const std::string& name : names) {
        const std::string path = join(rel, name);
        if (name[0] == '.' && name != ".well-known") {
            result.hidden.push_back(path);
            continue;
        }
        struct stat st {};
        if (::fstatat(src, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            result.error = "cannot read " + path + ": " + std::strerror(errno);
            return false;
        }
        if (S_ISLNK(st.st_mode)) {
            char target[4096];
            const ssize_t n = ::readlinkat(src, name.c_str(), target, sizeof target - 1);
            if (n < 0) {
                result.error = "cannot read the link " + path;
                return false;
            }
            target[n] = '\0';
            links.push_back({rel, name, target});   // made at the end, once checked in the copy
            continue;
        }
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {
            result.error = "not a file or folder: " + path;
            return false;
        }
        if (S_ISREG(st.st_mode) && isServerSideFile(name)) {
            result.serverSide.push_back(path);
            continue;
        }
        if (!readerMay(reader, st.st_uid, st.st_gid, st.st_mode, S_ISDIR(st.st_mode))) {
            result.unreadable.push_back(path);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            Fd child(::openat(src, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (child.fd < 0) {
                result.error = "cannot open " + path + ": " + std::strerror(errno);
                return false;
            }
            if (::mkdirat(dst, name.c_str(), 0755) != 0) {
                result.error = "could not make " + path + ": " + std::strerror(errno);
                return false;
            }
            Fd out(::openat(dst, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (out.fd < 0 || !copyDir(child.fd, out.fd, path, reader, result, depth + 1, links)) {
                if (result.error.empty()) result.error = "could not open the copy of " + path;
                return false;
            }
            continue;
        }
        // A regular file: opened without following, checked again on
        // the descriptor (it may have been replaced since fstatat).
        Fd in(::openat(src, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        struct stat again {};
        if (in.fd < 0 || ::fstat(in.fd, &again) != 0 || !S_ISREG(again.st_mode) ||
            !readerMay(reader, again.st_uid, again.st_gid, again.st_mode, false)) {
            result.error = "changed while copying, or cannot be opened: " + path;
            return false;
        }
        Fd out(::openat(dst, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644));
        std::string why;
        if (out.fd < 0 || !copyFile(in.fd, out.fd, why)) {
            result.error = "could not copy " + path + (why.empty() ? "" : ": " + why);
            return false;
        }
        ++result.files;
    }
    return true;
}

}  // namespace

bool isServerSideFile(const std::string& name) {
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!lower.empty() && lower.back() == '~') return true;   // editor backup
    static const char* kEndings[] = {
        ".php", ".phtml", ".php3", ".php4", ".php5", ".phps", ".inc",
        ".py", ".pyc", ".rb", ".pl", ".cgi", ".asp", ".aspx", ".jsp",
        ".sql", ".sqlite", ".sqlite3", ".db", ".mdb",
        ".bak", ".old", ".orig", ".swp", ".key", ".pem", ".env",
    };
    for (const char* e : kEndings) {
        const std::string end = e;
        if (lower.size() > end.size() && lower.compare(lower.size() - end.size(), end.size(), end) == 0)
            return true;
    }
    return false;
}

bool readerMay(const Reader& reader, uid_t owner, gid_t group, mode_t mode, bool directory) {
    if (reader.uid == static_cast<uid_t>(-1)) return true;
    const mode_t need = directory ? 05 : 04;   // read (+ enter, for a folder)
    if (owner == reader.uid) return ((mode >> 6) & need) == need;
    if (std::find(reader.groups.begin(), reader.groups.end(), group) != reader.groups.end())
        return ((mode >> 3) & need) == need;
    return (mode & need) == need;
}

SiteCopyResult copySiteTree(const std::string& source, const std::string& destination,
                            const Reader& reader) {
    SiteCopyResult result;
    Fd src(::open(source.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (src.fd < 0) {
        result.error = "cannot open " + source + ": " + std::strerror(errno);
        return result;
    }
    struct stat st {};
    if (::fstat(src.fd, &st) != 0 || !readerMay(reader, st.st_uid, st.st_gid, st.st_mode, true)) {
        result.error = "you cannot read " + source + " yourself";
        return result;
    }
    Fd dst(::open(destination.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (dst.fd < 0) {
        result.error = "cannot open " + destination + ": " + std::strerror(errno);
        return result;
    }
    std::vector<PendingLink> links;
    if (!copyDir(src.fd, dst.fd, "", reader, result, 0, links)) return result;
    for (const auto& link : links) {
        const std::string path = join(link.dir, link.name);
        if (!reachesInside(dst.fd, link.dir, link.target)) {
            result.error = "a link that leads outside the folder, through another link, or to something"
                           " not published: " + path + " -> " + link.target;
            return result;
        }
        Fd folder(link.dir.empty() ? ::dup(dst.fd)
                                   : ::openat(dst.fd, link.dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (folder.fd < 0 || ::symlinkat(link.target.c_str(), folder.fd, link.name.c_str()) != 0) {
            result.error = "could not copy the link " + path + ": " + std::strerror(errno);
            return result;
        }
    }
    result.ok = true;
    return result;
}

}  // namespace nexus::system
