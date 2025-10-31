#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

class QuickTaskFileIndexer {
public:
    QuickTaskFileIndexer();

    // Initialize the file index from the repository root
    bool index_repo_files();

    // Get all file paths relative to repo root
    const std::vector<std::string>& get_all_files() const { return indexed_files_; }

    // Filter files containing the search term (case-insensitive)
    std::vector<std::string> filter_files(const std::string& search_term) const;

private:
    std::vector<std::string> indexed_files_;
    bool indexed_ = false;

    // Check if a directory should be excluded from indexing
    bool should_exclude_directory(const fs::path& dir) const;
};
