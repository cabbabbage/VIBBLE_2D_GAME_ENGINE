#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vibble {

class RebuildQueueCoordinator {
public:
    RebuildQueueCoordinator();

    void request_full_asset_rebuild() const;
    void request_asset(const std::string& asset_name,
                       const std::vector<std::string>& animations = {}) const;
    void request_animation(const std::string& asset_name, const std::string& animation) const;

    void request_full_light_rebuild() const;
    void request_light(const std::string& asset_name) const;

    bool has_pending_asset_work() const;
    bool has_pending_light_work() const;

    bool run_asset_tool(const std::string& command_prefix = std::string()) const;
    bool run_light_tool(const std::string& command_prefix = std::string()) const;

    const std::filesystem::path& queue_path() const { return queue_path_; }

private:
    using json = nlohmann::json;

    std::filesystem::path repo_root_;
    std::filesystem::path queue_path_;
    std::filesystem::path manifest_path_;
    std::filesystem::path cache_root_;

    json load_queue() const;
    void write_queue(const json& data) const;

    void ensure_core_fields(json& data) const;
    void ensure_assets_array(json& data) const;
    void ensure_lights_array(json& data) const;

    bool merge_asset_entry(json& data,
                           const std::string& asset_name,
                           const std::vector<std::string>& animations) const;
    bool merge_light_entry(json& data, const std::string& asset_name) const;

    bool has_pending(const json& data, const char* key) const;

    bool run_python_script(const std::filesystem::path& script,
                           const std::string& command_prefix) const;
};

} // namespace vibble
