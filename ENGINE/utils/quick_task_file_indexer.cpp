#include "quick_task_file_indexer.hpp"
#include "core/manifest/manifest_loader.hpp"
#include <algorithm>
#include <cctype>
#include <array>

QuickTaskFileIndexer::QuickTaskFileIndexer() = default;

bool QuickTaskFileIndexer::index_repo_files() {
    if (indexed_) {
        return true; // Already indexed
    }

    try {
        const fs::path manifest_root = fs::absolute(fs::path(manifest::manifest_path()).parent_path());

        indexed_files_.clear();

        // Directories to exclude
        static const std::array<std::string, 6> excluded_dirs = {
            ".git", "build", "external", "vcpkg", "TEMP", "vcpkg_installed"
        };

        // File extensions to exclude (primarily binary or generated files)
        static const std::array<std::string, 9> excluded_extensions = {
            ".log", ".tmp", ".bak", ".obj", ".o", ".lib", ".a", ".so", ".dll"
        };

        // File names to exclude (common build/log files)
        static const std::array<std::string, 3> excluded_files = {
            "log.txt", "CMakeCache.txt", "cmake_install.cmake"
        };

        // Recursively iterate through directory
        for (const auto& entry : fs::recursive_directory_iterator(manifest_root,
                                                                fs::directory_options::skip_permission_denied | // Use | not or
                                                                fs::directory_options::follow_directory_symlink)) {
            try {
                // Get relative path first
                fs::path relative_path = fs::relative(entry.path(), manifest_root);

                // Check if any parent directory in the relative path is excluded
                bool in_excluded_path = false;
                for (const auto& part : relative_path) {
                    if (should_exclude_directory(part)) {
                        in_excluded_path = true;
                        break;
                    }
                }
                if (in_excluded_path) {
                    continue;
                }

                // Skip directories
                if (entry.is_directory()) {
                    continue;
                }

                // Only index regular files
                if (!entry.is_regular_file()) {
                    continue;
                }

                std::string relative_str = relative_path.generic_string();

                // Convert to lowercase for easier matching
                std::string filename_lower = relative_path.filename().string();
                std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(),
                              [](unsigned char c){ return std::tolower(c); });
                std::string extension_lower = relative_path.extension().string();
                std::transform(extension_lower.begin(), extension_lower.end(), extension_lower.begin(),
                              [](unsigned char c){ return std::tolower(c); });

                // Check file extension exclusions
                bool extension_excluded = false;
                for (const auto& ext : excluded_extensions) {
                    if (extension_lower == ext) {
                        extension_excluded = true;
                        break;
                    }
                }
                if (extension_excluded) continue;

                // Check filename exclusions
                bool file_excluded = false;
                for (const auto& excluded_name : excluded_files) {
                    if (filename_lower == excluded_name) {
                        file_excluded = true;
                        break;
                    }
                }
                if (file_excluded) continue;

                // Add to indexed list
                indexed_files_.push_back(relative_str);

            } catch (const fs::filesystem_error&) {
                // Skip problematic files/directories
                continue;
            } catch (const std::exception&) {
                // Skip other errors
                continue;
            }
        }

        // Sort the files for consistent ordering
        std::sort(indexed_files_.begin(), indexed_files_.end());

        indexed_ = true;
        return true;

    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> QuickTaskFileIndexer::filter_files(const std::string& search_term) const {
    if (search_term.empty()) {
        return get_all_files();
    }

    std::vector<std::string> filtered;

    // Convert search term to lowercase for case-insensitive matching
    std::string search_lower = search_term;
    std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(),
                  [](unsigned char c){ return std::tolower(c); });

    for (const auto& file_path : indexed_files_) {
        std::string path_lower = file_path;
        std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(),
                      [](unsigned char c){ return std::tolower(c); });

        // Simple substring match
        if (path_lower.find(search_lower) != std::string::npos) {
            filtered.push_back(file_path);
        }
    }

    return filtered;
}

bool QuickTaskFileIndexer::should_exclude_directory(const fs::path& dir) const {
    // Convert to lowercase for case-insensitive comparison
    std::string dirname_lower = dir.filename().string();
    std::transform(dirname_lower.begin(), dirname_lower.end(), dirname_lower.begin(),
                  [](unsigned char c){ return std::tolower(c); });

    static const std::array<std::string, 7> excluded_dir_names = {
        ".git", "build", "external", "vcpkg", "temp", "vcpkg_installed", "cache"
    };

    for (const auto& excluded : excluded_dir_names) {
        if (dirname_lower == excluded) {
            return true;
        }
    }

    return false;
}
