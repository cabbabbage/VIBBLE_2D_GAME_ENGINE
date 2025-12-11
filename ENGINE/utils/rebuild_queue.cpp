#include "utils/rebuild_queue.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

#include "core/manifest/manifest_loader.hpp"
#include "utils/log.hpp"

namespace fs = std::filesystem;

namespace vibble {
using json = nlohmann::json;
namespace {

fs::path default_repo_root() {
    fs::path manifest = manifest::manifest_path();
    if (manifest.empty()) {
        return fs::current_path();
    }
    return fs::absolute(manifest).parent_path();
}

fs::path script_path(const fs::path& repo_root, const std::string& script_name) {
    return repo_root / "tools" / script_name;
}

}

RebuildQueueCoordinator::RebuildQueueCoordinator() {
    repo_root_ = default_repo_root();
    manifest_path_ = fs::absolute(manifest::manifest_path());
    cache_root_ = repo_root_ / "cache";
}

void RebuildQueueCoordinator::request_full_asset_rebuild() const {
    mark_all_frames_for_rebuild();
}

void RebuildQueueCoordinator::request_asset(const std::string& asset_name,
                                            const std::vector<std::string>& animations) const {
    if (asset_name.empty()) {
        return;
    }
    if (animations.empty()) {
        mark_asset_for_rebuild(asset_name);
        return;
    }
    for (const auto& anim : animations) {
        request_animation(asset_name, anim);
    }
}

void RebuildQueueCoordinator::request_animation(const std::string& asset_name,
                                                const std::string& animation) const {
    if (asset_name.empty() || animation.empty()) {
        return;
    }
    mark_animation_for_rebuild(asset_name, animation);
}

void RebuildQueueCoordinator::request_frame(const std::string& asset_name,
                                            const std::string& animation,
                                            int frame_index) const {
    if (asset_name.empty() || animation.empty() || frame_index < 0) {
        return;
    }
    mark_frame_for_rebuild(asset_name, animation, frame_index);
}

void RebuildQueueCoordinator::request_full_light_rebuild() const {
    mark_all_lights_for_rebuild();
}

void RebuildQueueCoordinator::request_light(const std::string& asset_name) const {
    if (asset_name.empty()) {
        return;
    }
    mark_asset_lights_for_rebuild(asset_name);
}

void RebuildQueueCoordinator::request_light_entry(const std::string& asset_name, int light_index) const {
    if (asset_name.empty() || light_index < 0) {
        return;
    }
    mark_light_for_rebuild(asset_name, light_index);
}

bool RebuildQueueCoordinator::has_pending_asset_work() const {
    return manifest_has_needs_rebuild();
}

bool RebuildQueueCoordinator::has_pending_light_work() const {
    return manifest_has_light_needs_rebuild();
}

bool RebuildQueueCoordinator::run_asset_tool(const std::string& command_prefix) const {
    const fs::path script = script_path(repo_root_, "asset_tool.py");
    return run_python_script(script, {}, command_prefix);
}

bool RebuildQueueCoordinator::run_light_tool(const std::string& command_prefix) const {
    const fs::path script = script_path(repo_root_, "light_tool.py");
    return run_python_script(script, {}, command_prefix);
}

void RebuildQueueCoordinator::mark_all_frames_for_rebuild() const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script, {"all", "--manifest", manifest_path_.string()}, "");
}

void RebuildQueueCoordinator::mark_asset_for_rebuild(const std::string& asset_name) const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script, {"asset", asset_name, "--manifest", manifest_path_.string()}, "");
}

void RebuildQueueCoordinator::mark_animation_for_rebuild(const std::string& asset_name,
                                                         const std::string& animation) const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script,
                      {"animation", asset_name, animation, "--manifest", manifest_path_.string()},
                      "");
}

void RebuildQueueCoordinator::mark_frame_for_rebuild(const std::string& asset_name,
                                                     const std::string& animation,
                                                     int frame_index) const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script,
                      {"frame", asset_name, animation, std::to_string(frame_index), "--manifest", manifest_path_.string()},
                      "");
}

void RebuildQueueCoordinator::mark_light_for_rebuild(const std::string& asset_name, int light_index) const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script,
                      {"lighting_light", asset_name, std::to_string(light_index), "--manifest", manifest_path_.string()},
                      "");
}

void RebuildQueueCoordinator::mark_asset_lights_for_rebuild(const std::string& asset_name) const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script, {"lighting_asset", asset_name, "--manifest", manifest_path_.string()}, "");
}

void RebuildQueueCoordinator::mark_all_lights_for_rebuild() const {
    const fs::path script = script_path(repo_root_, "set_rebuild_values.py");
    run_python_script(script, {"lighting_all", "--manifest", manifest_path_.string()}, "");
}

bool RebuildQueueCoordinator::manifest_has_needs_rebuild() const {
    std::ifstream in(manifest_path_);
    if (!in.good()) {
        return false;
    }
    json manifest_json;
    try {
        in >> manifest_json;
    } catch (...) {
        return false;
    }
    auto assets_it = manifest_json.find("assets");
    if (assets_it == manifest_json.end() || !assets_it->is_object()) {
        return false;
    }
    for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        auto anims_it = it->find("animations");
        if (anims_it == it->end() || !anims_it->is_object()) {
            continue;
        }
        json anims = *anims_it;
        if (anims.contains("animations") && anims["animations"].is_object()) {
            anims = anims["animations"];
        }
        for (auto a_it = anims.begin(); a_it != anims.end(); ++a_it) {
            if (!a_it->is_object()) {
                continue;
            }
            const json& anim = *a_it;
            auto frames_it = anim.find("frames");
            if (frames_it == anim.end() || !frames_it->is_array()) {
                continue;
            }
            for (const auto& frame_entry : *frames_it) {
                if (frame_entry.is_object()) {
                    auto flag = frame_entry.find("needs_rebuild");
                    if (flag != frame_entry.end() && flag->is_boolean() && flag->get<bool>()) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool RebuildQueueCoordinator::manifest_has_light_needs_rebuild() const {
    std::ifstream in(manifest_path_);
    if (!in.good()) {
        return false;
    }
    json manifest_json;
    try {
        in >> manifest_json;
    } catch (...) {
        return false;
    }
    auto assets_it = manifest_json.find("assets");
    if (assets_it == manifest_json.end() || !assets_it->is_object()) {
        return false;
    }
    for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        auto lights_it = it->find("lighting_info");
        if (lights_it == it->end()) {
            continue;
        }
        if (lights_it->is_object()) {
            const auto& light = *lights_it;
            auto flag = light.find("needs_rebuild");
            if (flag != light.end() && flag->is_boolean() && flag->get<bool>()) {
                return true;
            }
        }
        if (!lights_it->is_array()) {
            continue;
        }
        for (const auto& light : *lights_it) {
            if (!light.is_object()) {
                continue;
            }
            auto flag = light.find("needs_rebuild");
            if (flag != light.end() && flag->is_boolean() && flag->get<bool>()) {
                return true;
            }
        }
    }
    return false;
}

bool RebuildQueueCoordinator::run_python_script(const fs::path& script,
                                                const std::vector<std::string>& args,
                                                const std::string& command_prefix) const {
    if (!fs::exists(script)) {
        vibble::log::warn(std::string{"Missing script: "} + script.string());
        return false;
    }

    std::string command = "python \"" + script.string() + "\"";
    for (const auto& arg : args) {
        command += " \"" + arg + "\"";
    }

    std::string full_command = command_prefix.empty() ? command : (command_prefix + command);
    vibble::log::info(std::string{"[RebuildQueue] Running "} + script.filename().string());
    int ret = std::system(full_command.c_str());
    if (ret != 0) {
        vibble::log::warn(std::string{"[RebuildQueue] Script exited with code "} + std::to_string(ret));
        return false;
    }
    return true;
}

bool RebuildQueueCoordinator::validate_manifest_cache(const std::string& command_prefix) const {
    const fs::path script = script_path(repo_root_, "cache_validator.py");
    return run_python_script(script, {"--manifest", manifest_path_.string()}, command_prefix);
}

}
