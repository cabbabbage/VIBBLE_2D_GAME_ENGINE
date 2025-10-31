#include "quick_task_csv.hpp"
#include "core/manifest/manifest_loader.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

QuickTaskCsvWriter::QuickTaskCsvWriter() = default;

std::string QuickTaskCsvWriter::get_csv_path() const {
    const fs::path manifest_root = fs::absolute(fs::path(manifest::manifest_path()).parent_path());
    return (manifest_root / "user_tasks.csv").string();
}

bool QuickTaskCsvWriter::ensure_header() {
    const std::string csv_path = get_csv_path();
    if (fs::exists(csv_path)) {
        // Check if header exists and is correct
        std::ifstream infile(csv_path);
        if (infile.is_open()) {
            std::string first_line;
            if (std::getline(infile, first_line)) {
                // Basic check for CSV header
                if (first_line.find("completed,prompt_fixed,related_files,original_prompt,ai_fixed_prompt") != std::string::npos) {
                    return true;
                }
            }
        }
    }

    // Create file with header
    std::ofstream outfile(csv_path);
    if (outfile.is_open()) {
        outfile << "completed,prompt_fixed,related_files,original_prompt,ai_fixed_prompt\n";
        outfile.close();
        return true;
    }
    return false;
}

bool QuickTaskCsvWriter::add_task_entry(const QuickTaskEntry& entry) {
    // Ensure header exists
    if (!ensure_header()) {
        return false;
    }

    // Read existing entries
    std::vector<QuickTaskEntry> existing_entries = read_existing_entries();
    if (existing_entries.empty() && fs::exists(get_csv_path())) {
        // Header exists but no other entries, or error reading
    }

    // Prepend new entry
    existing_entries.insert(existing_entries.begin(), entry);

    // Write back atomically
    return write_entries_atomically(existing_entries);
}

std::vector<QuickTaskEntry> QuickTaskCsvWriter::read_existing_entries() const {
    std::vector<QuickTaskEntry> entries;
    const std::string csv_path = get_csv_path();

    std::ifstream infile(csv_path);
    if (!infile.is_open()) {
        return entries;
    }

    std::string line;
    bool first_line = true;
    while (std::getline(infile, line)) {
        if (first_line) {
            first_line = false;
            continue; // Skip header
        }

        auto fields = parse_csv_line(line);
        if (fields.size() != 5) {
            continue; // Malformed line
        }

        QuickTaskEntry entry;
        try {
            entry.completed = (fields[0] == "true");
            entry.prompt_fixed = (fields[1] == "true");
            // Parse JSON array for related_files
            nlohmann::json files_json = nlohmann::json::parse(fields[2]);
            if (files_json.is_array()) {
                for (const auto& file : files_json) {
                    if (file.is_string()) {
                        entry.related_files.push_back(file.get<std::string>());
                    }
                }
            }
            entry.original_prompt = fields[3];
            entry.ai_fixed_prompt = fields[4];
            entries.push_back(entry);
        } catch (const std::exception&) {
            // Skip malformed entries
            continue;
        }
    }

    return entries;
}

std::string QuickTaskCsvWriter::escape_csv_field(const std::string& field) const {
    // Check if field contains comma, quote, or newline
    bool needs_quotes = field.find('"') != std::string::npos ||
                       field.find(',') != std::string::npos ||
                       field.find('\n') != std::string::npos ||
                       field.find('\r') != std::string::npos;

    if (!needs_quotes) {
        return field;
    }

    std::string escaped = field;
    // Escape quotes by doubling them
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, "\"");
        pos += 2;
    }

    return "\"" + escaped + "\"";
}

bool QuickTaskCsvWriter::write_entries_atomically(const std::vector<QuickTaskEntry>& entries) const {
    const std::string csv_path = get_csv_path();
    const std::string temp_path = csv_path + ".tmp";

    std::ofstream outfile(temp_path);
    if (!outfile.is_open()) {
        return false;
    }

    // Write header
    outfile << "completed,prompt_fixed,related_files,original_prompt,ai_fixed_prompt\n";

    // Write each entry
    for (const auto& entry : entries) {
        outfile << (entry.completed ? "true" : "false") << ",";
        outfile << (entry.prompt_fixed ? "true" : "false") << ",";
        // Serialize related_files as JSON array
        nlohmann::json files_json = entry.related_files;
        outfile << escape_csv_field(files_json.dump()) << ",";
        outfile << escape_csv_field(entry.original_prompt) << ",";
        outfile << escape_csv_field(entry.ai_fixed_prompt) << "\n";
    }

    outfile.close();

    // Atomic move
    try {
        fs::rename(temp_path, csv_path);
        return true;
    } catch (const fs::filesystem_error&) {
        // Cleanup temp file
        fs::remove(temp_path);
        return false;
    }
}

std::vector<std::string> QuickTaskCsvWriter::parse_csv_line(const std::string& line) const {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == '"' && !in_quotes) {
            in_quotes = true;
        } else if (c == '"' && in_quotes) {
            // Check for escaped quote
            if (i + 1 < line.length() && line[i + 1] == '"') {
                current += '"';
                ++i; // Skip next quote
            } else {
                in_quotes = false;
            }
        } else if (c == ',' && !in_quotes) {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

    // Add final field
    fields.push_back(current);

    return fields;
}
