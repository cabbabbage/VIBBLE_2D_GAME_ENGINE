#include "utils/rebuild_queue.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "core/manifest/manifest_loader.hpp"
#include "utils/log.hpp"

namespace fs = std::filesystem;

namespace vibble {
namespace {

constexpr const char* kAssetsKey = "assets";
constexpr const char* kLightsKey = "lights";

fs::path default_repo_root() {
    fs::path manifest = manifest::manifest_path();
    if (manifest.empty()) {
        return fs::current_path();
    }
    return fs::absolute(manifest).parent_path();
}

} // namespace

RebuildQueueCoordinator::RebuildQueueCoordinator() {
    repo_root_ = default_repo_root();
    manifest_path_ = fs::absolute(manifest::manifest_path());
    cache_root_ = repo_root_ / "cache";
    queue_path_ = repo_root_ / "tools" / "rebuild_requests.json";
}

void RebuildQueueCoordinator::request_full_asset_rebuild() const {
    auto data = load_queue();
    ensure_core_fields(data);
    data[kAssetsKey] = json::array();
    write_queue(data);
}

void RebuildQueueCoordinator::request_asset(const std::string& asset_name,
                                            const std::vector<std::string>& animations) const {
    if (asset_name.empty()) {
        return;
    }
    auto data = load_queue();
    ensure_core_fields(data);
    ensure_assets_array(data);
    if (merge_asset_entry(data, asset_name, animations)) {
        write_queue(data);
    }
}

void RebuildQueueCoordinator::request_animation(const std::string& asset_name,
                                                const std::string& animation) const {
    if (animation.empty()) {
        request_asset(asset_name, {});
        return;
    }
    request_asset(asset_name, std::vector<std::string>{animation});
}

void RebuildQueueCoordinator::request_full_light_rebuild() const {
    auto data = load_queue();
    ensure_core_fields(data);
    data[kLightsKey] = json::array();
    write_queue(data);
}

void RebuildQueueCoordinator::request_light(const std::string& asset_name) const {
    if (asset_name.empty()) {
        return;
    }
    auto data = load_queue();
    ensure_core_fields(data);
    ensure_lights_array(data);
    if (merge_light_entry(data, asset_name)) {
        write_queue(data);
    }
}

bool RebuildQueueCoordinator::has_pending_asset_work() const {
    auto data = load_queue();
    if (has_pending(data, kAssetsKey)) {
        return true;
    }

    std::error_code ec;
    if (!fs::exists(cache_root_, ec)) {
        vibble::log::info("[RebuildQueue] Cache root missing; queueing full asset rebuild.");
        request_full_asset_rebuild();
        return true;
    }

    return false;
}

bool RebuildQueueCoordinator::has_pending_light_work() const {
    auto data = load_queue();
    if (has_pending(data, kLightsKey)) {
        return true;
    }

    std::error_code ec;
    if (!fs::exists(cache_root_, ec)) {
        vibble::log::info("[RebuildQueue] Cache root missing; queueing full light rebuild.");
        request_full_light_rebuild();
        return true;
    }

    return false;
}

bool RebuildQueueCoordinator::run_asset_tool(const std::string& command_prefix) const {
    const fs::path script = repo_root_ / "tools" / "asset_tool.py";
    return run_python_script(script, command_prefix);
}

bool RebuildQueueCoordinator::run_light_tool(const std::string& command_prefix) const {
    const fs::path script = repo_root_ / "tools" / "light_tool.py";
    return run_python_script(script, command_prefix);
}

RebuildQueueCoordinator::json RebuildQueueCoordinator::load_queue() const {
    json data;
    std::ifstream input(queue_path_);
    if (input.good()) {
        try {
            input >> data;
        } catch (...) {
            data = json::object();
        }
    } else {
        data = json::object();
    }
    ensure_core_fields(data);
    return data;
}

void RebuildQueueCoordinator::write_queue(const json& data) const {
    std::error_code ec;
    fs::create_directories(queue_path_.parent_path(), ec);
    const fs::path tmp = queue_path_.parent_path() / (queue_path_.filename().string() + ".tmp");
    {
        std::ofstream output(tmp, std::ios::trunc);
        output << data.dump(2) << '\n';
    }
    fs::rename(tmp, queue_path_, ec);
    if (ec) {
        fs::remove(queue_path_, ec);
        ec.clear();
        fs::rename(tmp, queue_path_, ec);
    }
    if (ec) {
        vibble::log::warn(std::string{"Failed to update rebuild queue: "} + ec.message());
    }
}

void RebuildQueueCoordinator::ensure_core_fields(json& data) const {
    if (!data.is_object()) {
        data = json::object();
    }
    data["version"] = data.contains("version") && data["version"].is_number_integer()
                            ? data["version"].get<int>()
                            : 1;
    data["manifest_path"] = manifest_path_.string();
    data["cache_root"] = cache_root_.string();

    if (!data.contains(kAssetsKey) ||
        (!data[kAssetsKey].is_array() && !data[kAssetsKey].is_null())) {
        data[kAssetsKey] = nullptr;
    }
    if (!data.contains(kLightsKey) ||
        (!data[kLightsKey].is_array() && !data[kLightsKey].is_null())) {
        data[kLightsKey] = nullptr;
    }
}

void RebuildQueueCoordinator::ensure_assets_array(json& data) const {
    auto& node = data[kAssetsKey];
    if (!node.is_array()) {
        node = json::array();
    }
}

void RebuildQueueCoordinator::ensure_lights_array(json& data) const {
    auto& node = data[kLightsKey];
    if (!node.is_array()) {
        node = json::array();
    }
}

bool RebuildQueueCoordinator::merge_asset_entry(json& data,
                                                const std::string& asset_name,
                                                const std::vector<std::string>& animations) const {
    auto& assets = data[kAssetsKey];
    if (!assets.is_array()) {
        assets = json::array();
    }

    auto it = std::find_if(assets.begin(), assets.end(), [&](const json& entry) {
        return entry.is_object() && entry.contains("name") && entry["name"].is_string() &&
               entry["name"].get<std::string>() == asset_name;
    });

    std::vector<std::string> sanitized;
    sanitized.reserve(animations.size());
    for (const auto& anim : animations) {
        if (!anim.empty()) {
            sanitized.push_back(anim);
        }
    }

    if (it == assets.end()) {
        json entry;
        entry["name"] = asset_name;
        if (sanitized.empty()) {
            entry["animations"] = json::array();
        } else {
            entry["animations"] = sanitized;
        }
        assets.push_back(entry);
        return true;
    }

    auto& anim_node = (*it)["animations"];
    if (!anim_node.is_array()) {
        anim_node = json::array();
    }

    if (sanitized.empty()) {
        anim_node = json::array();
        return true;
    }

    if (anim_node.empty()) {
        // Already scheduled for full asset rebuild.
        return false;
    }

    bool changed = false;
    for (const auto& anim : sanitized) {
        bool exists = std::any_of(anim_node.begin(), anim_node.end(), [&](const json& value) {
            return value.is_string() && value.get<std::string>() == anim;
        });
        if (!exists) {
            anim_node.push_back(anim);
            changed = true;
        }
    }

    return changed;
}

bool RebuildQueueCoordinator::merge_light_entry(json& data, const std::string& asset_name) const {
    auto& lights = data[kLightsKey];
    if (!lights.is_array()) {
        lights = json::array();
    }
    auto it = std::find_if(lights.begin(), lights.end(), [&](const json& entry) {
        if (entry.is_string()) {
            return entry.get<std::string>() == asset_name;
        }
        return entry.is_object() && entry.contains("name") && entry["name"].is_string() &&
               entry["name"].get<std::string>() == asset_name;
    });
    if (it != lights.end()) {
        return false;
    }
    lights.push_back(asset_name);
    return true;
}

bool RebuildQueueCoordinator::has_pending(const json& data, const char* key) const {
    auto it = data.find(key);
    return it != data.end() && it->is_array();
}

bool RebuildQueueCoordinator::run_python_script(const fs::path& script,
                                                const std::string& command_prefix) const {
    if (!fs::exists(script)) {
        vibble::log::warn(std::string{"Missing script: "} + script.string());
        return false;
    }

#if defined(_WIN32)
    std::string command = "python \"" + script.string() + "\"";
#else
    std::string command = "python \"" + script.string() + "\"";
#endif
    std::string full_command = command_prefix.empty() ? command : (command_prefix + command);
    vibble::log::info(std::string{"[RebuildQueue] Running "} + script.filename().string());
    int ret = std::system(full_command.c_str());
    if (ret != 0) {
        vibble::log::warn(std::string{"[RebuildQueue] Script exited with code "} + std::to_string(ret));
        return false;
    }
    return true;
}

} // namespace vibble
