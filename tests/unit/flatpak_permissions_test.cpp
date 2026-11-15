#include <gtest/gtest.h>

#include <nexus/system/flatpak_permissions.hpp>

using nexus::system::parseFlatpakPermissions;

namespace {

bool has(const nexus::system::AppPermissions& p, const std::string& text, bool broad) {
    for (const auto& item : p.items) {
        if (item.text == text) return item.broad == broad;
    }
    return false;
}

}  // namespace

// 0.1.97, round-4 audit L4: what an app asks to reach is said before it
// is installed, and what reaches past the usual is marked.
TEST(FlatpakPermissionsTest, ReadsWhatAnAppAsksFor) {
    const auto p = parseFlatpakPermissions(
        "[Application]\nname=org.videolan.VLC\nruntime=org.kde.Platform/x86_64/6.8\n\n"
        "[Context]\nshared=network;ipc;\nsockets=x11;wayland;pulseaudio;\n"
        "devices=all;\nfilesystems=xdg-run/gvfsd;host;xdg-config/kdeglobals:ro;\n\n"
        "[Session Bus Policy]\norg.freedesktop.Flatpak=talk\norg.kde.StatusNotifierWatcher=talk\n");
    EXPECT_TRUE(p.read);
    EXPECT_TRUE(p.anyBroad());
    EXPECT_TRUE(has(p, "the internet", false));
    EXPECT_TRUE(has(p, "sound, and the microphone", false));
    EXPECT_TRUE(has(p, "every device: cameras, USB, game pads and more", true));
    EXPECT_TRUE(has(p, "all your files and the system's (host)", true));
    EXPECT_TRUE(has(p, "running commands outside its sandbox (org.freedesktop.Flatpak)", true));
    EXPECT_TRUE(has(p, "the place xdg-config/kdeglobals (read only)", false));
    // What reaches furthest comes first.
    ASSERT_FALSE(p.items.empty());
    EXPECT_TRUE(p.items.front().broad);
}

TEST(FlatpakPermissionsTest, AnOrdinaryAppHasNothingBroad) {
    const auto p = parseFlatpakPermissions(
        "[Application]\nname=org.gnome.Calculator\n\n[Context]\n"
        "shared=network;\nsockets=wayland;fallback-x11;\ndevices=dri;\n"
        "filesystems=xdg-download;~/Games:create;!home;\n");
    EXPECT_TRUE(p.read);
    EXPECT_FALSE(p.anyBroad());
    EXPECT_TRUE(has(p, "your Downloads folder", false));
    EXPECT_TRUE(has(p, "the folder ~/Games", false));
    EXPECT_TRUE(has(p, "the graphics card", false));
    // "!home" takes home away; it is not a permission.
    EXPECT_FALSE(has(p, "your whole home folder", true));
}

TEST(FlatpakPermissionsTest, HomeAndKeysAndAutostartAreBroad) {
    const auto p = parseFlatpakPermissions(
        "[Context]\nfilesystems=home:ro;~/.config/autostart;/srv/data;\n"
        "sockets=ssh-auth;session-bus;\n\n"
        "[Session Bus Policy]\norg.freedesktop.secrets=talk\n\n"
        "[System Bus Policy]\norg.freedesktop.NetworkManager=talk\n");
    EXPECT_TRUE(has(p, "your whole home folder (read only)", true));
    EXPECT_TRUE(has(p, "files that make programs start by itself (~/.config/autostart)", true));
    EXPECT_TRUE(has(p, "the folder /srv/data", true));
    EXPECT_TRUE(has(p, "your SSH keys (the SSH agent)", true));
    EXPECT_TRUE(has(p, "every service of your desktop session (session bus)", true));
    EXPECT_TRUE(has(p, "your saved passwords (the keyring)", true));
    EXPECT_TRUE(has(p, "the system service org.freedesktop.NetworkManager", true));
}

TEST(FlatpakPermissionsTest, NoMetadataIsNotRead) {
    EXPECT_FALSE(parseFlatpakPermissions("").read);
    EXPECT_FALSE(parseFlatpakPermissions("error: No remote refs found for org.x.Y\n").read);
    // Hostile text is made printable, not passed to the terminal.
    const auto p = parseFlatpakPermissions("[Context]\nfilesystems=/a\x1b[2Jb;\n");
    ASSERT_EQ(p.items.size(), 1u);
    EXPECT_EQ(p.items[0].text.find('\x1b'), std::string::npos);
}
