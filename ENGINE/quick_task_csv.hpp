#pragma once

#include <string>
#include <vector>

struct QuickTaskEntry {
    bool completed = false;
    bool prompt_fixed = false;
    std::vector<std::string> related_files;
    std::string original_prompt;
    std::string ai_fixed_prompt;
};

class QuickTaskCsvWriter {
public:
    QuickTaskCsvWriter();

    // Add a new task entry at the top of the CSV
    bool add_task_entry(const QuickTaskEntry& entry);

    // Read all existing entries (for validation)
    std::vector<QuickTaskEntry> read_existing_entries() const;

    // Ensure CSV header exists (create if missing)
    bool ensure_header();

private:
    std::string get_csv_path() const;

    // Escape CSV field according to standard rules
    std::string escape_csv_field(const std::string& field) const;

    // Write entries to file atomically (via temp file)
    bool write_entries_atomically(const std::vector<QuickTaskEntry>& entries) const;

    // Parse a single CSV line into fields
    std::vector<std::string> parse_csv_line(const std::string& line) const;
};
