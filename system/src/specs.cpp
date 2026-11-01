#include <nexus/system/specs.hpp>

#include <cstdio>
#include <sstream>

namespace nexus::system {

std::string parseCpuModel(const std::string& cpuinfo) {
    std::istringstream in(cpuinfo);
    std::string line;
    while (std::getline(in, line)) {
        // x86: "model name\t: Intel(R) Core(TM) i3-1115G4 ..."
        // Some arches have no "model name"; the caller falls back then.
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, colon);
        // Trim trailing spaces/tabs from the key.
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        if (key == "model name" || key == "Model" || key == "cpu model") {
            std::string value = line.substr(colon + 1);
            const auto first = value.find_first_not_of(" \t");
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t");
            return value.substr(first, last - first + 1);
        }
    }
    return {};
}

int parseCpuCores(const std::string& cpuinfo) {
    int count = 0;
    std::istringstream in(cpuinfo);
    std::string line;
    while (std::getline(in, line)) {
        // One "processor" line per thread the kernel sees.
        if (line.rfind("processor", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                while (!key.empty() &&
                       (key.back() == ' ' || key.back() == '\t')) {
                    key.pop_back();
                }
                if (key == "processor") {
                    ++count;
                }
            }
        }
    }
    return count;
}

void parseMemKiB(const std::string& meminfo,
                 std::uint64_t& totalKiB,
                 std::uint64_t& availKiB) {
    totalKiB = 0;
    availKiB = 0;
    std::istringstream in(meminfo);
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    while (in >> key >> value >> unit) {
        if (key == "MemTotal:") {
            totalKiB = value;
        } else if (key == "MemAvailable:") {
            availKiB = value;
        }
    }
}

long parseUptimeSeconds(const std::string& uptime) {
    // /proc/uptime: "<seconds up>.<hundredths> <idle> ..."
    std::istringstream in(uptime);
    double seconds = 0;
    if (in >> seconds && seconds >= 0) {
        return static_cast<long>(seconds);
    }
    return 0;
}

std::string formatUptime(long seconds) {
    if (seconds < 60) {
        return "up less than a minute";
    }

    const long days = seconds / 86400;
    const long hours = (seconds % 86400) / 3600;
    const long mins = (seconds % 3600) / 60;

    struct Unit {
        long value;
        const char* one;
        const char* many;
    };
    const Unit units[] = {
        {days, "day", "days"},
        {hours, "hour", "hours"},
        {mins, "min", "mins"},
    };

    std::vector<std::string> parts;
    for (const auto& u : units) {
        if (u.value > 0) {
            parts.push_back(std::to_string(u.value) + " " +
                            (u.value == 1 ? u.one : u.many));
        }
        if (parts.size() == 2) {
            break;  // largest two non-zero units
        }
    }

    if (parts.empty()) {
        return "up less than a minute";
    }

    std::string out = parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) {
        out += ", " + parts[i];
    }
    return out;
}

std::string parseOsReleaseValue(const std::string& osRelease,
                                const std::string& key) {
    std::istringstream in(osRelease);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) {
            std::string value = line.substr(key.size() + 1);
            if (value.size() >= 2 && value.front() == '"' &&
                value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return {};
}

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jsonString(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

std::string jsonStringArray(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) {
            out += ", ";
        }
        out += jsonString(items[i]);
    }
    out += "]";
    return out;
}

}  // namespace

std::string specsJson(const SpecsInfo& info) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"host\": " << jsonString(info.host) << ",\n";
    out << "  \"model\": " << jsonString(info.model) << ",\n";
    out << "  \"nexus_version\": " << jsonString(info.nexusVersion) << ",\n";
    out << "  \"edition\": " << jsonString(info.edition) << ",\n";
    out << "  \"os\": " << jsonString(info.osPretty) << ",\n";
    out << "  \"kernel\": " << jsonString(info.kernel) << ",\n";
    out << "  \"uptime\": " << jsonString(info.uptime) << ",\n";
    out << "  \"desktop\": " << jsonString(info.desktop) << ",\n";
    out << "  \"resolution\": " << jsonString(info.resolution) << ",\n";
    out << "  \"shell\": " << jsonString(info.shell) << ",\n";
    out << "  \"cpu\": " << jsonString(info.cpuModel) << ",\n";
    out << "  \"cpu_cores\": " << info.cpuCores << ",\n";
    out << "  \"gpus\": " << jsonStringArray(info.gpus) << ",\n";
    out << "  \"ram_used_mib\": " << info.ramUsedMiB << ",\n";
    out << "  \"ram_total_mib\": " << info.ramTotalMiB << ",\n";
    out << "  \"disks\": [";
    for (std::size_t i = 0; i < info.disks.size(); ++i) {
        const auto& d = info.disks[i];
        out << (i ? ",\n" : "\n");
        out << "    {\"mount\": " << jsonString(d.mount)
            << ", \"used_bytes\": " << d.usedBytes
            << ", \"total_bytes\": " << d.totalBytes << "}";
    }
    out << (info.disks.empty() ? "" : "\n  ") << "],\n";
    out << "  \"battery\": " << jsonString(info.battery) << ",\n";
    out << "  \"secure_boot\": " << jsonString(info.secureBoot) << ",\n";
    out << "  \"encryption\": " << jsonString(info.encryption) << ",\n";
    out << "  \"firewall\": " << jsonString(info.firewall) << ",\n";
    out << "  \"ntsync\": " << (info.ntsync ? "true" : "false") << ",\n";
    out << "  \"addons\": " << jsonStringArray(info.addons) << ",\n";
    out << "  \"signature\": " << jsonString(info.signature) << ",\n";
    out << "  \"last_update\": " << jsonString(info.lastUpdate) << ",\n";
    out << "  \"unreadable\": " << jsonStringArray(info.unreadable) << "\n";
    out << "}";
    return out.str();
}

}  // namespace nexus::system
