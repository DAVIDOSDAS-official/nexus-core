#include <nexus/system/transaction_log.hpp>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <nexus/system/control_file.hpp>

namespace nexus::system {

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);

    std::tm parts{};

    if (gmtime_r(&now, &parts) == nullptr) {
        return "";
    }

    char buffer[32]{};

    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);

    return buffer;
}

std::string defaultTransactionLog() {
    if (::geteuid() == 0) {
        return "/var/lib/nexus/transactions.log";
    }

    if (const char* home = std::getenv("HOME")) {
        return std::string(home) +
               "/.local/state/nexus/transactions.log";
    }

    return "";
}

bool recordTransaction(
    const std::string& path,
    const TransactionRecord& record
) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;

    const std::filesystem::path file(path);

    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }

    std::ofstream out(path, std::ios::app);

    if (!out) {
        return false;
    }

    out << "When: " << record.when << "\n"
        << "Request: " << record.request << "\n"
        << "Resolved: " << record.resolved << "\n"
        << "Outcome: " << record.outcome << "\n"
        << "Exit: " << record.exitCode << "\n";

    if (!record.packages.empty()) {
        if (!record.succeeded) {
            out << "Attempted:";
        } else if (record.kind == TransactionKind::Remove) {
            out << "Removed:";
        } else {
            out << "Added:";
        }


        for (const std::string& name : record.packages) {
            out << " " << name;
        }

        out << "\n";
    }

    out << "\n";

    return static_cast<bool>(out);
}

std::vector<TransactionRecord> readTransactions(
    const std::string& path
) {
    std::vector<TransactionRecord> records;

    std::ifstream input(path);

    if (!input) {
        return records;
    }

    for (const ControlStanza& stanza : parseControlStream(input)) {
        if (!stanza.has("when")) {
            continue;
        }

        TransactionRecord record;

        record.when = stanza.value("when");
        record.request = stanza.value("request");
        record.resolved = stanza.value("resolved");
        record.outcome = stanza.value("outcome");
        record.exitCode = std::atoi(stanza.value("exit").c_str());
        record.succeeded =
            stanza.has("added") || stanza.has("removed");

        record.kind = stanza.has("removed")
            ? TransactionKind::Remove
            : TransactionKind::Install;

        std::string body = stanza.value("attempted");

        if (stanza.has("added")) {
            body = stanza.value("added");
        } else if (stanza.has("removed")) {
            body = stanza.value("removed");
        }

        std::istringstream names(body);

        std::string name;

        while (names >> name) {
            record.packages.insert(name);
        }

        records.push_back(std::move(record));
    }

    return records;
}

}
