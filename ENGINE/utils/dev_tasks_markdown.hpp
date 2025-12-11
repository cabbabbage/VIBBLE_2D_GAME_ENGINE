#pragma once

#include <string>
#include <vector>
#include <optional>

enum class DevTaskStatus {
    PendingClineDescription,
    PendingFixVerification,
    Completed
};

struct DevTask {
    std::string id;
    DevTaskStatus status{DevTaskStatus::PendingClineDescription};
    std::string assignee;
    std::string created;
    std::vector<std::string> files;
    std::string cline_description;
    std::string notes;
    std::string title;
};

class DevTasksMarkdown {
public:
    DevTasksMarkdown();

    bool ensure_initialized();

    bool load(std::vector<DevTask>& out_tasks);

    bool save(const std::vector<DevTask>& tasks);

    std::string next_id_for_today(const std::vector<DevTask>& tasks) const;

    std::string tasks_markdown_path() const;

    static std::string to_string(DevTaskStatus status);
    static DevTaskStatus parse_status(const std::string& s);

private:

    std::string today_yyyy_mm_dd() const;
    static std::string trim(const std::string& s);
};

