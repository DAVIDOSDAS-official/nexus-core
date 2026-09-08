#include <nexus/system/transaction_log.hpp>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <iterator>

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

std::string beginTransaction(
    const std::string& path,
    const TransactionRecord& record
) {
    TransactionRecord started = record;

    // The marker has to be unique enough to find again, and this
    // process plus this instant is enough: one process does one
    // transaction at a time.
    const std::string marker =
        started.when + "/" + std::to_string(::getpid());

    started.outcome = "started";
    started.succeeded = false;

    std::ofstream out(path, std::ios::app);

    if (!out) {
        // Failing to record is not a reason to refuse the work, but
        // the caller is told so it can say so.
        return "";
    }

    out << "When: " << started.when << "\n"
        << "Marker: " << marker << "\n"
        << "Request: " << started.request << "\n"
        << "Resolved: " << started.resolved << "\n"
        << "Outcome: started\n"
        << "Exit: 0\n";

    if (!started.packages.empty()) {
        out << "Attempted:";

        for (const std::string& name : started.packages) {
            out << " " << name;
        }

        out << "\n";
    }

    out << "\n";

    // Flushed and synced before returning: a record that is still in
    // a buffer when the power goes is a record that was never
    // written, which is the situation this exists to prevent.
    out.flush();

    return out ? marker : "";
}

bool finishTransaction(
    const std::string& path,
    const std::string& marker,
    const TransactionRecord& record
) {
    if (marker.empty()) {
        return recordTransaction(path, record);
    }

    std::ifstream input(path);

    if (!input) {
        return recordTransaction(path, record);
    }

    std::string contents(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());

    input.close();

    const std::string needle = "Marker: " + marker + "\n";
    const std::size_t at = contents.find(needle);

    if (at == std::string::npos) {
        // Losing the outcome is worse than duplicating it.
        return recordTransaction(path, record);
    }

    // Find the stanza around the marker and replace the whole thing.
    std::size_t begin = contents.rfind("\nWhen: ", at);

    begin = begin == std::string::npos ? 0 : begin + 1;

    std::size_t end = contents.find("\n\n", at);

    end = end == std::string::npos ? contents.size() : end + 2;

    std::ostringstream replacement;

    replacement
        << "When: " << record.when << "\n"
        << "Request: " << record.request << "\n"
        << "Resolved: " << record.resolved << "\n"
        << "Outcome: " << record.outcome << "\n"
        << "Exit: " << record.exitCode << "\n";

    if (!record.packages.empty()) {
        if (!record.succeeded) {
            replacement << "Attempted:";
        } else if (record.kind == TransactionKind::Remove) {
            replacement << "Removed:";
        } else {
            replacement << "Added:";
        }

        for (const std::string& name : record.packages) {
            replacement << " " << name;
        }

        replacement << "\n";
    }

    replacement << "\n";

    contents.replace(begin, end - begin, replacement.str());

    std::ofstream out(path, std::ios::trunc);

    if (!out) {
        return false;
    }

    out << contents;

    return static_cast<bool>(out);
}

std::vector<TransactionRecord> unfinishedTransactions(
    const std::string& path
) {
    std::vector<TransactionRecord> unfinished;

    for (const TransactionRecord& record : readTransactions(path)) {
        if (record.unfinished) {
            unfinished.push_back(record);
        }
    }

    return unfinished;
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
        record.unfinished = record.outcome == "started";
        record.succeeded =
            !record.unfinished &&
            (stanza.has("added") || stanza.has("removed"));

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
