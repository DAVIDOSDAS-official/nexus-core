#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <nexus/system/site_copy.hpp>

namespace fs = std::filesystem;
using nexus::system::copySiteTree;
using nexus::system::Reader;
using nexus::system::readerMay;

namespace {

struct Dirs {
    fs::path root, src, dst;
    Dirs() {
        root = fs::path(::testing::TempDir()) / ("nexus-site-" + std::to_string(::getpid()) + "-" +
                                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(root);
        src = root / "site";
        dst = root / "out";
        fs::create_directories(src);
        fs::create_directories(dst);
    }
    ~Dirs() { std::error_code ec; fs::remove_all(root, ec); }
};

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

TEST(SiteCopyTest, CopiesTheSiteAndLeavesHiddenFilesOut) {
    Dirs d;
    write(d.src / "index.html", "hi");
    write(d.src / "css" / "a.css", "body{}");
    write(d.src / ".env", "SECRET=1");
    write(d.src / ".git" / "config", "[core]");
    write(d.src / ".well-known" / "security.txt", "Contact: x");
    const auto r = copySiteTree(d.src.string(), d.dst.string());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(read(d.dst / "index.html"), "hi");
    EXPECT_EQ(read(d.dst / "css" / "a.css"), "body{}");
    EXPECT_TRUE(fs::exists(d.dst / ".well-known" / "security.txt"));
    EXPECT_FALSE(fs::exists(d.dst / ".env"));
    EXPECT_FALSE(fs::exists(d.dst / ".git"));
    EXPECT_EQ(r.hidden.size(), 2u);
    EXPECT_EQ(r.files, 3u);
}

// The 0.1.76 review: a link to /etc/shadow became a public file.
TEST(SiteCopyTest, ALinkLeadingOutRefusesTheCopy) {
    Dirs d;
    write(d.src / "index.html", "hi");
    write(d.root / "secret", "outside");
    fs::create_symlink(d.root / "secret", d.src / "leak");
    auto r = copySiteTree(d.src.string(), d.dst.string());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("leads outside"), std::string::npos);

    Dirs e;
    fs::create_directories(e.src / "sub");
    fs::create_symlink("../../secret", e.src / "sub" / "up");
    r = copySiteTree(e.src.string(), e.dst.string());
    EXPECT_FALSE(r.ok);
}

// The 0.1.82 review: links were checked, then followed by the copy.
// Now a link is copied as a link, never read through.
TEST(SiteCopyTest, ALinkInsideIsCopiedAsALinkNotFollowed) {
    Dirs d;
    write(d.src / "index.html", "hi");
    fs::create_directories(d.src / "docs");
    fs::create_symlink("../index.html", d.src / "docs" / "home.html");
    const auto r = copySiteTree(d.src.string(), d.dst.string());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::is_symlink(d.dst / "docs" / "home.html"));
    EXPECT_EQ(fs::read_symlink(d.dst / "docs" / "home.html"), "../index.html");
}

TEST(SiteCopyTest, OnlyWhatThePersonCouldReadIsCopied) {
    Dirs d;
    write(d.src / "index.html", "hi");
    write(d.src / "private.txt", "no");
    ::chmod((d.src / "private.txt").c_str(), 0600);
    ::chmod((d.src / "index.html").c_str(), 0644);
    struct stat st {};
    ::stat(d.src.c_str(), &st);
    Reader someone;
    someone.uid = st.st_uid + 4242;   // not the owner, not in the group
    const auto r = copySiteTree(d.src.string(), d.dst.string(), someone);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(d.dst / "index.html"));
    EXPECT_FALSE(fs::exists(d.dst / "private.txt"));
    ASSERT_EQ(r.unreadable.size(), 1u);
    EXPECT_EQ(r.unreadable[0], "private.txt");
}

TEST(SiteCopyTest, ReadRightsByOwnerGroupAndOthers) {
    Reader me;
    me.uid = 1000;
    me.groups = {1000, 10};
    EXPECT_TRUE(readerMay(me, 1000, 1000, 0600, false));
    EXPECT_FALSE(readerMay(me, 1000, 1000, 0200, false));
    EXPECT_TRUE(readerMay(me, 0, 10, 0640, false));
    EXPECT_FALSE(readerMay(me, 0, 0, 0640, false));     // /etc/shadow-like
    EXPECT_TRUE(readerMay(me, 0, 0, 0644, false));
    EXPECT_FALSE(readerMay(me, 0, 0, 0744 & ~01, true)); // folder others cannot enter
    EXPECT_TRUE(readerMay(Reader{}, 0, 0, 0, false));    // no reader: no check
}
