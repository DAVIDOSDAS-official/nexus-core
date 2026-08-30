#include <nexus/system/xml.hpp>

#include <cctype>
#include <cstdlib>
#include <utility>

namespace nexus::system {

namespace {

bool isSpace(char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

bool isNameChar(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == ':' || character == '_' ||
           character == '-' || character == '.';
}

}

std::string decodeEntities(const std::string& text) {
    if (text.find('&') == std::string::npos) {
        return text;
    }

    std::string output;

    output.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '&') {
            output.push_back(text[index]);
            continue;
        }

        const std::size_t end = text.find(';', index);

        if (end == std::string::npos || end - index > 10) {
            // Not an entity, just a stray ampersand. Metadata in the
            // wild contains them; refusing the file would be worse
            // than passing the character through.
            output.push_back('&');
            continue;
        }

        const std::string entity =
            text.substr(index + 1, end - index - 1);

        if (entity == "amp") {
            output.push_back('&');
        } else if (entity == "lt") {
            output.push_back('<');
        } else if (entity == "gt") {
            output.push_back('>');
        } else if (entity == "quot") {
            output.push_back('"');
        } else if (entity == "apos") {
            output.push_back('\'');
        } else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex = entity[1] == 'x' || entity[1] == 'X';

            const long value = std::strtol(
                entity.c_str() + (hex ? 2 : 1),
                nullptr,
                hex ? 16 : 10
            );

            // Only ASCII is decoded; anything above it is left alone
            // rather than mangled into the wrong encoding.
            if (value > 0 && value < 128) {
                output.push_back(static_cast<char>(value));
            } else {
                output.append(text, index, end - index + 1);
            }
        } else {
            output.append(text, index, end - index + 1);
        }

        index = end;
    }

    return output;
}

XmlReader::XmlReader(std::string document)
    : document_(std::move(document)) {
}

XmlReader::Event XmlReader::current() const {
    return current_;
}

const std::string& XmlReader::name() const {
    return name_;
}

const std::map<std::string, std::string>&
XmlReader::attributes() const {
    return attributes_;
}

std::string XmlReader::attribute(const std::string& key) const {
    const auto entry = attributes_.find(key);

    return entry == attributes_.end() ? std::string{} : entry->second;
}

bool XmlReader::hasAttribute(const std::string& key) const {
    return attributes_.count(key) > 0;
}

const std::string& XmlReader::text() const {
    return text_;
}

bool XmlReader::selfClosing() const {
    return selfClosing_;
}

const std::string& XmlReader::error() const {
    return error_;
}

std::size_t XmlReader::line() const {
    return line_;
}

void XmlReader::skipUntil(std::string_view terminator) {
    const std::size_t end = document_.find(terminator, position_);

    if (end == std::string::npos) {
        position_ = document_.size();
        return;
    }

    for (std::size_t index = position_; index < end; ++index) {
        if (document_[index] == '\n') {
            line_ += 1;
        }
    }

    position_ = end + terminator.size();
}

XmlReader::Event XmlReader::next() {
    // A self-closing element reports its end separately, so callers
    // never have to handle <a/> differently from <a></a>.
    if (pendingEnd_) {
        pendingEnd_ = false;
        current_ = Event::EndElement;
        return current_;
    }

    attributes_.clear();
    text_.clear();
    selfClosing_ = false;

    if (position_ >= document_.size()) {
        current_ = Event::None;
        return current_;
    }

    if (document_[position_] != '<') {
        const std::size_t start = position_;
        const std::size_t end = document_.find('<', position_);

        const std::size_t stop =
            end == std::string::npos ? document_.size() : end;

        for (std::size_t index = start; index < stop; ++index) {
            if (document_[index] == '\n') {
                line_ += 1;
            }
        }

        std::string raw = document_.substr(start, stop - start);

        position_ = stop;

        // Whitespace between elements is not content.
        bool blank = true;

        for (char character : raw) {
            if (!isSpace(character)) {
                blank = false;
                break;
            }
        }

        if (blank) {
            return next();
        }

        text_ = decodeEntities(raw);
        current_ = Event::Text;

        return current_;
    }

    // Comments, declarations, doctypes and CDATA carry nothing this
    // parser needs.
    if (document_.compare(position_, 4, "<!--") == 0) {
        position_ += 4;
        skipUntil("-->");
        return next();
    }

    if (document_.compare(position_, 9, "<![CDATA[") == 0) {
        position_ += 9;

        const std::size_t end = document_.find("]]>", position_);
        const std::size_t stop =
            end == std::string::npos ? document_.size() : end;

        text_ = document_.substr(position_, stop - position_);
        position_ = end == std::string::npos ? stop : end + 3;

        current_ = Event::Text;

        return current_;
    }

    if (document_.compare(position_, 2, "<?") == 0) {
        position_ += 2;
        skipUntil("?>");
        return next();
    }

    if (document_.compare(position_, 2, "<!") == 0) {
        position_ += 2;
        skipUntil(">");
        return next();
    }

    return parseElement() ? current_ : Event::Error;
}

bool XmlReader::parseElement() {
    position_ += 1;

    const bool closing =
        position_ < document_.size() && document_[position_] == '/';

    if (closing) {
        position_ += 1;
    }

    const std::size_t nameStart = position_;

    while (position_ < document_.size() &&
           isNameChar(document_[position_])) {
        position_ += 1;
    }

    if (position_ == nameStart) {
        error_ = "expected an element name";
        current_ = Event::Error;
        return false;
    }

    name_ = document_.substr(nameStart, position_ - nameStart);

    // Attributes.
    while (position_ < document_.size()) {
        while (position_ < document_.size() &&
               isSpace(document_[position_])) {
            if (document_[position_] == '\n') {
                line_ += 1;
            }

            position_ += 1;
        }

        if (position_ >= document_.size()) {
            break;
        }

        if (document_[position_] == '>') {
            position_ += 1;
            break;
        }

        if (document_[position_] == '/') {
            position_ += 1;

            if (position_ < document_.size() &&
                document_[position_] == '>') {
                position_ += 1;
            }

            selfClosing_ = true;
            pendingEnd_ = true;
            break;
        }

        const std::size_t keyStart = position_;

        while (position_ < document_.size() &&
               isNameChar(document_[position_])) {
            position_ += 1;
        }

        if (position_ == keyStart) {
            error_ = "expected an attribute name";
            current_ = Event::Error;
            return false;
        }

        const std::string key =
            document_.substr(keyStart, position_ - keyStart);

        while (position_ < document_.size() &&
               isSpace(document_[position_])) {
            position_ += 1;
        }

        if (position_ >= document_.size() ||
            document_[position_] != '=') {
            // A valueless attribute. Record it as empty rather than
            // failing the whole document over it.
            attributes_[key] = "";
            continue;
        }

        position_ += 1;

        while (position_ < document_.size() &&
               isSpace(document_[position_])) {
            position_ += 1;
        }

        if (position_ >= document_.size()) {
            break;
        }

        const char quote = document_[position_];

        if (quote != '"' && quote != '\'') {
            error_ = "attribute value is not quoted";
            current_ = Event::Error;
            return false;
        }

        position_ += 1;

        const std::size_t valueStart = position_;

        while (position_ < document_.size() &&
               document_[position_] != quote) {
            if (document_[position_] == '\n') {
                line_ += 1;
            }

            position_ += 1;
        }

        attributes_[key] = decodeEntities(
            document_.substr(valueStart, position_ - valueStart)
        );

        if (position_ < document_.size()) {
            position_ += 1;
        }
    }

    current_ = closing ? Event::EndElement : Event::StartElement;

    return true;
}

}
