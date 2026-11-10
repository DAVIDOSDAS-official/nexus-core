#include <nexus/system/pins.hpp>

#include <cctype>
#include <fstream>
#include <sstream>

namespace nexus::system {

namespace {

bool isDigest(const std::string& text) {
    if (text.size() != 71 || text.rfind("sha256:", 0) != 0) return false;
    for (std::size_t i = 7; i < text.size(); ++i) {
        const char c = text[i];
        if (!std::isdigit(static_cast<unsigned char>(c)) && (c < 'a' || c > 'f')) return false;
    }
    return true;
}

}  // namespace

std::map<std::string, std::string> parsePins(const std::string& text) {
    std::map<std::string, std::string> pins;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        std::string ref, digest, extra;
        if (!(words >> ref >> digest) || (words >> extra)) continue;
        if (ref.find('@') != std::string::npos || !isDigest(digest)) continue;
        pins[ref] = digest;
    }
    return pins;
}

std::string pinnedImage(const std::string& ref, const std::string& pinsFile) {
    std::ifstream in(pinsFile);
    if (!in) return ref;
    std::stringstream text;
    text << in.rdbuf();
    const auto pins = parsePins(text.str());
    const auto it = pins.find(ref);
    return it == pins.end() ? ref : ref + "@" + it->second;
}

}  // namespace nexus::system
