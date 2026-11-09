#include <nexus/system/gaming.hpp>

#include <cctype>

namespace nexus::system {

std::vector<GamingFix> gamingPlan(const GamingState& s, const std::string& only) {
    std::vector<GamingFix> plan;
    if (only.empty() || only == "ntsync") {
        if (s.ntsyncModule && !s.ntsyncDevice) plan.push_back(GamingFix::NtsyncNow);
        if (s.ntsyncModule && !s.ntsyncAtStart) plan.push_back(GamingFix::NtsyncAtStart);
    }
    if (only.empty() || only == "split-lock") {
        // only where the kernel has the knob and it still slows things down
        if (s.splitLock && *s.splitLock != 0) plan.push_back(GamingFix::SplitLockOff);
        else if (s.splitLock && !s.splitLockConf) {
            // already 0 now, but not kept across restarts by Nexus: leave it;
            // someone else set it, and that is their setting.
        }
    }
    return plan;
}

GamingUndo gamingUndoPlan(bool ntsyncConf, bool splitLockConf) {
    return GamingUndo{ntsyncConf, splitLockConf};
}

std::optional<int> parseSysctlInt(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size()) return std::nullopt;
    bool neg = false;
    if (text[i] == '-') { neg = true; ++i; }
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return std::nullopt;
    long long v = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        v = v * 10 + (text[i] - '0');
        if (v > 2000000000L) return std::nullopt;
        ++i;
    }
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i != text.size()) return std::nullopt;
    return static_cast<int>(neg ? -v : v);
}

}  // namespace nexus::system
