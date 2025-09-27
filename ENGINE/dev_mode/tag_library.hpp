#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Provides read-through access to the shared tag catalogue stored in ENGINE/tags.csv.
class TagLibrary {
public:
    static TagLibrary& instance();

    // Returns the normalized, deduplicated list of known tags. Reloads if the
    // underlying CSV changes on disk.
    const std::vector<std::string>& tags();

    // Test hook: override the CSV path (e.g., for unit tests).
    void set_csv_path(std::string path);

    // Clears the cached data so the next call to tags() forces a reload.
    void invalidate();

private:
    TagLibrary();
    void ensure_loaded();
    void load_from_disk();

    std::filesystem::path csv_path_;
    std::vector<std::string> tags_;
    std::filesystem::file_time_type last_write_time_{};
    bool loaded_ = false;
};
