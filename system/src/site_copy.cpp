#include <nexus/system/site_copy.hpp>

#include <algorithm>
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

// "sub/dir" + "../x" -> whether it stays inside the root, lexically.
bool staysInside(const std::string& linkDir, const std::string& target) {
    if (target.empty() || target[0] == '/') return false;
    std::vector<std::string> parts;
    auto push = [&](const std::string& path) -> bool {
        std::size_t i = 0;
        while (i <= path.size()) {
            const std::size_t j = path.find('/', i);
            const std::string part = path.substr(i, j == std::string::npos ? std::string::npos : j - i);
            if (part == "..") {
                if (parts.empty()) return false;
                parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return true;
    };
    return push(linkDir) && push(target);
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
             SiteCopyResult& result, int depth) {
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
            if (!staysInside(rel, target)) {
                result.error = "a link that leads outside the folder: " + path + " -> " + target;
                return false;
            }
            if (::symlinkat(target, dst, name.c_str()) != 0) {
                result.error = "could not copy the link " + path + ": " + std::strerror(errno);
                return false;
            }
            continue;
        }
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {
            result.error = "not a file or folder: " + path;
            return false;
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
            if (out.fd < 0 || !copyDir(child.fd, out.fd, path, reader, result, depth + 1)) {
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
    result.ok = copyDir(src.fd, dst.fd, "", reader, result, 0);
    return result;
}

}  // namespace nexus::system
