#include "tag_library.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

#include "tag_utils.hpp"

TagLibrary& TagLibrary::instance() {
    static TagLibrary lib;
    return lib;
}

TagLibrary::TagLibrary() {
    csv_path_ = std::filesystem::path("ENGINE") / "tags.csv";
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
        // If the CSV has multiple columns, take the first one.
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
