#include "simple_tasks_markdown.hpp"
#include "core/manifest/manifest_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
static std::string trim(const std::string& s) {
    size_t i = 0, j = s.size();
    while (i < j && isspace(static_cast<unsigned char>(s[i]))) ++i;
    while (j > i && isspace(static_cast<unsigned char>(s[j-1]))) --j;
    return s.substr(i, j - i);
}

static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && ((s.front()=='"' && s.back()=='"') || (s.front()=='\'' && s.back()=='\''))) {
        return s.substr(1, s.size()-2);
    }
    return s;
}
}

SimpleTasksFile::SimpleTasksFile(std::string file_name, std::string title)
    : file_name_(std::move(file_name)), title_(std::move(title)) {}

std::string SimpleTasksFile::absolute_path() const {
    const fs::path root = fs::absolute(fs::path(manifest::manifest_path()).parent_path());
    return (root / file_name_).string();
}

bool SimpleTasksFile::ensure_initialized() const {
    const std::string path = absolute_path();
    if (fs::exists(path)) return true;
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "# " << title_ << "\n\n";
    out.close();
    return true;
}

bool SimpleTasksFile::load(std::vector<SimpleTask>& out) const {
    out.clear();
    if (!ensure_initialized()) return false;
    std::ifstream in(absolute_path());
    if (!in.is_open()) return false;

    std::string line;
    SimpleTask current{};
    bool have_current = false;
    bool in_meta = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.rfind("- ", 0) == 0) {
            if (have_current) {
                out.push_back(current);
                current = SimpleTask{};
            }
            current.description = trim(t.substr(2));
            current.status = "pending";
            have_current = true;
            in_meta = false;
            continue;
        }
        if (t == "<!--") { in_meta = true; continue; }
        if (t == "-->") { in_meta = false; continue; }
        if (in_meta) {
            const auto colon = t.find(':');
            if (colon == std::string::npos) continue;
            std::string key = trim(t.substr(0, colon));
            std::string value = trim(t.substr(colon + 1));
            value = unquote(value);
            if (key == "assignee") current.assignee = value;
            else if (key == "assigner") current.assigner = value;
            else if (key == "status") current.status = value;
        }
    }
    if (have_current) out.push_back(current);
    return true;
}

bool SimpleTasksFile::save(const std::vector<SimpleTask>& tasks) const {
    if (!ensure_initialized()) return false;
    const std::string path = absolute_path();
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) return false;
    out << "# " << title_ << "\n\n";
    for (const auto& t : tasks) {
        out << "- " << t.description << "\n";
        out << "<!--\n";
        out << "assignee: " << t.assignee << "\n";
        out << "assigner: " << t.assigner << "\n";
        out << "status: " << (t.status.empty() ? "pending" : t.status) << "\n";
        out << "-->\n\n";
    }
    out.close();
    try {
        fs::rename(tmp, path);
        return true;
    } catch (const fs::filesystem_error&) {
        fs::remove(tmp);
        return false;
    }
}

