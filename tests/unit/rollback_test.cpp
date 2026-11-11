#include <gtest/gtest.h>

#include <nexus/system/rollback.hpp>

using namespace nexus::system;

namespace {
const std::string A(64, 'a');
const std::string B = "5f452330679b2f3ca3aa8040936d8dbd56b694819c3b6eb72adfb68b835f2764";
}

// The Asus, 8 November, after the boot check went back.
TEST(RollbackTest, ReadsOstreeAdminStatus) {
    const std::string text =
        "* default " + B + ".0\n"
        "    Version: 44.20261108.0\n"
        "    origin refspec: ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde\n"
        "  default " + A + ".0 (rollback)\n"
        "    Version: 44.20261108.0\n";
    const auto d = parseOstreeAdminStatus(text);
    ASSERT_EQ(d.size(), 2u);
    EXPECT_TRUE(d[0].booted);
    EXPECT_EQ(d[0].stateroot, "default");
    EXPECT_EQ(d[0].id, B + ".0");
    EXPECT_EQ(d[0].flag, "");
    EXPECT_FALSE(d[1].booted);
    EXPECT_EQ(d[1].flag, "rollback");
}

TEST(RollbackTest, StagedAndJunk) {
    const auto d = parseOstreeAdminStatus(
        "  default " + A + ".1 (staged)\n* default " + B + ".0\nerror: something\n  default nothex.0\n");
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0].flag, "staged");
    EXPECT_TRUE(d[1].booted);
}

TEST(RollbackTest, ImageRefsByTagAndDigest) {
    const auto t = parseImageRef("ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->transport, "ostree-image-signed:docker://");
    EXPECT_EQ(t->repository, "ghcr.io/davidosdas-official/nexus-core");
    EXPECT_EQ(t->tag, "kde");
    EXPECT_EQ(editionOf(*t), "kde");

    const std::string pinned = "ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core@sha256:" + B;
    const auto p = parseImageRef(pinned);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->digest, "sha256:" + B);
    EXPECT_EQ(editionOf(*p), "");
    EXPECT_EQ(imageRefString(*p), pinned);

    auto moved = *t; moved.tag = "minimal";
    EXPECT_EQ(imageRefString(moved), "ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:minimal");

    const auto port = parseImageRef("ostree-unverified-registry:docker://localhost:5000/x");
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(port->repository, "localhost:5000/x");
    EXPECT_EQ(port->tag, "latest");

    EXPECT_FALSE(parseImageRef("nonsense").has_value());
    EXPECT_FALSE(parseImageRef("x://repo@sha256:short").has_value());
}

TEST(RollbackTest, Digests) {
    EXPECT_TRUE(validDigest("sha256:" + B));
    EXPECT_FALSE(validDigest("sha256:" + B.substr(1)));
    EXPECT_FALSE(validDigest("sha256:" + std::string(64, 'G')));
    EXPECT_FALSE(validDigest(B));
    EXPECT_FALSE(validDigest("sha256:" + B + "; rm -rf /"));
}

// An outside review (0.1.84): a machine on an unverified origin was told
// to run `nexus rebase kde`, which could not read rpm-ostree's short
// form at all and kept the long form unverified.
TEST(RollbackTest, UnverifiedNexusOriginsMoveToSigned) {
    using namespace nexus::system;
    const auto shortForm = parseImageRef("ostree-unverified-registry:ghcr.io/davidosdas-official/nexus-core:kde");
    ASSERT_TRUE(shortForm.has_value());
    EXPECT_EQ(shortForm->repository, "ghcr.io/davidosdas-official/nexus-core");
    EXPECT_EQ(shortForm->tag, "kde");
    EXPECT_EQ(transportFor(*shortForm), "ostree-image-signed:docker://");

    const auto longForm = parseImageRef("ostree-unverified-image:docker://ghcr.io/davidosdas-official/nexus-core:minimal");
    ASSERT_TRUE(longForm.has_value());
    ImageRef next = *longForm;
    next.transport = transportFor(next);
    EXPECT_EQ(imageRefString(next), "ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:minimal");

    // Not a Nexus image: left as it is.
    const auto other = parseImageRef("ostree-unverified-registry:quay.io/fedora/fedora-kinoite:44");
    ASSERT_TRUE(other.has_value());
    EXPECT_EQ(transportFor(*other), "ostree-unverified-registry:");
}
