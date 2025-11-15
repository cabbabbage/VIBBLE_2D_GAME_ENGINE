#include "tag_library.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <iterator>

#include "tag_utils.hpp"

TagLibrary& TagLibrary::instance() {
    static TagLibrary lib;
    return lib;
}

TagLibrary::TagLibrary() {
#ifdef PROJECT_ROOT
    csv_path_ = std::filesystem::path(PROJECT_ROOT) / "ENGINE" / "tags.csv";
#else
    csv_path_ = std::filesystem::path("ENGINE") / "tags.csv";
#endif
}

const std::vector<std::string>& TagLibrary::tags() {
    ensure_loaded();
    return tags_;
}

void TagLibrary::set_csv_path(std::string path) {
    csv_path_ = path;
    invalidate();
}

void TagLibrary::invalidate() {
    loaded_ = false;
    tags_.clear();
    last_write_time_ = {};
}

void TagLibrary::ensure_loaded() {
    if (!loaded_) {
        load_from_disk();
        return;
    }
    std::error_code ec;
    auto stamp = std::filesystem::exists(csv_path_, ec)
                   ? std::filesystem::last_write_time(csv_path_, ec)
                   : std::filesystem::file_time_type{};
    if (!ec && stamp != last_write_time_) {
        load_from_disk();
    }
}

void TagLibrary::load_from_disk() {
    std::unordered_set<std::string> unique;
    std::vector<std::string> ordered;

    std::error_code ec;
    if (!std::filesystem::exists(csv_path_, ec)) {
        tags_.clear();
        loaded_ = true;
        last_write_time_ = {};
        return;
    }

    std::ifstream in(csv_path_);
    if (!in) {
        tags_.clear();
        loaded_ = true;
        last_write_time_ = {};
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        auto comma = line.find_first_of(",;\t");
        std::string token = comma == std::string::npos ? line : line.substr(0, comma);
        if (!token.empty() && token.front() == '#') continue;
        auto value = tag_utils::normalize(token);
        if (value.empty()) continue;
        if (unique.insert(value).second) {
            ordered.push_back(std::move(value));
        }
    }

    std::sort(ordered.begin(), ordered.end());
    tags_ = std::move(ordered);
    loaded_ = true;
    std::error_code stamp_ec;
    last_write_time_ = std::filesystem::exists(csv_path_, stamp_ec)
                           ? std::filesystem::last_write_time(csv_path_, stamp_ec)
                           : std::filesystem::file_time_type{};
}

bool TagLibrary::write_to_disk(const std::vector<std::string>& tags) const {
    std::error_code dir_ec;
    const auto parent = csv_path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, dir_ec);
    }

    std::ofstream out(csv_path_, std::ios::trunc);
    if (!out) {
        std::cerr << "[TagLibrary] Failed to open '" << csv_path_ << "' for writing\n";
        return false;
    }

    for (std::size_t i = 0; i < tags.size(); ++i) {
        out << tags[i];
        if (i + 1 < tags.size()) {
            out << '\n';
        }
    }

    out.flush();
    if (!out.good()) {
        std::cerr << "[TagLibrary] Failed to write tags to '" << csv_path_ << "'\n";
        return false;
    }

    return true;
}

bool TagLibrary::remove_tag(std::string_view value) {
    auto normalized = tag_utils::normalize(value);
    if (normalized.empty()) {
        return false;
    }

    ensure_loaded();
    auto it = std::find(tags_.begin(), tags_.end(), normalized);
    if (it == tags_.end()) {
        return false;
    }

    std::vector<std::string> updated = tags_;
    updated.erase(updated.begin() + std::distance(tags_.begin(), it));

    if (!write_to_disk(updated)) {
        return false;
    }

    tags_ = std::move(updated);
    loaded_ = true;

    std::error_code stamp_ec;
    last_write_time_ = std::filesystem::exists(csv_path_, stamp_ec)
                           ? std::filesystem::last_write_time(csv_path_, stamp_ec)
                           : std::filesystem::file_time_type{};
    return true;
}
