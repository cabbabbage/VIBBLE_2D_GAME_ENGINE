#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class TagLibrary {
public:
    static TagLibrary& instance();

    const std::vector<std::string>& tags();

    void set_csv_path(std::string path);

    void invalidate();

    bool remove_tag(std::string_view value);

private:
    TagLibrary();
    void ensure_loaded();
    void load_from_disk();
    bool write_to_disk(const std::vector<std::string>& tags) const;

    std::filesystem::path csv_path_;
    std::vector<std::string> tags_;
    std::filesystem::file_time_type last_write_time_{};
    bool loaded_ = false;
};
