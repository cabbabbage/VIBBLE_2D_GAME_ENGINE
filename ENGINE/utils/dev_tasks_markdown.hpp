#pragma once

#include <string>
#include <vector>
#include <optional>

// Simple Markdown-based dev tasks store persisted at repo root: DEV_TASKS.md

enum class DevTaskStatus {
    PendingClineDescription,
    PendingFixVerification,
    Completed
};

struct DevTask {
    std::string id;                  // T-YYYY-MM-DD-###
    DevTaskStatus status{DevTaskStatus::PendingClineDescription};
    std::string assignee;            // "" => CLINE task; "@name" => HUMAN task
    std::string created;             // YYYY-MM-DD
    std::vector<std::string> files;  // optional linked files
    std::string cline_description;   // editable text body used by Cline
    std::string notes;               // optional notes
    std::string title;               // checklist item text
};

class DevTasksMarkdown {
public:
    DevTasksMarkdown();

    // Ensure DEV_TASKS.md exists with the 3 lane headings
    bool ensure_initialized();

    // Load all tasks from DEV_TASKS.md
    bool load(std::vector<DevTask>& out_tasks);

    // Save tasks to DEV_TASKS.md (atomic write)
    bool save(const std::vector<DevTask>& tasks);

    // Generate next task id for today's date (based on existing tasks)
    std::string next_id_for_today(const std::vector<DevTask>& tasks) const;

    // Resolve absolute path to DEV_TASKS.md
    std::string tasks_markdown_path() const;

    static std::string to_string(DevTaskStatus status);
    static DevTaskStatus parse_status(const std::string& s);

private:
    // Internal helpers
    std::string today_yyyy_mm_dd() const;
    static std::string trim(const std::string& s);
};

