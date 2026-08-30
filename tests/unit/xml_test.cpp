#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <nexus/system/xml.hpp>

using nexus::system::decodeEntities;
using nexus::system::XmlReader;

namespace {

// Flatten a document into a readable trace, so a test failure shows
// what the parser saw rather than which assertion tripped.
std::vector<std::string> trace(const std::string& document) {
    XmlReader reader(document);

    std::vector<std::string> events;

    while (true) {
        const XmlReader::Event event = reader.next();

        if (event == XmlReader::Event::None) {
            break;
        }

        if (event == XmlReader::Event::Error) {
            events.push_back("error:" + reader.error());
            break;
        }

        if (event == XmlReader::Event::StartElement) {
            events.push_back("<" + reader.name() + ">");
        } else if (event == XmlReader::Event::EndElement) {
            events.push_back("</" + reader.name() + ">");
        } else {
            events.push_back("'" + reader.text() + "'");
        }
    }

    return events;
}

}

TEST(XmlTest, ReadsElements) {
    EXPECT_EQ(
        trace("<a><b>text</b></a>"),
        (std::vector<std::string>{"<a>", "<b>", "'text'", "</b>", "</a>"})
    );
}

// Callers should never have to handle <a/> differently from <a></a>.
TEST(XmlTest, SelfClosingStillReportsAnEnd) {
    EXPECT_EQ(
        trace("<a><b/></a>"),
        (std::vector<std::string>{"<a>", "<b>", "</b>", "</a>"})
    );
}

TEST(XmlTest, ReadsAttributes) {
    XmlReader reader("<entry name='glibc' flags=\"GE\" epoch='0'/>");

    ASSERT_EQ(reader.next(), XmlReader::Event::StartElement);
    EXPECT_EQ(reader.name(), "entry");
    EXPECT_EQ(reader.attribute("name"), "glibc");
    EXPECT_EQ(reader.attribute("flags"), "GE");
    EXPECT_TRUE(reader.hasAttribute("epoch"));
    EXPECT_FALSE(reader.hasAttribute("release"));
    EXPECT_TRUE(reader.selfClosing());
}

// Namespaced names are kept whole: the metadata uses fixed prefixes
// and resolving them properly would buy nothing.
TEST(XmlTest, KeepsNamespacePrefixes) {
    EXPECT_EQ(
        trace("<rpm:requires><rpm:entry/></rpm:requires>"),
        (std::vector<std::string>{
            "<rpm:requires>", "<rpm:entry>", "</rpm:entry>",
            "</rpm:requires>"
        })
    );
}

TEST(XmlTest, SkipsDeclarationsCommentsAndDoctypes) {
    EXPECT_EQ(
        trace("<?xml version='1.0'?><!-- note --><!DOCTYPE x><a/>"),
        (std::vector<std::string>{"<a>", "</a>"})
    );
}

TEST(XmlTest, ReadsCdata) {
    EXPECT_EQ(
        trace("<a><![CDATA[raw <not> markup]]></a>"),
        (std::vector<std::string>{
            "<a>", "'raw <not> markup'", "</a>"
        })
    );
}

// Whitespace between elements is layout, not content.
TEST(XmlTest, IgnoresWhitespaceBetweenElements) {
    EXPECT_EQ(
        trace("<a>\n  <b/>\n</a>"),
        (std::vector<std::string>{"<a>", "<b>", "</b>", "</a>"})
    );
}

TEST(XmlTest, DecodesEntities) {
    EXPECT_EQ(decodeEntities("a &amp; b"), "a & b");
    EXPECT_EQ(decodeEntities("&lt;tag&gt;"), "<tag>");
    EXPECT_EQ(decodeEntities("&quot;x&apos;"), "\"x'");
    EXPECT_EQ(decodeEntities("&#65;&#x42;"), "AB");
    EXPECT_EQ(decodeEntities("no entities"), "no entities");
}

// Metadata in the wild contains bare ampersands. Refusing the file
// would be worse than passing the character through.
TEST(XmlTest, ToleratesAStrayAmpersand) {
    EXPECT_EQ(decodeEntities("Q&A"), "Q&A");
    EXPECT_EQ(decodeEntities("&unknown;"), "&unknown;");
}

TEST(XmlTest, DecodesEntitiesInAttributes) {
    XmlReader reader("<a summary='C&amp;C++ &lt;lib&gt;'/>");

    ASSERT_EQ(reader.next(), XmlReader::Event::StartElement);
    EXPECT_EQ(reader.attribute("summary"), "C&C++ <lib>");
}

TEST(XmlTest, LeavesNonAsciiEntitiesAlone) {
    // Decoding these as single bytes would corrupt the encoding.
    EXPECT_EQ(decodeEntities("&#233;"), "&#233;");
}

TEST(XmlTest, ReportsAMalformedAttribute) {
    const auto events = trace("<a name=unquoted/>");

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().rfind("error:", 0), 0u);
}

TEST(XmlTest, HandlesAnEmptyDocument) {
    EXPECT_TRUE(trace("").empty());
    EXPECT_TRUE(trace("   \n  ").empty());
}

TEST(XmlTest, TracksLineNumbers) {
    XmlReader reader("<a>\n<b>\n<c/>\n</b>\n</a>");

    while (reader.next() != XmlReader::Event::None) {
        if (reader.current() == XmlReader::Event::StartElement &&
            reader.name() == "c") {
            break;
        }
    }

    EXPECT_GE(reader.line(), 3u);
}

// A real primary.xml is tens of megabytes. Parsing must stream rather
// than build a document tree.
TEST(XmlTest, HandlesALargeDocument) {
    constexpr int kPackages = 20000;

    std::string document = "<metadata>";

    document.reserve(kPackages * 90);

    for (int index = 0; index < kPackages; ++index) {
        document += "<package><name>pkg-";
        document += std::to_string(index);
        document += "</name><version epoch='0' ver='1.0' rel='1'/>";
        document += "</package>";
    }

    document += "</metadata>";

    XmlReader reader(std::move(document));

    std::size_t packages = 0;

    while (true) {
        const XmlReader::Event event = reader.next();

        if (event == XmlReader::Event::None) {
            break;
        }

        ASSERT_NE(event, XmlReader::Event::Error) << reader.error();

        if (event == XmlReader::Event::StartElement &&
            reader.name() == "package") {
            packages += 1;
        }
    }

    EXPECT_EQ(packages, static_cast<std::size_t>(kPackages));
}
