#pragma once

#include <string>
#include <vector>

// Minimal task model for simple Markdown storage
struct SimpleTask {
    std::string description;
    std::string assignee; // Any, Cal, Kaden, Haden, Cline
    std::string assigner; // Cal, Kaden, Haden
    std::string status;   // "pending" only for now
};

// Simple Markdown-backed task list stored at repo root
// Format:
//   # <Title>
//   - <description>\n
//     <!--
//     assignee: <name>\n
//     assigner: <name>\n
//     status: pending\n
//     -->
class SimpleTasksFile {
public:
    // file_name like "DEV_TASKS.md", title like "Dev Tasks"
    SimpleTasksFile(std::string file_name, std::string title);

    // Ensure file exists with title header
    bool ensure_initialized() const;

    // Absolute path
    std::string absolute_path() const;

    // Load all tasks
    bool load(std::vector<SimpleTask>& out) const;

    // Save tasks (atomic write via temp + rename)
    bool save(const std::vector<SimpleTask>& tasks) const;

private:
    std::string file_name_;
    std::string title_;
};

