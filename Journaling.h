#ifndef JOURNALING_H
#define JOURNALING_H

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <mutex>

namespace CommandJournaling {

    struct Entry {
        std::string mountId;
        std::string operation;
        std::string path;
        std::string content;
        std::time_t when;
    };

    inline std::vector<Entry>& store() {
        static std::vector<Entry> entries;
        return entries;
    }

    inline std::mutex& journalMutex() {
        static std::mutex mtx;
        return mtx;
    }

    inline std::string normalizeId(const std::string& s) {
        std::string out = s;

        // pasar a minúsculas
        std::transform(out.begin(), out.end(), out.begin(), ::tolower);

        // eliminar espacios
        out.erase(std::remove_if(out.begin(), out.end(), ::isspace), out.end());

        return out;
    }

    inline void add(const std::string& mountId,
                    const std::string& operation,
                    const std::string& path,
                    const std::string& content) {
        if (mountId.empty()) return;

        std::lock_guard<std::mutex> lock(journalMutex());

        store().push_back({
            normalizeId(mountId),
            operation,
            path,
            content,
            std::time(nullptr)
        });
    }

    inline std::string toLower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), ::tolower);
        return out;
    }

    inline std::string normalize(const std::string& s) {
        std::string out = s;
        for (char& c : out) {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        }
        return out;
    }

    inline std::string fit(const std::string& s, size_t width) {
        std::string clean = normalize(s);
        if (clean.size() <= width) return clean + std::string(width - clean.size(), ' ');
        if (width <= 3) return clean.substr(0, width);
        return clean.substr(0, width - 3) + "...";
    }

    inline std::string dateTime(std::time_t t) {
        char buf[32];
        std::tm* tmInfo = std::localtime(&t);
        std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", tmInfo);
        return std::string(buf);
    }

    inline void clearFor(const std::string& mountId) {
        if (mountId.empty()) return;

        std::string target = normalizeId(mountId);
        auto& entries = store();

        entries.erase(
            std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
                return e.mountId == target;
            }),
            entries.end()
        );
    }

    inline std::string execute(const std::string& input) {
    std::string id = "";

    size_t pos = input.find("-id=");
    if (pos != std::string::npos) {
        size_t start = pos + 4;
        size_t end = input.find(' ', start);

        if (end == std::string::npos)
            id = input.substr(start);
        else
            id = input.substr(start, end - start);

        // quitar comillas
        if (id.size() >= 2 && 
           ((id.front() == '"' && id.back() == '"') ||
            (id.front() == '\'' && id.back() == '\''))) {
            id = id.substr(1, id.size() - 2);
        }
    }

    if (id.empty()) return "Error: journaling requiere -id";

    std::string target = normalizeId(id);

    std::vector<Entry> filtered;
    for (const auto& e : store()) {
        if (e.mountId == target) {
            filtered.push_back(e);
        }
    }

    std::ostringstream out;
    out << "\n=== JOURNALING ===\n";
    out << "Partición: " << id << "\n";

    if (filtered.empty()) {
        out << "Sin transacciones registradas.\n";
        return out.str();
    }

    const size_t wOp = 12;
    const size_t wPath = 28;
    const size_t wContent = 30;
    const size_t wDate = 16;

    auto sep = [&]() {
        out << "+"
            << std::string(wOp + 2, '-') << "+"
            << std::string(wPath + 2, '-') << "+"
            << std::string(wContent + 2, '-') << "+"
            << std::string(wDate + 2, '-') << "+\n";
    };

    sep();
    out << "| " << fit("Operacion", wOp)
        << " | " << fit("Path", wPath)
        << " | " << fit("Contenido", wContent)
        << " | " << fit("Fecha", wDate) << " |\n";
    sep();

    for (const auto& e : filtered) {
        out << "| " << fit(e.operation, wOp)
            << " | " << fit(e.path, wPath)
            << " | " << fit(e.content, wContent)
            << " | " << fit(dateTime(e.when), wDate) << " |\n";
    }

    sep();
    out << "Total: " << filtered.size() << " transaccion(es)\n";

    return out.str();
}

}

#endif