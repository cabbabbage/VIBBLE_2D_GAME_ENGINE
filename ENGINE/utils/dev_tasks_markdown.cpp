#include "dev_tasks_markdown.hpp"
#include "core/manifest/manifest_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace fs = std::filesystem;

namespace {
constexpr const char* kFileName = "DEV_TASKS.md";
constexpr const char* kTitle = "# Dev Tasks";
constexpr const char* kLane1 = "## Pending — Cline Description";
constexpr const char* kLane2 = "## Pending — Fix Verification";
constexpr const char* kLane3 = "## Completed";

std::string status_heading(DevTaskStatus s) {
    switch (s) {
        case DevTaskStatus::PendingClineDescription: return kLane1;
        case DevTaskStatus::PendingFixVerification:  return kLane2;
        case DevTaskStatus::Completed:               return kLane3;
    }
    return kLane1;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = s.find(from, start_pos)) != std::string::npos) {
        s.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

}

DevTasksMarkdown::DevTasksMarkdown() = default;

std::string DevTasksMarkdown::tasks_markdown_path() const {
    const fs::path manifest_root = fs::absolute(fs::path(manifest::manifest_path()).parent_path());
    return (manifest_root / kFileName).string();
}

bool DevTasksMarkdown::ensure_initialized() {
    const std::string path = tasks_markdown_path();
    if (fs::exists(path)) {
        return true;
    }
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << kTitle << "\n\n";
    out << kLane1 << "\n\n";
    out << kLane2 << "\n\n";
    out << kLane3 << "\n\n";
    out.close();
    return true;
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && ((s.front()=='"' && s.back()=='"') || (s.front()=='\'' && s.back()=='\''))) {
        return s.substr(1, s.size()-2);
    }
    return s;
}

DevTaskStatus DevTasksMarkdown::parse_status(const std::string& s) {
    const std::string t = trim(s);
    if (t == "pending_cline_description") return DevTaskStatus::PendingClineDescription;
    if (t == "pending_fix_verification") return DevTaskStatus::PendingFixVerification;
    if (t == "completed") return DevTaskStatus::Completed;
    return DevTaskStatus::PendingClineDescription;
}

std::string DevTasksMarkdown::to_string(DevTaskStatus status) {
    switch (status) {
        case DevTaskStatus::PendingClineDescription: return "pending_cline_description";
        case DevTaskStatus::PendingFixVerification:  return "pending_fix_verification";
        case DevTaskStatus::Completed:               return "completed";
    }
    return "pending_cline_description";
}

std::string DevTasksMarkdown::today_yyyy_mm_dd() const {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    std::time_t t = clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16] = {0};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

std::string DevTasksMarkdown::trim(const std::string& s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    while (j > i && std::isspace(static_cast<unsigned char>(s[j-1]))) --j;
    return s.substr(i, j - i);
}

bool DevTasksMarkdown::load(std::vector<DevTask>& out_tasks) {
    out_tasks.clear();
    if (!ensure_initialized()) return false;

    const std::string path = tasks_markdown_path();
    std::ifstream in(path);
    if (!in.is_open()) return false;

    enum class Lane { None, L1, L2, L3 } lane = Lane::None;
    std::string line;
    DevTask current{};
    bool in_meta = false;
    auto commit = [&]() {
        if (!current.id.empty()) {

            if (current.title.empty()) current.title = current.cline_description;
            out_tasks.push_back(current);
            current = DevTask{};
        }
};

    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;
        if (t == kLane1) { lane = Lane::L1; continue; }
        if (t == kLane2) { lane = Lane::L2; continue; }
        if (t == kLane3) { lane = Lane::L3; continue; }

        if (starts_with(t, "- [")) {

            commit();

            bool checked = false;
            if (starts_with(t, "- [x]")) checked = true;

            const auto rb = t.find("] ");
            std::string title = (rb != std::string::npos) ? trim(t.substr(rb + 2)) : std::string{};
            current = DevTask{};
            current.title = title;
            current.status = checked ? DevTaskStatus::Completed
                                     : (lane == Lane::L2 ? DevTaskStatus::PendingFixVerification : DevTaskStatus::PendingClineDescription);
            continue;
        }

        if (t == "<!--") {
            in_meta = true;
            continue;
        }
        if (t == "-->") {
            in_meta = false;

            continue;
        }

        if (in_meta) {

            const auto colon = t.find(":");
            if (colon == std::string::npos) continue;
            std::string key = trim(t.substr(0, colon));
            std::string value = trim(t.substr(colon + 1));

            value = unquote(value);

            if (key == "id") current.id = value;
            else if (key == "status") current.status = parse_status(value);
            else if (key == "assignee") current.assignee = value;
            else if (key == "created") current.created = value;
            else if (key == "files") {

                std::string s = value;
                if (!s.empty() && s.front()=='[' && s.back()==']') {
                    s = s.substr(1, s.size()-2);
                }
                current.files.clear();
                std::stringstream ss(s);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    tok = trim(tok);
                    tok = unquote(tok);
                    if (!tok.empty()) current.files.push_back(tok);
                }
            } else if (key == "cline_description") current.cline_description = value;
            else if (key == "notes") current.notes = value;
        }
    }

    commit();
    return true;
}

bool DevTasksMarkdown::save(const std::vector<DevTask>& tasks) {
    if (!ensure_initialized()) return false;
    const std::string path = tasks_markdown_path();
    const std::string tmp = path + ".tmp";

    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) return false;

    out << kTitle << "\n\n";

    auto write_section = [&](DevTaskStatus section) {
        out << status_heading(section) << "\n\n";
        for (const DevTask& t : tasks) {
            if (t.status != section) continue;
            const bool checked = (t.status == DevTaskStatus::Completed);
            out << "- [" << (checked ? 'x' : ' ') << "] " << t.title << "\n";
            out << "<!--\n";
            out << "id: " << t.id << "\n";
            out << "status: " << to_string(t.status) << "\n";
            out << "assignee: " << t.assignee << "\n";
            out << "created: " << t.created << "\n";

            out << "files: [";
            for (size_t i = 0; i < t.files.size(); ++i) {
                std::string f = t.files[i];
                replace_all(f, "\"", "\\\"");
                out << '\"' << f << '\"';
                if (i + 1 < t.files.size()) out << ", ";
            }
            out << "]\n";

            std::string desc = t.cline_description;
            replace_all(desc, "\n", "\\n");
            std::string notes = t.notes;
            replace_all(notes, "\n", "\\n");
            out << "cline_description: " << desc << "\n";
            out << "notes: " << notes << "\n";
            out << "-->\n\n";
        }
};

    write_section(DevTaskStatus::PendingClineDescription);
    write_section(DevTaskStatus::PendingFixVerification);
    write_section(DevTaskStatus::Completed);

    out.close();

    try {
        fs::rename(tmp, path);
        return true;
    } catch (const fs::filesystem_error&) {
        fs::remove(tmp);
        return false;
    }
}

std::string DevTasksMarkdown::next_id_for_today(const std::vector<DevTask>& tasks) const {
    const std::string today = today_yyyy_mm_dd();
    int max_n = 0;
    for (const auto& t : tasks) {

        if (t.id.size() >= 15 && t.id.rfind("T-", 0) == 0) {
            if (t.id.size() >= 13 && t.id.substr(2, 10) == today) {

                const auto last_dash = t.id.find_last_of('-');
                if (last_dash != std::string::npos && last_dash + 1 < t.id.size()) {
                    try {
                        int n = std::stoi(t.id.substr(last_dash + 1));
                        if (n > max_n) max_n = n;
                    } catch (...) {

                    }
                }
            }
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "T-%s-%03d", today.c_str(), max_n + 1);
    return std::string(buf);
}
