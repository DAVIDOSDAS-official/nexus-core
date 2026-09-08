#include <gtest/gtest.h>

#include <nexus/system/nix_source.hpp>

using nexus::Source;
using nexus::system::parseNixProfile;
using nexus::system::versionFromStorePath;

namespace {

// Captured from a real profile. Stanzas of Key: value, and the store
// paths carry the version where the entry itself does not.
const char* kProfile =
    "Name:               hyprland\n"
    "Flake attribute:    packages.x86_64-linux.hyprland\n"
    "Original flake URL: github:hyprwm/Hyprland\n"
    "Locked flake URL:   github:hyprwm/Hyprland/e3c9b64\n"
    "Store paths:        /nix/store/30yliaxg0ga7fd2z6bk6575gni1fh8cn-hyprland-0.54.0+date=2026-04-24_e3c9b64-man /nix/store/m9syk06d5mz34d0ahj8y57aw5gfqlwyw-hyprland-0.54.0+date=2026-04-24_e3c9b64\n";

}

TEST(NixTest, ReadsAProfile) {
    const auto result = parseNixProfile(kProfile);

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].name(), "hyprland");
    EXPECT_EQ(result.installed, 1u);
}

TEST(NixTest, PackagesAreMarkedAsComingFromNix) {
    const auto result = parseNixProfile(kProfile);

    ASSERT_FALSE(result.components.empty());
    EXPECT_EQ(result.components[0].source(), Source::Nix);
    EXPECT_TRUE(isIsolated(result.components[0].source()));
}

// A store path is /nix/store/<hash>-<name>-<version>.
TEST(NixTest, RecoversAVersionFromAStorePath) {
    EXPECT_EQ(
        versionFromStorePath(
            "/nix/store/abc123-hyprland-0.54.0", "hyprland"),
        "0.54.0");

    EXPECT_EQ(
        versionFromStorePath("/nix/store/abc123-ripgrep-14.1.0",
                             "ripgrep"),
        "14.1.0");
}

// Some outputs are suffixed -man, -doc, -dev. Those are the same
// derivation rather than a version, and reading one as a version
// would report ripgrep 14.1.0 as ripgrep "man".
TEST(NixTest, OutputSuffixesAreNotVersions) {
    EXPECT_TRUE(
        versionFromStorePath("/nix/store/abc-hyprland-man",
                             "hyprland").empty());
    EXPECT_TRUE(
        versionFromStorePath("/nix/store/abc-hyprland-doc",
                             "hyprland").empty());
}

TEST(NixTest, SkipsSuffixedPathsAndFindsTheRealVersion) {
    const auto result = parseNixProfile(kProfile);

    ASSERT_EQ(result.components.size(), 1u);

    // The first path ends in -man; the version comes from the second.
    EXPECT_FALSE(result.components[0].version().empty());
    EXPECT_NE(result.components[0].version().find("0.54.0"),
              std::string::npos);
}

// Not every derivation has a version, and inventing one would be
// worse than leaving it empty.
TEST(NixTest, AMissingVersionStaysEmpty) {
    EXPECT_TRUE(
        versionFromStorePath("/nix/store/abc-something", "other")
            .empty());
    EXPECT_TRUE(versionFromStorePath("", "x").empty());
}

TEST(NixTest, ReadsSeveralEntries) {
    const auto result = parseNixProfile(
        std::string(kProfile) +
        "\n"
        "Name:               ripgrep\n"
        "Flake attribute:    legacyPackages.x86_64-linux.ripgrep\n"
        "Store paths:        /nix/store/xyz-ripgrep-14.1.0\n");

    ASSERT_EQ(result.components.size(), 2u);
    EXPECT_EQ(result.components[1].name(), "ripgrep");
    EXPECT_EQ(result.components[1].version(), "14.1.0");
}

TEST(NixTest, AnEmptyProfileIsNotAFailure) {
    const auto result = parseNixProfile("");

    EXPECT_TRUE(result.components.empty());
    EXPECT_TRUE(result.error.empty());
}

// nix colours its output, and a value that reads as "hyprland" on a
// terminal arrives as "\033[1mhyprland\033[0m" through a pipe. The
// parse succeeds, the count is right, and every name is unfindable --
// which looks exactly like the components never being loaded.
TEST(NixTest, TerminalEscapesAreStripped) {
    const std::string coloured =
        "\033[1mName:\033[0m               \033[32mhyprland\033[0m\n"
        "\033[1mStore paths:\033[0m        "
        "/nix/store/m9sy-hyprland-0.54.0\n";

    const auto result = parseNixProfile(coloured);

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].name(), "hyprland");
    EXPECT_EQ(result.components[0].version(), "0.54.0");
}

TEST(NixTest, PlainOutputIsUnaffected) {
    const auto result = parseNixProfile(
        "Name:               ripgrep\n"
        "Store paths:        /nix/store/xyz-ripgrep-14.1.0\n");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].name(), "ripgrep");
}

// An output suffix sits at the end of the whole version rather than
// being the whole of it: hyprland's man output is
// "0.54.0+date=2026-04-24_e3c9b64-man". Checking for equality caught
// only the short case and reported the man pages' path as the
// version.
TEST(NixTest, LongOutputSuffixesAreAlsoRejected) {
    EXPECT_TRUE(
        versionFromStorePath(
            "/nix/store/30yl-hyprland-0.54.0+date=2026-04-24_e3c9b64-man",
            "hyprland").empty());

    EXPECT_EQ(
        versionFromStorePath(
            "/nix/store/m9sy-hyprland-0.54.0+date=2026-04-24_e3c9b64",
            "hyprland"),
        "0.54.0+date=2026-04-24_e3c9b64");
}

TEST(NixTest, TheRealVersionIsFoundPastTheManPath) {
    const auto result = parseNixProfile(
        "Name:               hyprland\n"
        "Store paths:        "
        "/nix/store/30yl-hyprland-0.54.0+date=2026-04-24_e3c9b64-man "
        "/nix/store/m9sy-hyprland-0.54.0+date=2026-04-24_e3c9b64\n");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].version(),
              "0.54.0+date=2026-04-24_e3c9b64");
}
