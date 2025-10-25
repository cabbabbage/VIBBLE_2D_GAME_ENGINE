#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace devmode::core {

class DevJsonStore {
public:
    static DevJsonStore& instance();

    nlohmann::json load(const std::filesystem::path& path);
    void submit(const std::filesystem::path& path, const nlohmann::json& data, int indent = 4);
    void flush_all();
    void shutdown();

private:
    DevJsonStore();
    ~DevJsonStore();

    DevJsonStore(const DevJsonStore&) = delete;
    DevJsonStore& operator=(const DevJsonStore&) = delete;

    struct Impl;
    Impl* impl_;
};

}
