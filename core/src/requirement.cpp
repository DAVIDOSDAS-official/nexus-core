#include <nexus/requirement.hpp>

namespace nexus {

std::string toString(const Requirement& requirement) {
    std::string text;

    for (std::size_t index = 0;
         index < requirement.alternatives.size();
         ++index) {

        if (index > 0) {
            text += " | ";
        }

        text += toString(requirement.alternatives[index]);
    }

    return text;
}

}
