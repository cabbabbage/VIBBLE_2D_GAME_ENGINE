#pragma once

#include <string>
#include <vector>

struct SimpleTask {
    std::string description;
    std::string assignee;
    std::string assigner;
    std::string status;
};

class SimpleTasksFile {
public:

    SimpleTasksFile(std::string file_name, std::string title);

    bool ensure_initialized() const;

    std::string absolute_path() const;

    bool load(std::vector<SimpleTask>& out) const;

    bool save(const std::vector<SimpleTask>& tasks) const;

private:
    std::string file_name_;
    std::string title_;
};

