#pragma once

#include <map>
#include <string>
#include <string_view>

namespace nexus::system {

// A pull parser for the subset of XML that package metadata uses.
//
// Streaming, not a document tree: a Fedora primary.xml is tens of
// megabytes uncompressed, and building a tree of it to read a handful
// of fields per package would cost more memory than the entire
// resolved system.
//
// Deliberately not a general XML parser. Namespaces are kept as part
// of the element name ("rpm:entry" stays "rpm:entry") because the
// metadata uses fixed prefixes and resolving them properly would buy
// nothing here. Anything it cannot handle is reported rather than
// guessed at.
class XmlReader {
public:
    enum class Event {
        None,
        StartElement,
        EndElement,
        Text,
        Error
    };

    explicit XmlReader(std::string document);

    // Advance to the next event. Returns Event::None at the end.
    Event next();

    Event current() const;

    // Valid after StartElement or EndElement.
    const std::string& name() const;

    // Valid after StartElement.
    const std::map<std::string, std::string>& attributes() const;

    std::string attribute(const std::string& key) const;

    bool hasAttribute(const std::string& key) const;

    // Valid after Text. Entity references are already decoded.
    const std::string& text() const;

    // True when the last StartElement was self-closing, in which case
    // a matching EndElement is still produced so callers never have to
    // special-case the two forms.
    bool selfClosing() const;

    const std::string& error() const;

    std::size_t line() const;

private:
    bool parseElement();
    void skipUntil(std::string_view terminator);

    std::string document_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;

    Event current_ = Event::None;
    std::string name_;
    std::string text_;
    std::map<std::string, std::string> attributes_;
    bool selfClosing_ = false;
    bool pendingEnd_ = false;
    std::string error_;
};

// Replace the five XML entity references, plus numeric ones.
std::string decodeEntities(const std::string& text);

}
