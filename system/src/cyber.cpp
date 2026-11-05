#include <nexus/system/cyber.hpp>

#include <cctype>
#include <sstream>

namespace nexus::system {

const std::vector<CyberTier>& cyberTiers() {
    static const std::vector<CyberTier> tiers = {
        {"small", "kali-tools-top10",
         "Kali's ten most used tools (nmap, Metasploit, Burp Suite, Wireshark, aircrack-ng, "
         "John, Hydra, sqlmap and friends)"},
        {"medium", "kali-linux-default",
         "the tool set a normal Kali install ships with"},
        {"full", "kali-linux-everything",
         "every tool Kali packages -- very large"},
    };
    return tiers;
}

std::optional<CyberTier> cyberTier(const std::string& name) {
    for (const auto& t : cyberTiers()) if (t.name == name) return t;
    return std::nullopt;
}

bool safeToolName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name[0] == '-' || name[0] == '.') return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '.' || c == '_' || c == '+' || c == '-')) return false;
    }
    return true;
}

namespace {
std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace

BoxState parseDistroboxList(const std::string& text, const std::string& name) {
    // ID           | NAME        | STATUS             | IMAGE
    // 3f2a...      | nexus-cyber | Up 2 minutes       | docker.io/kalilinux/kali-rolling:latest
    BoxState st;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> cols;
        std::stringstream ls(line);
        std::string c;
        while (std::getline(ls, c, '|')) cols.push_back(trim(c));
        if (cols.size() < 3 || cols[1] != name) continue;
        st.exists = true;
        st.status = cols[2];
        st.running = st.status.rfind("Up", 0) == 0;
        if (cols.size() > 3) st.image = cols[3];
        break;
    }
    return st;
}

}  // namespace nexus::system
