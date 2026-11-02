#include <nexus/system/suggest.hpp>

#include <algorithm>
#include <set>
#include <utility>

namespace nexus::system {

std::size_t boundedDistance(const std::string& a, const std::string& b,
                            std::size_t limit) {
    const std::size_t gap = a.size() > b.size() ? a.size() - b.size()
                                                : b.size() - a.size();
    if (gap > limit) return limit + 1;

    std::vector<std::size_t> previous(b.size() + 1), current(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) previous[j] = j;

    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        std::size_t rowBest = current[0];
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t change =
                previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, change});
            rowBest = std::min(rowBest, current[j]);
        }
        if (rowBest > limit) return limit + 1;
        previous.swap(current);
    }
    return previous[b.size()];
}

std::vector<std::string> spellingSuggestions(
    const std::vector<std::string>& names,
    const std::string& typed,
    std::size_t max) {
    if (typed.empty() || max == 0) return {};
    const std::size_t limit = typed.size() >= 6 ? 2 : 1;

    std::set<std::string> seen;
    std::vector<std::pair<std::size_t, std::string>> found;
    for (const std::string& name : names) {
        if (name == typed || !seen.insert(name).second) continue;
        const std::size_t d = boundedDistance(name, typed, limit);
        if (d <= limit) found.emplace_back(d, name);
    }

    std::sort(found.begin(), found.end(), [](const auto& x, const auto& y) {
        if (x.first != y.first) return x.first < y.first;
        if (x.second.size() != y.second.size())
            return x.second.size() < y.second.size();
        return x.second < y.second;
    });

    std::vector<std::string> out;
    for (const auto& [d, name] : found) {
        out.push_back(name);
        if (out.size() == max) break;
    }
    return out;
}

}  // namespace nexus::system
