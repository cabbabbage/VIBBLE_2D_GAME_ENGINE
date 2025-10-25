#include "room_editor.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "asset/asset_utils.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "dev_mode/room_editor_map_info.hpp"
#include "dev_mode/asset_info_ui.hpp"
#include "dev_mode/dev_controls_persistence.hpp"
#include "map_layers_common.hpp"
#include "dev_mode/asset_library_ui.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/DockableCollapsible.hpp"
#include "spawn_group_config/SpawnGroupConfig.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "dev_mode/dev_footer_bar.hpp"
#include "room_config/room_configurator.hpp"
#include "dev_mode/FloatingDockableManager.hpp"
#include "dev_mode/widgets.hpp"
#include "dm_styles.hpp"
#include "render/camera.hpp"
#include "map_generation/room.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/check.hpp"
#include "spawn/methods/center_spawner.hpp"
#include "spawn/methods/exact_spawner.hpp"
#include "spawn/methods/perimeter_spawner.hpp"
#include "spawn/methods/edge_spawner.hpp"
#include "spawn/methods/percent_spawner.hpp"
#include "spawn/methods/random_spawner.hpp"
#include "spawn/spawn_context.hpp"
#include "utils/input.hpp"
#include "util/grid.hpp"
#include "util/grid_occupancy.hpp"
#include "utils/map_grid_settings.hpp"
#include "utils/relative_room_position.hpp"
#include "map_generation/map_layers_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <iostream>
#include <cctype>
#include <limits>
#include <random>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <SDL_log.h>

#include <nlohmann/json.hpp>

using devmode::spawn::ensure_spawn_groups_array;
using devmode::spawn::find_spawn_groups_array;
using devmode::spawn::generate_spawn_id;

namespace {

std::string trim_copy_room_editor(const std::string& input) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    std::string result = input;
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [&](unsigned char ch) {
        return !is_space(ch);
    }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [&](unsigned char ch) {
        return !is_space(ch);
    }).base(), result.end());
    return result;
}

std::string sanitize_room_key_local(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool last_underscore = false;
    for (char ch : input) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            out.push_back(static_cast<char>(std::tolower(uch)));
            last_underscore = false;
        } else if (ch == '_' || ch == '-') {
            if (!last_underscore && !out.empty()) {
                out.push_back('_');
                last_underscore = true;
            }
        } else if (std::isspace(uch)) {
            if (!last_underscore && !out.empty()) {
                out.push_back('_');
                last_underscore = true;
            }
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "room";
    }
    return out;
}

std::string make_unique_room_key_excluding(const nlohmann::json& rooms_data,
                                           const std::string& base_key,
                                           const std::string& exclude_key) {
    std::string base = base_key.empty() ? std::string("room") : base_key;
    std::string candidate = base;
    int suffix = 1;
    while (rooms_data.is_object() && rooms_data.contains(candidate) && candidate != exclude_key) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

nlohmann::json* find_spawn_entry_in_array(nlohmann::json& array, const std::string& spawn_id) {
    if (!array.is_array()) {
        return nullptr;
    }
    for (auto& entry : array) {
        if (!entry.is_object()) {
            continue;
        }
        auto id_it = entry.find("spawn_id");
        if (id_it != entry.end() && id_it->is_string() && id_it->get<std::string>() == spawn_id) {
            return &entry;
        }
    }
    return nullptr;
}

nlohmann::json* find_spawn_entry_recursive(nlohmann::json& node,
                                          const std::string& spawn_id,
                                          nlohmann::json** owner_array) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "spawn_groups" && it->is_array()) {
                if (nlohmann::json* entry = find_spawn_entry_in_array(*it, spawn_id)) {
                    if (owner_array) {
                        *owner_array = &(*it);
                    }
                    return entry;
                }
            }
            if (it.key() == "spawn_groups") {
                continue;
            }
            if (nlohmann::json* nested = find_spawn_entry_recursive(it.value(), spawn_id, owner_array)) {
                return nested;
            }
        }
    } else if (node.is_array()) {
        for (auto& element : node) {
            if (nlohmann::json* nested = find_spawn_entry_recursive(element, spawn_id, owner_array)) {
                return nested;
            }
        }
    }
    return nullptr;
}

std::optional<double> ray_segment_distance(SDL_Point origin,
                                           SDL_FPoint direction,
                                           const SDL_Point& a,
                                           const SDL_Point& b) {
    SDL_FPoint segment{static_cast<float>(b.x - a.x), static_cast<float>(b.y - a.y)};
    SDL_FPoint offset{static_cast<float>(a.x - origin.x), static_cast<float>(a.y - origin.y)};

    double denom = static_cast<double>(direction.x) * segment.y - static_cast<double>(direction.y) * segment.x;
    if (std::fabs(denom) < 1e-6) {
        return std::nullopt;
    }

    double t = (offset.x * segment.y - offset.y * segment.x) / denom;
    double u = (offset.x * direction.y - offset.y * direction.x) / denom;
    if (t < 0.0 || u < 0.0 || u > 1.0) {
        return std::nullopt;
    }

    double dir_length = std::hypot(static_cast<double>(direction.x), static_cast<double>(direction.y));
    if (dir_length <= 1e-9) {
        return std::nullopt;
    }

    return t * dir_length;
}

void room_editor_trace(const std::string& message) {
    try {
        std::ofstream log("dev_mode_trace.log", std::ios::app);
        log << message << '\n';
    } catch (...) {
        // ignore logging failures
    }
}

}

RoomEditor::RoomEditor(Assets* owner, int screen_w, int screen_h)
    : assets_(owner), screen_w_(screen_w), screen_h_(screen_h) {
    update_room_config_bounds();
    rebuild_room_spawn_id_cache();
}

RoomEditor::~RoomEditor() = default;

void RoomEditor::set_room_assets_saved_callback(RoomAssetsSavedCallback cb) {
    room_assets_saved_callback_ = std::move(cb);
}

void RoomEditor::notify_room_assets_saved() {
    if (room_assets_saved_callback_) {
        room_assets_saved_callback_();
    }
}

void RoomEditor::save_current_room_assets_json() {
    if (!current_room_) {
        return;
    }
    if (info_ui_ && info_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Asset info panel is locked; save skipped.");
        return;
    }
    if (room_cfg_ui_ && room_cfg_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Room configurator is locked; save skipped.");
        return;
    }
    current_room_->save_assets_json();
    notify_room_assets_saved();
}

void RoomEditor::set_input(Input* input) {
    input_ = input;
    ensure_area_editor();
}

void RoomEditor::set_player(Asset* player) {
    player_ = player;
}

void RoomEditor::set_active_assets(std::vector<Asset*>& actives) {
    active_assets_ = &actives;
}

void RoomEditor::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    update_room_config_bounds();
    if (room_cfg_ui_ && room_config_dock_open_) {
        room_cfg_ui_->set_bounds(room_config_bounds_);
    }
    configure_shared_panel();
    refresh_room_config_visibility();

    if (spawn_group_panel_) {
        spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
        spawn_group_panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
        update_spawn_group_config_anchor();
    }

}

void RoomEditor::set_room_config_visible(bool visible) {
    ensure_room_configurator();
    if (!room_cfg_ui_) return;
    if (visible && active_modal_ == ActiveModal::AssetInfo) {
        pulse_active_modal_header();
        return;
    }
    if (visible) {
        room_cfg_ui_->open(current_room_);
    }
    room_config_dock_open_ = visible;
    refresh_room_config_visibility();
}

void RoomEditor::set_shared_footer_bar(DevFooterBar* footer) {
    shared_footer_bar_ = footer;
    configure_shared_panel();
    update_spawn_group_config_anchor();
}

void RoomEditor::set_header_visibility_callback(std::function<void(bool)> cb) {
    header_visibility_callback_ = std::move(cb);
    if (header_visibility_callback_) {
        header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
    }
    if (room_cfg_ui_) {
        room_cfg_ui_->set_header_visibility_controller([this](bool visible) {
            room_config_panel_visible_ = visible;
            if (header_visibility_callback_) {
                header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
            }
        });
    }
    if (info_ui_) {
        info_ui_->set_header_visibility_callback([this](bool visible) {
            asset_info_panel_visible_ = visible;
            if (header_visibility_callback_) {
                header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
            }
        });
    }
}

void RoomEditor::set_current_room(Room* room) {
    room_editor_trace("[RoomEditor] set_current_room begin");
    if (room) {
        room_editor_trace(std::string("[RoomEditor] target room -> ") + room->room_name);
    } else {
        room_editor_trace("[RoomEditor] target room -> <null>");
    }

    const bool room_changed = (room != current_room_);

    if (room != current_room_) {
        room_editor_trace("[RoomEditor] clearing active spawn group target");
        clear_active_spawn_group_target();
    }

    current_room_ = room;
    if (current_room_) {
        room_editor_trace("[RoomEditor] acquiring assets_data");
        auto& assets_json = current_room_->assets_data();
        room_editor_trace("[RoomEditor] ensuring spawn_groups array");
        auto& groups = ensure_spawn_groups_array(assets_json);
        if (sanitize_perimeter_spawn_groups(groups)) {
            room_editor_trace("[RoomEditor] perimeter groups sanitized, saving");
            save_current_room_assets_json();
        }
    }
    room_editor_trace("[RoomEditor] rebuilding room spawn id cache");
    rebuild_room_spawn_id_cache();
    room_editor_trace("[RoomEditor] refreshing spawn group config UI");
    refresh_spawn_group_config_ui();

    if (room_cfg_ui_) {
        room_editor_trace("[RoomEditor] opening room config UI");
        room_cfg_ui_->open(current_room_);
        refresh_room_config_visibility();
    }

    if (!enabled_ && room_changed && current_room_) {
        room_editor_trace("[RoomEditor] focusing camera on room center");
        focus_camera_on_room_center();
    }

    room_editor_trace("[RoomEditor] set_current_room complete");
}

void RoomEditor::set_enabled(bool enabled, bool preserve_camera_state) {
    enabled_ = enabled;
    if (!assets_) return;
    if (!enabled_) {
        active_modal_ = ActiveModal::None;
    }

    camera* cam = assets_ ? &assets_->getView() : nullptr;
    if (enabled_) {
        apply_area_editor_camera_override(false);
        if (cam && !preserve_camera_state) {
            cam->set_manual_zoom_override(false);
        }
        close_asset_info_editor();
        ensure_room_configurator();
        if (room_cfg_ui_) {
            room_cfg_ui_->open(current_room_);
            refresh_room_config_visibility();
        }
        configure_shared_panel();
    } else {
        apply_area_editor_camera_override(false);
        if (cam && !preserve_camera_state) {
            cam->set_manual_zoom_override(false);
            cam->clear_focus_override();
        }
        if (library_ui_) library_ui_->close();
        if (info_ui_) info_ui_->close();
        if (spawn_group_panel_) spawn_group_panel_->set_visible(false);
        clear_active_spawn_group_target();
        if (area_editor_) area_editor_->cancel();
        clear_selection();
        reset_click_state();
        reopen_info_after_area_edit_ = false;
        info_for_reopen_.reset();
        last_area_editor_active_ = false;
        set_room_config_visible(false);
        refresh_room_config_visibility();
    }

    if (input_) input_->clearClickBuffer();
}

void RoomEditor::update(const Input& input) {
    handle_shortcuts(input);

    if (!enabled_) return;
    if (!input_ || !active_assets_) return;

    handle_delete_shortcut(input);

    if (!should_enable_mouse_controls()) {
        if (assets_) {
            pan_zoom_.cancel(assets_->getView());
        }
        return;
    }

    const int mx = input.getX();
    const int my = input.getY();

    if (!is_ui_blocking_input(mx, my)) {
        handle_mouse_input(input);
    }
}

void RoomEditor::update_ui(const Input& input) {
    const bool config_visible_now = room_cfg_ui_ && room_cfg_ui_->visible();

    if (!enabled_) {
        room_config_was_visible_ = config_visible_now;
        return;
    }

    if (config_visible_now && !room_config_was_visible_) {
        reset_drag_state();
    }

    if (library_ui_ && library_ui_->is_visible()) {
        if (manifest_store_) {
            library_ui_->update(input, screen_w_, screen_h_, assets_->library(), *assets_, *manifest_store_);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "[RoomEditor] Manifest store unavailable; asset library UI update skipped.");
        }
    }

    if (library_ui_) {
        if (auto selected = library_ui_->consume_selection()) {
            bool spawned_asset = false;
            if (pending_spawn_world_pos_) {
                SDL_Point world = *pending_spawn_world_pos_;
                pending_spawn_world_pos_.reset();
                if (current_room_ && assets_) {
                    bool inside_room = !current_room_->room_area ||
                                       current_room_->room_area->contains_point(world);
                    if (inside_room) {
                        if (Asset* spawned = assets_->spawn_asset(selected->name, world)) {
                            finalize_asset_drag(spawned, selected);
                            selected_assets_.clear();
                            selected_assets_.push_back(spawned);
                            hovered_asset_ = spawned;
                            update_highlighted_assets();
                            sync_spawn_group_panel_with_selection();
                            spawned_asset = true;
                        }
                    }
                }
            }
            if (!spawned_asset) {
                pending_spawn_world_pos_.reset();
                open_asset_info_editor(selected);
            }
        }
    }

    if (pending_spawn_world_pos_ && (!library_ui_ || !library_ui_->is_visible())) {
        pending_spawn_world_pos_.reset();
    }

    if (room_cfg_ui_ && room_cfg_ui_->visible()) {
        room_cfg_ui_->update(input, screen_w_, screen_h_);
        update_spawn_group_config_anchor();
    }

    if (spawn_group_panel_) {
        spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
        if (spawn_group_panel_->is_visible()) {
            spawn_group_panel_->update(input, screen_w_, screen_h_);
        }
    }

    ensure_area_editor();
    if (area_editor_) {
        const bool was = last_area_editor_active_;
        const bool now = area_editor_->is_active();
        if (!was && now) {
            apply_area_editor_camera_override(true);
        }
        if (now) {
            area_editor_->update(input, screen_w_, screen_h_);

            if (assets_) {
                camera& cam = assets_->getView();
                if (should_enable_mouse_controls()) {
                    pan_zoom_.handle_input(cam, input, true);
                } else {
                    pan_zoom_.cancel(cam);
                }
            }
        }
        if (was && !now) {
            apply_area_editor_camera_override(false);
            if (area_editor_->consume_saved_flag() && reopen_info_after_area_edit_ && info_for_reopen_) {
                open_asset_info_editor(info_for_reopen_);
                reopen_info_after_area_edit_ = false;
                info_for_reopen_.reset();
            } else {
                reopen_info_after_area_edit_ = false;
                info_for_reopen_.reset();
            }
        }
        last_area_editor_active_ = now;
    }

    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->update(input, screen_w_, screen_h_);
    } else if (active_modal_ == ActiveModal::AssetInfo) {
        active_modal_ = ActiveModal::None;
    }

    update_area_editor_focus();

    room_config_was_visible_ = config_visible_now;
}

bool RoomEditor::handle_sdl_event(const SDL_Event& event) {
    int mx = 0;
    int my = 0;
    if (event.type == SDL_MOUSEMOTION) {
        mx = event.motion.x;
        my = event.motion.y;
    } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        mx = event.button.x;
        my = event.button.y;
    } else if (event.type == SDL_MOUSEWHEEL) {
        SDL_GetMouseState(&mx, &my);
    }

    const bool pointer_event =
        (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION);
    const bool wheel_event = (event.type == SDL_MOUSEWHEEL);
    const bool pointer_based = pointer_event || wheel_event;

    struct RouteResult {
        bool handled = false;
        bool pointer_blocked = false;
};

    auto apply_result = [&](const RouteResult& result, bool& pointer_blocked) -> bool {
        if (result.handled) {
            if (input_) {
                input_->consumeEvent(event);
            }
            return true;
        }
        if (pointer_based && result.pointer_blocked) {
            pointer_blocked = true;
        }
        return false;
    };

    bool pointer_blocked = false;

    auto route_info_panel = [&]() -> RouteResult {
        RouteResult result;
        if (!info_ui_ || !info_ui_->is_visible()) {
            return result;
        }
        if (info_ui_->handle_event(event)) {
            result.handled = true;
            result.pointer_blocked = true;
            return result;
        }
        if (pointer_based && info_ui_->is_point_inside(mx, my)) {
            result.pointer_blocked = true;
        }
        return result;
};

    auto route_room_config = [&]() -> RouteResult {
        RouteResult result;
        if (!room_cfg_ui_ || !room_cfg_ui_->visible()) {
            return result;
        }
        room_cfg_ui_->prepare_for_event(screen_w_, screen_h_);
        if (room_cfg_ui_->handle_event(event)) {
            result.handled = true;
            result.pointer_blocked = true;
            return result;
        }
        if (pointer_based && room_cfg_ui_->is_point_inside(mx, my)) {
            result.pointer_blocked = true;
        }
        return result;
};

    auto route_spawn_groups = [&]() -> RouteResult {
        RouteResult result;
        if (!spawn_group_panel_ || !spawn_group_panel_->is_visible()) {
            return result;
        }
        spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
        if (spawn_group_panel_->handle_event(event)) {
            result.handled = true;
            result.pointer_blocked = true;
            return result;
        }
        if (pointer_based && spawn_group_panel_->is_point_inside(mx, my)) {
            result.pointer_blocked = true;
        }
        return result;
};

    auto route_library_panel = [&]() -> RouteResult {
        RouteResult result;
        if (!library_ui_ || !library_ui_->is_visible()) {
            return result;
        }
        if (library_ui_->handle_event(event)) {
            result.handled = true;
            result.pointer_blocked = true;
            return result;
        }
        if (pointer_based && library_ui_->is_input_blocking_at(mx, my)) {
            result.pointer_blocked = true;
        }
        return result;
};

    if (apply_result(route_info_panel(), pointer_blocked)) {
        return true;
    }
    if (apply_result(route_room_config(), pointer_blocked)) {
        return true;
    }
    if (apply_result(route_spawn_groups(), pointer_blocked)) {
        return true;
    }
    if (apply_result(route_library_panel(), pointer_blocked)) {
        return true;
    }

    ensure_area_editor();
    if ((!pointer_blocked || !pointer_based) && area_editor_ && area_editor_->is_active()) {
        if (area_editor_->handle_event(event)) {
            if (pointer_event && input_) {
                input_->clearClickBuffer();
            }
            return true;
        }
    }

    if (auto* dropdown = DMDropdown::active_dropdown()) {
        if (dropdown->handle_event(event)) {
            if (pointer_event && input_) {
                input_->clearClickBuffer();
            }
            return true;
        }
    }

    if (pointer_based && pointer_blocked) {
        if (input_) {
            input_->consumeEvent(event);
        }
        return true;
    }

    return false;
}

bool RoomEditor::is_room_panel_blocking_point(int x, int y) const {
    if (!enabled_) {
        return false;
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(x, y)) {
        return true;
    }
    if (spawn_group_panel_ && spawn_group_panel_->is_visible() && spawn_group_panel_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

bool RoomEditor::is_room_ui_blocking_point(int x, int y) const {

    if (!enabled_) {
        return false;
    }

    if (info_ui_ && info_ui_->is_visible() && info_ui_->is_point_inside(x, y)) {
        return true;
    }

    if (room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(x, y)) {
        return true;
    }
    if (spawn_group_panel_ && spawn_group_panel_->is_visible() && spawn_group_panel_->is_point_inside(x, y)) {
        return true;
    }

    if (library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking_at(x, y)) {
        return true;
    }

    if (area_editor_ && area_editor_->is_active()) {
        return true;
    }

    return false;
}

void RoomEditor::render_overlays(SDL_Renderer* renderer) {
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->render(renderer, screen_w_, screen_h_);
    }
    ensure_area_editor();
    if (area_editor_ && area_editor_->is_active()) {
        area_editor_->render(renderer);
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->render_world_overlay(renderer, assets_->getView());
        info_ui_->render(renderer, screen_w_, screen_h_);
    }

    if (renderer && assets_ && current_room_ && current_room_->room_area) {
        auto overlay = compute_perimeter_overlay_for_drag();
        if (!overlay) {
            std::string spawn_id;
            if (hovered_asset_ && hovered_asset_->spawn_method == "Perimeter" && !hovered_asset_->spawn_id.empty()) {
                spawn_id = hovered_asset_->spawn_id;
            } else {
                for (Asset* asset : selected_assets_) {
                    if (!asset) continue;
                    if (asset->spawn_method == "Perimeter" && !asset->spawn_id.empty()) {
                        spawn_id = asset->spawn_id;
                        break;
                    }
                }
            }
            if (!spawn_id.empty()) {
                overlay = compute_perimeter_overlay_for_spawn(spawn_id);
            }
        }
        if (overlay && overlay->radius > 0.0) {
            const camera& cam = assets_->getView();
            const double scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
            const double inv_scale = 1.0 / scale;
            SDL_Point screen_center = cam.map_to_screen(overlay->center);
            int radius_px = static_cast<int>(std::lround(overlay->radius * inv_scale));
            radius_px = std::max(1, radius_px);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const SDL_Color accent = DMStyles::AccentButton().hover_bg;
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, 210);
            const int segments = std::clamp(radius_px * 4, 64, 720);
            for (int i = 0; i < segments; ++i) {
                double angle = (static_cast<double>(i) / static_cast<double>(segments)) * 2.0 * M_PI;
                int px = screen_center.x + static_cast<int>(std::lround(std::cos(angle) * static_cast<double>(radius_px)));
                int py = screen_center.y + static_cast<int>(std::lround(std::sin(angle) * static_cast<double>(radius_px)));
                SDL_RenderDrawPoint(renderer, px, py);
            }
            const int cross = std::max(6, radius_px / 4);
            SDL_RenderDrawLine(renderer, screen_center.x - cross, screen_center.y, screen_center.x + cross, screen_center.y);
            SDL_RenderDrawLine(renderer, screen_center.x, screen_center.y - cross, screen_center.x, screen_center.y + cross);
        }
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible()) {
        room_cfg_ui_->render(renderer);
    }
    if (spawn_group_panel_ && spawn_group_panel_->is_visible()) {
        spawn_group_panel_->render(renderer);
    }
    DMDropdown::render_active_options(renderer);
}

void RoomEditor::toggle_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    const bool currently_open = library_ui_ && library_ui_->is_visible();
    if (!currently_open && active_modal_ == ActiveModal::AssetInfo) {
        pulse_active_modal_header();
        return;
    }
    if (library_ui_ && library_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Asset library is locked; toggle ignored.");
        return;
    }
    library_ui_->toggle();
}

void RoomEditor::open_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    if (active_modal_ == ActiveModal::AssetInfo && (!library_ui_ || !library_ui_->is_visible())) {
        pulse_active_modal_header();
        return;
    }
    library_ui_->open();
}

void RoomEditor::close_asset_library() {
    if (library_ui_) library_ui_->close();
    pending_spawn_world_pos_.reset();
}

bool RoomEditor::is_asset_library_open() const {
    return library_ui_ && library_ui_->is_visible();
}

bool RoomEditor::is_library_drag_active() const {
    return library_ui_ && library_ui_->is_visible() && library_ui_->is_dragging_asset();
}

std::shared_ptr<AssetInfo> RoomEditor::consume_selected_asset_from_library() {
    if (!library_ui_) return nullptr;
    return library_ui_->consume_selection();
}

void RoomEditor::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!info) return;
    if (library_ui_) library_ui_->close();
    clear_active_spawn_group_target();
    if (room_config_dock_open_) {
        set_room_config_visible(false);
    }
    if (!info_ui_) {
        info_ui_ = std::make_unique<AssetInfoUI>();
        if (info_ui_) {
            info_ui_->set_manifest_store(manifest_store_);
        }
        info_ui_->set_header_visibility_callback([this](bool visible) {
            asset_info_panel_visible_ = visible;
            if (header_visibility_callback_) {
                header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
            }
        });
    }
    if (info_ui_) info_ui_->set_assets(assets_);
    if (info_ui_) {
        info_ui_->clear_info();
        info_ui_->set_info(info);
        info_ui_->set_target_asset(nullptr);
        info_ui_->open();
    }
    active_modal_ = ActiveModal::AssetInfo;
}

void RoomEditor::open_asset_info_editor_for_asset(Asset* asset) {
    if (!asset || !asset->info) return;
    std::cout << "Opening AssetInfoUI for asset: " << asset->info->name << std::endl;
    clear_selection();
    focus_camera_on_asset(asset, 0.8, 0);
    open_asset_info_editor(asset->info);
    if (info_ui_) info_ui_->set_target_asset(asset);
}

void RoomEditor::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
    if (info_ui_) {
        info_ui_->set_manifest_store(manifest_store_);
    }
}

void RoomEditor::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
    if (asset_info_panel_visible_) {
        asset_info_panel_visible_ = false;
        if (header_visibility_callback_) {
            header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
        }
    }
    if (active_modal_ == ActiveModal::AssetInfo) {
        active_modal_ = ActiveModal::None;
    }
}

bool RoomEditor::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

bool RoomEditor::has_active_modal() const {
    return active_modal_ != ActiveModal::None;
}

void RoomEditor::pulse_active_modal_header() {
    if (active_modal_ == ActiveModal::AssetInfo && info_ui_) {
        info_ui_->pulse_header();
    }
}

void RoomEditor::finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info) {
    if (!asset || !info || !current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);

    int width = 0;
    int height = 0;
    SDL_Point center{0, 0};
    if (current_room_->room_area) {
        auto bounds = current_room_->room_area->get_bounds();
        width = std::max(1, std::get<2>(bounds) - std::get<0>(bounds));
        height = std::max(1, std::get<3>(bounds) - std::get<1>(bounds));
        auto c = current_room_->room_area->get_center();
        center.x = c.x;
        center.y = c.y;
    }

    std::string spawn_id = generate_spawn_id();
    nlohmann::json entry;
    entry["spawn_id"] = spawn_id;
    entry["position"] = "Exact";
    entry["dx"] = asset->pos.x - center.x;
    entry["dy"] = asset->pos.y - center.y;
    if (width > 0) entry["origional_width"] = width;
    if (height > 0) entry["origional_height"] = height;
    entry["display_name"] = info->name;

    const int default_resolution = current_room_ ? current_room_->map_grid_settings().resolution
                                                 : MapGridSettings::defaults().resolution;
    devmode::spawn::ensure_spawn_group_entry_defaults(entry, info->name, default_resolution);

    entry["candidates"].push_back({{"name", info->name}, {"chance", 100}});

    arr.push_back(entry);
    save_current_room_assets_json();
    asset->spawn_id = spawn_id;
    asset->spawn_method = "Exact";
    active_spawn_group_id_ = spawn_id;
    refresh_spawn_group_config_ui();
    rebuild_room_spawn_id_cache();
}

void RoomEditor::toggle_room_config() {
    ensure_room_configurator();
    if (room_cfg_ui_ && room_cfg_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Room configurator is locked; toggle ignored.");
        return;
    }
    set_room_config_visible(!is_room_config_open());
}

void RoomEditor::open_room_config() {
    ensure_room_configurator();
    if (room_cfg_ui_ && room_cfg_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Room configurator is locked; open request ignored.");
        return;
    }
    set_room_config_visible(true);
}

void RoomEditor::close_room_config() {
    set_room_config_visible(false);
}

bool RoomEditor::is_room_config_open() const {
    return room_config_dock_open_;
}

void RoomEditor::regenerate_room() {
    if (room_cfg_ui_ && room_cfg_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Room configurator is locked; regeneration skipped.");
        return;
    }
    regenerate_current_room();
}

void RoomEditor::regenerate_room_from_template(Room* source_room) {
    if (room_cfg_ui_ && room_cfg_ui_->is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Room configurator is locked; regeneration from template skipped.");
        return;
    }
    if (!assets_ || !current_room_ || !source_room) return;

    nlohmann::json template_root = source_room->assets_data();
    auto& template_groups = ensure_spawn_groups_array(template_root);
    const int template_resolution = current_room_ ? current_room_->map_grid_settings().resolution
                                                  : MapGridSettings::defaults().resolution;
    for (auto& entry : template_groups) {
        if (!entry.is_object()) continue;
        entry["spawn_id"] = generate_spawn_id();
        devmode::spawn::ensure_spawn_group_entry_defaults(
            entry,
            entry.contains("display_name") && entry["display_name"].is_string()
                ? entry["display_name"].get<std::string>()
                : std::string{"New Spawn"},
            template_resolution);
    }

    sanitize_perimeter_spawn_groups(template_groups);

    auto& target_root = current_room_->assets_data();
    target_root = std::move(template_root);

    regenerate_current_room();

    rebuild_room_spawn_id_cache();
    save_current_room_assets_json();
}

void RoomEditor::begin_area_edit_for_selected_asset(const std::string& area_name) {
    ensure_area_editor();
    if (!area_editor_) return;

    Asset* target = nullptr;
    if (!selected_assets_.empty()) target = selected_assets_.front();
    if (!target) target = hovered_asset_;
    if (!target && info_ui_) target = info_ui_->get_target_asset();
    if (!target || !target->info) return;

    if (info_ui_ && info_ui_->is_visible()) {
        reopen_info_after_area_edit_ = true;
        info_for_reopen_ = target->info;
        info_target_for_reopen_ = target;
        info_ui_->close();
    } else {
        reopen_info_after_area_edit_ = false;
        info_for_reopen_.reset();
        info_target_for_reopen_ = nullptr;
    }

    focus_camera_on_asset(target, 0.8, 0);
    if (area_editor_->begin(target->info.get(), target, area_name)) {
        apply_area_editor_camera_override(true);
        last_area_editor_active_ = true;
    }
}

void RoomEditor::focus_camera_on_asset(Asset* asset, double zoom_factor, int duration_steps) {
    if (!asset || !assets_) return;
    camera& cam = assets_->getView();
    cam.set_manual_zoom_override(true);
    cam.pan_and_zoom_to_asset(asset, zoom_factor, duration_steps);
}

void RoomEditor::focus_camera_on_room_center(bool reframe_zoom) {
    if (!enabled_ || !assets_) return;
    if (!current_room_ || !current_room_->room_area) return;

    camera& cam = assets_->getView();
    const SDL_Point center = current_room_->room_area->get_center();
    cam.set_manual_zoom_override(true);
    cam.set_focus_override(center);

    if (reframe_zoom) {
        cam.zoom_to_area(*current_room_->room_area, 0);
    }
}

void RoomEditor::reset_click_state() {
    click_buffer_frames_ = 0;
    rclick_buffer_frames_ = 0;
    suppress_next_left_click_ = false;
    last_click_asset_ = nullptr;
    last_click_time_ms_ = 0;
    reset_drag_state();
}

void RoomEditor::clear_selection() {
    selected_assets_.clear();
    highlighted_assets_.clear();
    hovered_asset_ = nullptr;
    reset_drag_state();
    sync_spawn_group_panel_with_selection();
    if (!active_assets_) return;
    for (Asset* asset : *active_assets_) {
        if (!asset) continue;
        asset->set_selected(false);
        asset->set_highlighted(false);
    }
}

void RoomEditor::clear_highlighted_assets() {
    highlighted_assets_.clear();
    if (!active_assets_) {
        selected_assets_.clear();
        hovered_asset_ = nullptr;
        return;
    }
    auto erase_if_inactive = [this](Asset* asset) {
        if (!asset) return true;
        auto it = std::find(active_assets_->begin(), active_assets_->end(), asset);
        if (it == active_assets_->end()) {
            asset->set_highlighted(false);
            asset->set_selected(false);
            return true;
        }
        return false;
};

    selected_assets_.erase( std::remove_if(selected_assets_.begin(), selected_assets_.end(), erase_if_inactive), selected_assets_.end());

    if (hovered_asset_ && erase_if_inactive(hovered_asset_)) {
        hovered_asset_ = nullptr;
        hover_miss_frames_ = 0;
    }

    for (Asset* asset : *active_assets_) {
        if (!asset) {
            continue;
        }
        asset->set_highlighted(false);
        const bool is_selected = std::find(selected_assets_.begin(), selected_assets_.end(), asset) != selected_assets_.end();
        asset->set_selected(is_selected);
    }
    sync_spawn_group_panel_with_selection();
}

void RoomEditor::purge_asset(Asset* asset) {
    if (!asset) return;
    if (hovered_asset_ == asset) hovered_asset_ = nullptr;
    auto erase_from = [asset](std::vector<Asset*>& vec) {
        vec.erase(std::remove(vec.begin(), vec.end(), asset), vec.end());
};
    erase_from(selected_assets_);
    erase_from(highlighted_assets_);
    if (drag_anchor_asset_ == asset) {
        drag_anchor_asset_ = nullptr;
        dragging_ = false;
    }
    drag_states_.erase(std::remove_if(drag_states_.begin(), drag_states_.end(),
                                      [asset](const DraggedAssetState& state) { return state.asset == asset; }),
                       drag_states_.end());
    if (drag_states_.empty()) {
        reset_drag_state();
    }
    sync_spawn_group_panel_with_selection();
}

void RoomEditor::set_zoom_scale_factor(double factor) {
    zoom_scale_factor_ = (factor > 0.0) ? factor : 1.0;
    pan_zoom_.set_zoom_scale_factor(zoom_scale_factor_);
}

bool RoomEditor::is_spawn_group_panel_visible() const {
    return spawn_group_panel_ && spawn_group_panel_->is_visible();
}

void RoomEditor::handle_mouse_input(const Input& input) {
    camera& cam = assets_->getView();

    const bool asset_info_open =
        (active_modal_ == ActiveModal::AssetInfo) || (info_ui_ && info_ui_->is_visible());

    if (!asset_info_open && input.isScancodeDown(SDL_SCANCODE_ESCAPE)) {
        clear_selection();
        return;
    }

    if (!input_) return;

    const int mx = input_->getX();
    const int my = input_->getY();
    const bool ui_blocked = asset_info_open || is_ui_blocking_input(mx, my);
    const bool library_modal_block =
        library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking();

    const bool config_open = room_cfg_ui_ && room_cfg_ui_->visible();
    const bool suppress_hover_and_drag = library_modal_block || asset_info_open || config_open;
    const bool suppress_clicks = library_modal_block || asset_info_open;

    Asset* hit_asset = nullptr;
    if (!ui_blocked && !suppress_hover_and_drag) {
        hit_asset = hit_test_asset(SDL_Point{mx, my});
    }

    pan_zoom_.handle_input(cam, input, true);

    SDL_Point world_mouse = cam.screen_to_map(SDL_Point{mx, my});

    if (suppress_hover_and_drag) {
        if (dragging_) {
            if (library_modal_block) {
                finalize_drag_session();
                dragging_ = false;
            } else if (config_open) {
                reset_drag_state();
            }
        } else if (config_open) {
            reset_drag_state();
        }
        hovered_asset_ = nullptr;
        hover_miss_frames_ = 3;
    } else {
        update_hover_state(hit_asset);

        const bool pointer_over_selection = hovered_asset_ &&
            (std::find(selected_assets_.begin(), selected_assets_.end(), hovered_asset_) != selected_assets_.end());
        const bool ctrl_modifier = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);

        if (input_->isDown(Input::LEFT) && !selected_assets_.empty()) {
            if (!dragging_) {
                if (pointer_over_selection) {
                    dragging_ = true;
                    drag_last_world_ = world_mouse;
                    begin_drag_session(world_mouse, ctrl_modifier);
                }
            } else {
                update_drag_session(world_mouse);
            }
        } else {
            if (dragging_) {
                finalize_drag_session();
            }
            dragging_ = false;
        }
    }

    if (!suppress_clicks) {
        handle_click(input);
    } else {
        click_buffer_frames_ = 0;
        rclick_buffer_frames_ = 0;
    }
    update_highlighted_assets();
}

Asset* RoomEditor::hit_test_asset(SDL_Point screen_point) const {
    if (!active_assets_ || !assets_) return nullptr;

    const camera& cam = assets_->getView();
    const float scale = std::max(0.0001f, cam.get_scale());
    const float inv_scale = 1.0f / scale;

    float reference_screen_height = 1.0f;
    Asset* player_asset = assets_->player;
    if (player_asset) {
        SDL_Texture* player_final = player_asset->get_final_texture();
        SDL_Texture* player_frame = player_asset->get_current_frame();
        int pw = player_asset->cached_w;
        int ph = player_asset->cached_h;
        if ((pw == 0 || ph == 0) && player_final) {
            SDL_QueryTexture(player_final, nullptr, nullptr, &pw, &ph);
        }
        if ((pw == 0 || ph == 0) && player_frame) {
            SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
        }
        if (pw != 0) player_asset->cached_w = pw;
        if (ph != 0) player_asset->cached_h = ph;

        float player_scale = 1.0f;
        if (player_asset->info && std::isfinite(player_asset->info->scale_factor) && player_asset->info->scale_factor >= 0.0f) {
            player_scale = player_asset->info->scale_factor;
        }
        if (ph > 0) {
            reference_screen_height = static_cast<float>(ph) * player_scale * inv_scale;
        }
    }
    if (reference_screen_height <= 0.0f) {
        reference_screen_height = 1.0f;
    }

    Asset* best = nullptr;
    int best_screen_y = std::numeric_limits<int>::min();
    int best_z_index = std::numeric_limits<int>::min();

    for (Asset* asset : *active_assets_) {
        if (!asset) continue;

        SDL_Texture* tex = asset->get_final_texture();
        if (!tex) {
            tex = asset->get_current_frame();
        }

        int fw = asset->cached_w;
        int fh = asset->cached_h;
        if ((fw == 0 || fh == 0) && tex) {
            SDL_QueryTexture(tex, nullptr, nullptr, &fw, &fh);
            if (asset->cached_w == 0) asset->cached_w = fw;
            if (asset->cached_h == 0) asset->cached_h = fh;
        }
        if (fw <= 0 || fh <= 0) continue;

        float base_scale = 1.0f;
        if (asset->info && std::isfinite(asset->info->scale_factor) && asset->info->scale_factor >= 0.0f) {
            base_scale = asset->info->scale_factor;
        }

        const float scaled_fw = static_cast<float>(fw) * base_scale;
        const float scaled_fh = static_cast<float>(fh) * base_scale;
        const float base_sw = scaled_fw * inv_scale;
        const float base_sh = scaled_fh * inv_scale;

        const camera::RenderEffects effects =
            cam.compute_render_effects(SDL_Point{asset->pos.x, asset->pos.y}, base_sh, reference_screen_height);

        const float scaled_sw = base_sw * effects.distance_scale;
        const float scaled_sh = base_sh * effects.distance_scale;
        const float final_visible_h = scaled_sh * effects.vertical_scale;

        const int sw = std::max(1, static_cast<int>(std::lround(static_cast<double>(scaled_sw))));
        const int sh = std::max(1, static_cast<int>(std::lround(static_cast<double>(final_visible_h))));
        if (sw <= 0 || sh <= 0) continue;

        const SDL_Point& center = effects.screen_position;
        SDL_Rect rect{center.x - sw / 2, center.y - sh, sw, sh};
        if (!SDL_PointInRect(&screen_point, &rect)) continue;

        if (!best || center.y > best_screen_y ||
            (center.y == best_screen_y && asset->z_index > best_z_index)) {
            best = asset;
            best_screen_y = center.y;
            best_z_index = asset->z_index;
        }
    }

    return best;
}

void RoomEditor::update_hover_state(Asset* hit) {
    if (hit) {
        hovered_asset_ = hit;
        hover_miss_frames_ = 0;
    } else {
        if (++hover_miss_frames_ >= 3) {
            hovered_asset_ = nullptr;
            hover_miss_frames_ = 3;
        }
    }
}

void RoomEditor::handle_click(const Input& input) {
    if (!input_) return;

    SDL_Point world_mouse{0, 0};
    if (assets_) {
        world_mouse = assets_->getView().screen_to_map(SDL_Point{input_->getX(), input_->getY()});
    }

    if (suppress_next_left_click_) {
        if (input_->wasClicked(Input::LEFT)) {
            suppress_next_left_click_ = false;
            click_buffer_frames_ = 0;
            return;
        }
    }

    if (input_->wasClicked(Input::RIGHT)) {
        if (rclick_buffer_frames_ > 0) {
            --rclick_buffer_frames_;
            return;
        }
        rclick_buffer_frames_ = 2;
        if (hovered_asset_) {
            open_asset_info_editor_for_asset(hovered_asset_);
        } else {
            bool inside_room = true;
            if (current_room_ && current_room_->room_area) {
                inside_room = current_room_->room_area->contains_point(world_mouse);
            }
            if (inside_room) {
                pending_spawn_world_pos_ = world_mouse;
                open_asset_library();
                if (!is_asset_library_open()) {
                    pending_spawn_world_pos_.reset();
                }
            }
        }
        return;
    } else {
        rclick_buffer_frames_ = 0;
    }

    if (!input_->wasClicked(Input::LEFT)) {
        click_buffer_frames_ = 0;
        return;
    }

    if (suppress_next_left_click_) {
        suppress_next_left_click_ = false;
        click_buffer_frames_ = 0;
        return;
    }

    if (click_buffer_frames_ > 0) {
        --click_buffer_frames_;
        return;
    }
    click_buffer_frames_ = 2;

    Asset* nearest = hovered_asset_;
    if (nearest) {
        const bool already_selected =
            std::find(selected_assets_.begin(), selected_assets_.end(), nearest) != selected_assets_.end();
        if (already_selected) {
            selected_assets_.clear();
            highlighted_assets_.clear();
            last_click_asset_ = nullptr;
            last_click_time_ms_ = 0;
            return;
        }

        selected_assets_.clear();
        bool select_group = true;
        const std::string& method = nearest->spawn_method;
        if (method == "Exact" || method == "Exact Position" || method == "Percent") {
            select_group = false;
        }
        if (select_group && !nearest->spawn_id.empty() && active_assets_) {
            for (Asset* asset : *active_assets_) {
                if (!asset_belongs_to_room(asset)) continue;
                if (asset->spawn_id == nearest->spawn_id) {
                    selected_assets_.push_back(asset);
                }
            }
        } else {
            if (asset_belongs_to_room(nearest)) {
                selected_assets_.push_back(nearest);
            }
        }
        sync_spawn_group_panel_with_selection();
    } else {
        selected_assets_.clear();
        highlighted_assets_.clear();
        sync_spawn_group_panel_with_selection();

        const bool asset_info_open = (active_modal_ == ActiveModal::AssetInfo);
        const bool floating_modal_open = FloatingDockableManager::instance().active_panel() != nullptr;

        const bool area_editor_active = area_editor_ && area_editor_->is_active();

        bool inside_room = true;
        if (current_room_ && current_room_->room_area) {
            inside_room = current_room_->room_area->contains_point(world_mouse);
        }

        if (!inside_room && assets_) {
            for (Room* r : assets_->rooms()) {
                if (!r || r == current_room_ || !r->room_area) continue;
                if (r->room_area->contains_point(world_mouse)) {

                    assets_->set_editor_current_room(r);
                    inside_room = true;
                    break;
                }
            }
        }

        if (inside_room && !asset_info_open && !floating_modal_open &&
            !area_editor_active && hovered_asset_ == nullptr) {
            if (assets_) {
                camera& cam = assets_->getView();

                cam.pan_and_zoom_to_point(world_mouse, 1.0, 0);
            }
        }
    }
}

void RoomEditor::update_highlighted_assets() {
    if (!active_assets_) return;

    highlighted_assets_ = selected_assets_;
    bool allow_hover_group = false;
    if (hovered_asset_) {
        if (selected_assets_.empty()) {
            allow_hover_group = true;
        } else if (!hovered_asset_->spawn_id.empty()) {
            allow_hover_group = std::any_of(selected_assets_.begin(), selected_assets_.end(),
                                            [&](Asset* asset) {
                                                return asset && asset->spawn_id == hovered_asset_->spawn_id;
                                            });
        } else {
            allow_hover_group = std::find(selected_assets_.begin(), selected_assets_.end(), hovered_asset_) != selected_assets_.end();
        }
    }

    if (allow_hover_group) {
        for (Asset* asset : *active_assets_) {
            if (!asset_belongs_to_room(asset)) continue;
            if (!hovered_asset_->spawn_id.empty() && asset->spawn_id == hovered_asset_->spawn_id) {
                if (std::find(highlighted_assets_.begin(), highlighted_assets_.end(), asset) == highlighted_assets_.end()) {
                    highlighted_assets_.push_back(asset);
                }
            } else if (asset == hovered_asset_) {
                if (std::find(highlighted_assets_.begin(), highlighted_assets_.end(), asset) == highlighted_assets_.end()) {
                    highlighted_assets_.push_back(asset);
                }
            }
        }
    }

    for (Asset* asset : *active_assets_) {
        if (!asset) continue;
        asset->set_highlighted(false);
        asset->set_selected(false);
    }

    for (Asset* asset : highlighted_assets_) {
        if (!asset) continue;
        if (std::find(selected_assets_.begin(), selected_assets_.end(), asset) != selected_assets_.end()) {
            asset->set_selected(true);
            asset->set_highlighted(false);
        } else {
            asset->set_highlighted(true);
            asset->set_selected(false);
        }
    }
}

bool RoomEditor::is_ui_blocking_input(int mx, int my) const {
    if (info_ui_ && info_ui_->is_visible()) {
        if (info_ui_->is_point_inside(mx, my)) {
            return true;
        }
    }
    if (shared_footer_bar_ && shared_footer_bar_->visible()) {
        if (shared_footer_bar_->contains(mx, my)) {
            return true;
        }
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(mx, my)) {
        return true;
    }
    if (spawn_group_panel_ && spawn_group_panel_->is_visible() && spawn_group_panel_->is_point_inside(mx, my)) {
        return true;
    }
    if (library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking_at(mx, my)) {
        return true;
    }
    if (area_editor_ && area_editor_->is_active()) {
        return true;
    }

    return false;
}

bool RoomEditor::should_enable_mouse_controls() const {
    if (!enabled_) {
        return false;
    }

    if (active_modal_ != ActiveModal::None) {
        return false;
    }

    if (info_ui_ && info_ui_->is_visible()) {
        return false;
    }
    if (library_ui_ && library_ui_->is_visible()) {
        return false;
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible()) {
        return false;
    }
    if (area_editor_ && area_editor_->is_active()) {
        return false;
    }

    auto floating = FloatingDockableManager::instance().open_panels();
    for (DockableCollapsible* panel : floating) {
        if (!panel) {
            continue;
        }
        if (spawn_group_panel_ && panel == spawn_group_panel_.get()) {
            continue;
        }
        if (!panel->is_visible()) {
            continue;
        }
        return false;
    }

    return true;
}

void RoomEditor::handle_shortcuts(const Input& input) {
    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (!ctrl) return;

    if (input.wasScancodePressed(SDL_SCANCODE_A)) {
        if (library_ui_ && library_ui_->is_locked()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Asset library is locked; shortcut ignored.");
        } else {
            toggle_asset_library();
        }
    }
    if (input.wasScancodePressed(SDL_SCANCODE_R)) {
        if (room_cfg_ui_ && room_cfg_ui_->is_locked()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Room configurator is locked; shortcut ignored.");
        } else {
            toggle_room_config();
        }
    }
}

void RoomEditor::update_area_editor_focus() {
    ensure_area_editor();
    if (!area_editor_) return;

    const bool editing_overlay_active = area_editor_->is_active();
    if (!assets_) return;

    camera& cam = assets_->getView();
    if (editing_overlay_active) {
        Asset* focus = nullptr;
        if (!selected_assets_.empty()) focus = selected_assets_.front();
        if (!focus) focus = hovered_asset_;
        if (focus) {
            cam.set_manual_zoom_override(true);
            cam.set_focus_override(SDL_Point{focus->pos.x, focus->pos.y});
        }

    }
}

void RoomEditor::ensure_area_editor() {
    if (!area_editor_) {
        area_editor_ = std::make_unique<AreaOverlayEditor>();
        if (area_editor_) area_editor_->attach_assets(assets_);
    }
}

void RoomEditor::apply_area_editor_camera_override(bool enable) {
    area_editor_override_active_ = enable;
}

void RoomEditor::ensure_room_configurator() {
    if (!room_cfg_ui_) {
        room_cfg_ui_ = std::make_unique<RoomConfigurator>();
    }
    if (room_cfg_ui_) {
        room_cfg_ui_->set_header_visibility_controller([this](bool visible) {
            room_config_panel_visible_ = visible;
            if (header_visibility_callback_) {
                header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
            }
        });
        room_cfg_ui_->set_bounds(room_config_bounds_);
        room_cfg_ui_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
        room_cfg_ui_->set_on_close([this]() {
            room_config_dock_open_ = false;
            update_spawn_group_config_anchor();
        });
        room_cfg_ui_->set_spawn_group_callbacks(
            [this](const std::string& spawn_id) {
                if (active_modal_ == ActiveModal::AssetInfo) {
                    pulse_active_modal_header();
                    return;
                }
                open_spawn_group_editor_by_id(spawn_id);
            },
            [this](const std::string& spawn_id) {
                delete_spawn_group_internal(spawn_id);
            },
            [this](const std::string& spawn_id, size_t index) {
                reorder_spawn_group_internal(spawn_id, index);
            },
            [this]() {
                if (active_modal_ == ActiveModal::AssetInfo) {
                    pulse_active_modal_header();
                    return;
                }
                add_spawn_group_internal();
            },
            [this](const std::string& spawn_id) {
                if (spawn_id.empty()) {
                    clear_active_spawn_group_target();
                } else {
                    active_spawn_group_id_ = spawn_id;
                }
                refresh_spawn_group_config_ui();
                if (spawn_id.empty()) {
                    return;
                }
                if (nlohmann::json* entry = find_spawn_entry(spawn_id)) {
                    respawn_spawn_group(*entry);
                }
            });
        room_cfg_ui_->set_on_room_renamed([this](const std::string& old_name, const std::string& desired) {
            return this->rename_active_room(old_name, desired);
        });
    }
}

std::string RoomEditor::rename_active_room(const std::string& old_name, const std::string& desired_name) {
    std::string trimmed = trim_copy_room_editor(desired_name);
    std::string base = sanitize_room_key_local(trimmed.empty() ? desired_name : trimmed);
    if (!assets_ || !current_room_) {
        return base.empty() ? old_name : base;
    }

    auto& map_info = assets_->map_info_json();
    nlohmann::json& rooms_data = map_info["rooms_data"];
    if (!rooms_data.is_object()) {
        rooms_data = nlohmann::json::object();
    }

    std::string candidate = base.empty() ? current_room_->room_name : base;
    if (candidate.empty()) {
        candidate = old_name;
    }

    if (candidate == old_name) {
        return old_name;
    }

    if (rooms_data.contains(candidate)) {
        return old_name;
    }

    std::string final_key = candidate;

    if (final_key != current_room_->room_name) {
        current_room_->rename(final_key, map_info);
        map_layers::rename_room_references_in_layers(map_info, old_name, final_key);
        if (manifest_store_ && assets_) {
            if (devmode::persist_map_manifest_entry(
                    *manifest_store_, assets_->map_id(), map_info, std::cerr)) {
                manifest_store_->flush();
            }
        }
        rebuild_room_spawn_id_cache();
    }

    return final_key;
}

void RoomEditor::ensure_spawn_group_config_ui() {
    if (spawn_group_panel_) {
        return;
    }

    spawn_group_panel_ = std::make_unique<SpawnGroupConfig>();
    if (!spawn_group_panel_) {
        return;
    }

    spawn_group_panel_->set_show_header(true);
    spawn_group_panel_->set_close_button_enabled(true);
    spawn_group_panel_->set_scroll_enabled(true);
    spawn_group_panel_->set_visible(false);
    spawn_group_panel_->set_expanded(true);
    spawn_group_panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
    spawn_group_panel_->set_on_close([this]() {
        if (suppress_spawn_group_close_clear_) {
            suppress_spawn_group_close_clear_ = false;
            return;
        }
        clear_active_spawn_group_target();
    });

    SpawnGroupConfig::Callbacks callbacks{};
    callbacks.on_add = [this]() { add_spawn_group_internal(); };
    callbacks.on_delete = [this](const std::string& id) { delete_spawn_group_internal(id); };
    callbacks.on_reorder = [this](const std::string& id, size_t index) {
        reorder_spawn_group_internal(id, index);
    };
    callbacks.on_regenerate = [this](const std::string& id) {
        if (id.empty()) {
            return;
        }
        if (nlohmann::json* entry = find_spawn_entry(id)) {
            respawn_spawn_group(*entry);
        }
    };
    spawn_group_panel_->set_callbacks(std::move(callbacks));
    spawn_group_panel_->set_on_layout_changed([this]() { update_spawn_group_config_anchor(); });
}

void RoomEditor::update_room_config_bounds() {
    const int side_margin = 0; // Align flush to the right edge
    const int available_width = std::max(0, screen_w_ - 2 * side_margin);
    const int max_width = std::max(320, available_width);
    const int desired_width = std::max(360, screen_w_ / 3);
    const int width = std::min(max_width, desired_width);
    const int height = std::max(1, screen_h_);
    const int max_x = std::max(0, screen_w_ - width);
    const int desired_x = screen_w_ - width; // no right margin gap
    const int x = std::clamp(desired_x, 0, max_x);
    const int y = 0;
    room_config_bounds_ = SDL_Rect{x, y, width, height};
    if (room_cfg_ui_ && room_config_dock_open_) {
        room_cfg_ui_->set_bounds(room_config_bounds_);
    }
    refresh_room_config_visibility();
}

void RoomEditor::configure_shared_panel() {
    if (!shared_footer_bar_) {
        return;
    }
    shared_footer_bar_->set_bounds(screen_w_, screen_h_);
}

void RoomEditor::refresh_room_config_visibility() {
    ensure_room_configurator();
    if (!room_cfg_ui_) {
        return;
    }
    if (active_modal_ == ActiveModal::AssetInfo) {
        room_cfg_ui_->close();
        update_spawn_group_config_anchor();
        return;
    }
    if (room_config_dock_open_) {
        room_cfg_ui_->set_bounds(room_config_bounds_);
        room_cfg_ui_->open(current_room_);
    } else {
        room_cfg_ui_->close();
    }
    update_spawn_group_config_anchor();
}

void RoomEditor::handle_delete_shortcut(const Input& input) {
    if (!input.wasScancodePressed(SDL_SCANCODE_DELETE)) return;
    if (selected_assets_.empty() || !active_assets_ || !current_room_) return;

    Asset* primary = selected_assets_.front();
    if (!primary) return;
    const std::string& spawn_id = primary->spawn_id;
    if (spawn_id.empty()) return;

    delete_spawn_group_internal(spawn_id);
    clear_selection();
}

void RoomEditor::begin_drag_session(const SDL_Point& world_mouse, bool ctrl_modifier) {
    drag_mode_ = DragMode::None;
    drag_states_.clear();
    drag_spawn_id_.clear();
    drag_perimeter_base_radius_ = 0.0;
    drag_moved_ = false;
    drag_room_center_ = get_room_center();
    drag_last_world_ = world_mouse;
    drag_anchor_asset_ = nullptr;
    drag_edge_area_ = nullptr;
    drag_edge_center_ = drag_room_center_;
    drag_edge_inset_percent_ = 100.0;

    if (selected_assets_.empty()) return;
    Asset* primary = selected_assets_.front();
    if (!primary) return;

    drag_anchor_asset_ = primary;
    drag_spawn_id_ = primary->spawn_id;

    MapGridSettings map_settings = current_room_ ? current_room_->map_grid_settings() : MapGridSettings::defaults();
    map_settings.clamp();
    drag_resolution_ = vibble::grid::clamp_resolution(map_settings.resolution);
    nlohmann::json* spawn_entry = nullptr;
    if (!drag_spawn_id_.empty()) {
        spawn_entry = find_spawn_entry(drag_spawn_id_);
        if (spawn_entry) {
            drag_resolution_ = vibble::grid::clamp_resolution(spawn_entry->value("resolution", drag_resolution_));
        }
    }

    const std::string& method = primary->spawn_method;
    if (method == "Exact" || method == "Exact Position") {
        drag_mode_ = DragMode::Exact;
    } else if (method == "Percent") {
        drag_mode_ = DragMode::Percent;
    } else if (method == "Perimeter") {
        drag_mode_ = ctrl_modifier ? DragMode::PerimeterCenter : DragMode::Perimeter;
    } else if (method == "Edge") {
        drag_mode_ = DragMode::Edge;
    } else if (method == "Random") {
        drag_mode_ = DragMode::None;
        dragging_ = false;
        drag_states_.clear();
        return;
    } else {
        drag_mode_ = DragMode::Free;
    }

    auto [room_w, room_h] = get_room_dimensions();
    drag_perimeter_curr_w_ = room_w;
    drag_perimeter_curr_h_ = room_h;
    drag_perimeter_orig_w_ = std::max(1, room_w);
    drag_perimeter_orig_h_ = std::max(1, room_h);
    drag_perimeter_center_offset_world_ = SDL_Point{0, 0};
    drag_perimeter_circle_center_ = drag_room_center_;

    if (spawn_entry) {
        drag_perimeter_orig_w_ = std::max(1, spawn_entry->value("origional_width", drag_perimeter_curr_w_));
        drag_perimeter_orig_h_ = std::max(1, spawn_entry->value("origional_height", drag_perimeter_curr_h_));
        const int stored_dx = spawn_entry->value("dx", 0);
        const int stored_dy = spawn_entry->value("dy", 0);
        RelativeRoomPosition relative(SDL_Point{stored_dx, stored_dy},
                                      drag_perimeter_orig_w_,
                                      drag_perimeter_orig_h_);
        drag_perimeter_center_offset_world_ = relative.scaled_offset(drag_perimeter_curr_w_, drag_perimeter_curr_h_);
        drag_perimeter_circle_center_.x = drag_room_center_.x + drag_perimeter_center_offset_world_.x;
        drag_perimeter_circle_center_.y = drag_room_center_.y + drag_perimeter_center_offset_world_.y;
        if ((*spawn_entry).contains("radius") && (*spawn_entry)["radius"].is_number_integer()) {
            drag_perimeter_base_radius_ = std::max(0, (*spawn_entry)["radius"].get<int>());
        }
    }

    if (drag_mode_ == DragMode::Edge) {
        if (spawn_entry) {
            drag_edge_area_ = find_edge_area_for_entry(*spawn_entry);
            drag_edge_inset_percent_ = static_cast<double>(std::clamp(spawn_entry->value("edge_inset_percent", 100), 0, 200));
        } else {
            drag_edge_area_ = current_room_ ? current_room_->room_area.get() : nullptr;
            drag_edge_inset_percent_ = 100.0;
        }
        if (drag_edge_area_) {
            SDL_Point center = drag_edge_area_->get_center();
            drag_edge_center_ = center;
        } else {
            drag_edge_center_ = drag_room_center_;
        }
    }

    if (drag_mode_ == DragMode::Perimeter || drag_mode_ == DragMode::PerimeterCenter) {
        if (drag_perimeter_base_radius_ <= 0.0) {
            double dx = static_cast<double>(primary->pos.x - drag_perimeter_circle_center_.x);
            double dy = static_cast<double>(primary->pos.y - drag_perimeter_circle_center_.y);
            drag_perimeter_base_radius_ = std::hypot(dx, dy);
        }
        if (!std::isfinite(drag_perimeter_base_radius_) || drag_perimeter_base_radius_ <= 0.0) {
            drag_perimeter_base_radius_ = 0.0;
        }
    }

    drag_states_.reserve(selected_assets_.size());
    for (Asset* asset : selected_assets_) {
        if (!asset) continue;
        DraggedAssetState state;
        state.asset = asset;
        state.start_pos = asset->pos;
        state.active = true;
        if (drag_mode_ == DragMode::Perimeter) {
            double dx = static_cast<double>(asset->pos.x - drag_perimeter_circle_center_.x);
            double dy = static_cast<double>(asset->pos.y - drag_perimeter_circle_center_.y);
            double len = std::hypot(dx, dy);
            if (len > 1e-6) {
                state.direction.x = static_cast<float>(dx / len);
                state.direction.y = static_cast<float>(dy / len);
            } else {
                state.direction.x = 0.0f;
                state.direction.y = -1.0f;
            }
        } else if (drag_mode_ == DragMode::Edge) {
            double dx = static_cast<double>(asset->pos.x - drag_edge_center_.x);
            double dy = static_cast<double>(asset->pos.y - drag_edge_center_.y);
            double len = std::hypot(dx, dy);
            if (len > 1e-6) {
                state.direction.x = static_cast<float>(dx / len);
                state.direction.y = static_cast<float>(dy / len);
            } else {
                state.direction.x = 0.0f;
                state.direction.y = -1.0f;
                len = 1.0;
            }
            if (drag_edge_area_) {
                state.edge_length = edge_length_along_direction(*drag_edge_area_, drag_edge_center_, state.direction);
            }
            if (state.edge_length <= 1e-6) {
                state.edge_length = len;
            }
        }
        drag_states_.push_back(state);
    }

}

void RoomEditor::update_drag_session(const SDL_Point& world_mouse) {
    if (drag_states_.empty()) {
        drag_last_world_ = world_mouse;
        return;
    }

    if (drag_mode_ == DragMode::Perimeter) {
        apply_perimeter_drag(world_mouse);
        drag_last_world_ = world_mouse;
        return;
    }
    if (drag_mode_ == DragMode::Edge) {
        apply_edge_drag(world_mouse);
        drag_last_world_ = world_mouse;
        return;
    }

    SDL_Point delta{world_mouse.x - drag_last_world_.x, world_mouse.y - drag_last_world_.y};
    if (delta.x == 0 && delta.y == 0) {
        drag_last_world_ = world_mouse;
        return;
    }

    for (auto& state : drag_states_) {
        if (!state.asset) continue;
        state.asset->pos.x += delta.x;
        state.asset->pos.y += delta.y;
    }
    if (drag_mode_ == DragMode::PerimeterCenter) {
        drag_perimeter_circle_center_.x += delta.x;
        drag_perimeter_circle_center_.y += delta.y;
        drag_perimeter_center_offset_world_.x += delta.x;
        drag_perimeter_center_offset_world_.y += delta.y;
    }
    snap_dragged_assets_to_grid();
    drag_last_world_ = world_mouse;
    drag_moved_ = true;
}

void RoomEditor::apply_perimeter_drag(const SDL_Point& world_mouse) {
    if (drag_states_.empty()) return;

    const DraggedAssetState* ref = nullptr;
    for (const auto& state : drag_states_) {
        if (state.asset == drag_anchor_asset_) {
            ref = &state;
            break;
        }
    }
    if (!ref) ref = &drag_states_.front();

    auto compute_start_distance = [this](const DraggedAssetState& state) {
        double dx = static_cast<double>(state.start_pos.x - drag_perimeter_circle_center_.x);
        double dy = static_cast<double>(state.start_pos.y - drag_perimeter_circle_center_.y);
        return std::hypot(dx, dy);
};

    double reference_length = compute_start_distance(*ref);
    if (reference_length <= 1e-6) {
        double dx = static_cast<double>(ref->asset->pos.x - drag_perimeter_circle_center_.x);
        double dy = static_cast<double>(ref->asset->pos.y - drag_perimeter_circle_center_.y);
        reference_length = std::hypot(dx, dy);
    }
    if (reference_length <= 1e-6) reference_length = 1.0;

    double base_radius = drag_perimeter_base_radius_;
    if (base_radius <= 1e-6) base_radius = reference_length;

    double new_radius = std::hypot(static_cast<double>(world_mouse.x - drag_perimeter_circle_center_.x),
                                   static_cast<double>(world_mouse.y - drag_perimeter_circle_center_.y));
    if (!std::isfinite(new_radius)) {
        new_radius = 0.0;
    }

    double ratio = base_radius > 1e-6 ? new_radius / base_radius : 0.0;
    if (!std::isfinite(ratio)) ratio = 0.0;
    if (ratio < 0.0) ratio = 0.0;

    bool changed = false;
    for (auto& state : drag_states_) {
        if (!state.asset) continue;
        double base = compute_start_distance(state);
        SDL_FPoint state_dir = state.direction;
        if (base <= 0.0 || (state_dir.x == 0.0f && state_dir.y == 0.0f)) {
            double dx = static_cast<double>(state.asset->pos.x - drag_perimeter_circle_center_.x);
            double dy = static_cast<double>(state.asset->pos.y - drag_perimeter_circle_center_.y);
            double len = std::hypot(dx, dy);
            if (base <= 0.0) base = len;
            if (len > 1e-6) {
                state_dir.x = static_cast<float>(dx / len);
                state_dir.y = static_cast<float>(dy / len);
            }
        }
        double desired = base * ratio;
        int new_x = drag_perimeter_circle_center_.x + static_cast<int>(std::lround(state_dir.x * desired));
        int new_y = drag_perimeter_circle_center_.y + static_cast<int>(std::lround(state_dir.y * desired));
        if (state.asset->pos.x != new_x || state.asset->pos.y != new_y) {
            state.asset->pos.x = new_x;
            state.asset->pos.y = new_y;
            changed = true;
        }
    }
    if (changed) {
        drag_moved_ = true;
    }
    snap_dragged_assets_to_grid();
}

void RoomEditor::apply_edge_drag(const SDL_Point& world_mouse) {
    const SDL_Point center = drag_edge_center_;

    const DraggedAssetState* ref = nullptr;
    if (!drag_states_.empty()) {
        for (const auto& state : drag_states_) {
            if (state.asset == drag_anchor_asset_) {
                ref = &state;
                break;
            }
        }
        if (!ref) {
            ref = &drag_states_.front();
        }
    }

    SDL_FPoint reference_direction{0.0f, 0.0f};
    double reference_length = 0.0;

    if (ref) {
        reference_direction = ref->direction;
        double dir_len = std::hypot(static_cast<double>(reference_direction.x),
                                    static_cast<double>(reference_direction.y));
        if (dir_len > 1e-6) {
            reference_direction.x = static_cast<float>(reference_direction.x / dir_len);
            reference_direction.y = static_cast<float>(reference_direction.y / dir_len);
        } else {
            reference_direction.x = 0.0f;
            reference_direction.y = 0.0f;
        }

        reference_length = ref->edge_length;
        if (reference_length <= 1e-6 && ref->asset) {
            double dx = static_cast<double>(ref->asset->pos.x - center.x);
            double dy = static_cast<double>(ref->asset->pos.y - center.y);
            reference_length = std::hypot(dx, dy);
        }
    }

    double dx_mouse = static_cast<double>(world_mouse.x - center.x);
    double dy_mouse = static_cast<double>(world_mouse.y - center.y);
    double mouse_len = std::hypot(dx_mouse, dy_mouse);

    if ((reference_direction.x == 0.0f && reference_direction.y == 0.0f) && mouse_len > 1e-6) {
        reference_direction.x = static_cast<float>(dx_mouse / mouse_len);
        reference_direction.y = static_cast<float>(dy_mouse / mouse_len);
    }

    if (reference_length <= 1e-6 && drag_edge_area_ &&
        !(reference_direction.x == 0.0f && reference_direction.y == 0.0f)) {
        reference_length = edge_length_along_direction(*drag_edge_area_, center, reference_direction);
    }

    if (reference_length <= 1e-6) {
        reference_length = mouse_len;
    }
    if (!std::isfinite(reference_length) || reference_length <= 1e-6) {
        reference_length = 1.0;
    }

    double projected = dx_mouse * static_cast<double>(reference_direction.x) +
                       dy_mouse * static_cast<double>(reference_direction.y);
    double ratio = projected / reference_length;
    if (!std::isfinite(ratio)) {
        ratio = 0.0;
    }
    ratio = std::clamp(ratio, 0.0, 2.0);

    int snapped_percent = std::clamp(static_cast<int>(std::lround(ratio * 100.0)), 0, 200);
    double snapped_ratio = static_cast<double>(snapped_percent) / 100.0;

    bool assets_changed = false;
    for (auto& state : drag_states_) {
        if (!state.asset) continue;
        double base_length = state.edge_length;
        if (base_length <= 1e-6) {
            double dx = static_cast<double>(state.asset->pos.x - center.x);
            double dy = static_cast<double>(state.asset->pos.y - center.y);
            base_length = std::hypot(dx, dy);
        }
        SDL_FPoint dir = state.direction;
        double dir_len = std::hypot(static_cast<double>(dir.x), static_cast<double>(dir.y));
        if (dir_len > 1e-6) {
            dir.x = static_cast<float>(dir.x / dir_len);
            dir.y = static_cast<float>(dir.y / dir_len);
        } else if (base_length > 1e-6) {
            double dx = static_cast<double>(state.asset->pos.x - center.x);
            double dy = static_cast<double>(state.asset->pos.y - center.y);
            double len = std::hypot(dx, dy);
            if (len > 1e-6) {
                dir.x = static_cast<float>(dx / len);
                dir.y = static_cast<float>(dy / len);
            }
        }
        state.direction = dir;
        double desired = base_length * snapped_ratio;
        int new_x = center.x + static_cast<int>(std::lround(static_cast<double>(dir.x) * desired));
        int new_y = center.y + static_cast<int>(std::lround(static_cast<double>(dir.y) * desired));
        if (state.asset->pos.x != new_x || state.asset->pos.y != new_y) {
            state.asset->pos.x = new_x;
            state.asset->pos.y = new_y;
            assets_changed = true;
        }
    }

    double previous_percent = drag_edge_inset_percent_;
    drag_edge_inset_percent_ = static_cast<double>(snapped_percent);

    if (assets_changed) {
        drag_moved_ = true;
    }
    if (std::fabs(previous_percent - drag_edge_inset_percent_) > 1e-6) {
        drag_moved_ = true;
    }

    snap_dragged_assets_to_grid();
}

void RoomEditor::snap_dragged_assets_to_grid() {
    if (drag_states_.empty()) return;
    const int resolution = vibble::grid::clamp_resolution(drag_resolution_);
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    bool changed = false;

    if (drag_mode_ == DragMode::PerimeterCenter) {
        SDL_Point snapped_center = grid_service.snap_to_vertex(drag_perimeter_circle_center_, resolution);
        if (snapped_center.x != drag_perimeter_circle_center_.x || snapped_center.y != drag_perimeter_circle_center_.y) {
            const int dx = snapped_center.x - drag_perimeter_circle_center_.x;
            const int dy = snapped_center.y - drag_perimeter_circle_center_.y;
            drag_perimeter_circle_center_ = snapped_center;
            drag_perimeter_center_offset_world_.x += dx;
            drag_perimeter_center_offset_world_.y += dy;
            for (auto& state : drag_states_) {
                if (!state.asset) continue;
                state.asset->pos.x += dx;
                state.asset->pos.y += dy;
            }
            changed = true;
        }
    }

    for (auto& state : drag_states_) {
        if (!state.asset) continue;
        SDL_Point current{state.asset->pos.x, state.asset->pos.y};
        SDL_Point snapped = grid_service.snap_to_vertex(current, resolution);
        if (snapped.x != state.asset->pos.x || snapped.y != state.asset->pos.y) {
            state.asset->pos.x = snapped.x;
            state.asset->pos.y = snapped.y;
            changed = true;
        }
    }

    if (changed) {
        drag_moved_ = true;
    }
}

void RoomEditor::finalize_drag_session() {
    if (drag_states_.empty()) {
        reset_drag_state();
        return;
    }

    Asset* primary = selected_assets_.empty() ? nullptr : selected_assets_.front();
    if (!primary) {
        reset_drag_state();
        return;
    }

    const bool drag_was_moved = drag_moved_;
    bool json_modified = false;
    SDL_Point center = get_room_center();
    auto [width, height] = get_room_dimensions();

    if (!drag_spawn_id_.empty()) {
        if (nlohmann::json* entry = find_spawn_entry(drag_spawn_id_)) {
            switch (drag_mode_) {
                case DragMode::Exact:
                    if (drag_moved_) {
                        update_exact_json(*entry, *primary, center, width, height);
                        json_modified = true;
                    }
                    break;
                case DragMode::Percent:
                    if (drag_moved_) {
                        update_percent_json(*entry, *primary, center, width, height);
                        json_modified = true;
                    }
                    break;
                case DragMode::Perimeter:
                    if (drag_moved_) {
                        const int curr_w = std::max(1, drag_perimeter_curr_w_ > 0 ? drag_perimeter_curr_w_ : width);
                        const int curr_h = std::max(1, drag_perimeter_curr_h_ > 0 ? drag_perimeter_curr_h_ : height);
                        const int orig_w = std::max(1, drag_perimeter_orig_w_ > 0 ? drag_perimeter_orig_w_ : curr_w);
                        const int orig_h = std::max(1, drag_perimeter_orig_h_ > 0 ? drag_perimeter_orig_h_ : curr_h);
                        SDL_Point stored = RelativeRoomPosition::ToOriginal(drag_perimeter_center_offset_world_, orig_w, orig_h, curr_w, curr_h);
                        const double dist = std::hypot(static_cast<double>(primary->pos.x - drag_perimeter_circle_center_.x), static_cast<double>(primary->pos.y - drag_perimeter_circle_center_.y));
                        const int radius = static_cast<int>(std::lround(dist));
                        save_perimeter_json(*entry, stored.x, stored.y, orig_w, orig_h, radius);
                        json_modified = true;
                    }
                    break;
                case DragMode::PerimeterCenter:
                    if (drag_moved_) {
                        const int curr_w = std::max(1, drag_perimeter_curr_w_ > 0 ? drag_perimeter_curr_w_ : width);
                        const int curr_h = std::max(1, drag_perimeter_curr_h_ > 0 ? drag_perimeter_curr_h_ : height);
                        const int orig_w = std::max(1, drag_perimeter_orig_w_ > 0 ? drag_perimeter_orig_w_ : curr_w);
                        const int orig_h = std::max(1, drag_perimeter_orig_h_ > 0 ? drag_perimeter_orig_h_ : curr_h);
                        SDL_Point stored = RelativeRoomPosition::ToOriginal(drag_perimeter_center_offset_world_, orig_w, orig_h, curr_w, curr_h);
                        const double dist = std::hypot(static_cast<double>(primary->pos.x - drag_perimeter_circle_center_.x), static_cast<double>(primary->pos.y - drag_perimeter_circle_center_.y));
                        const int radius = static_cast<int>(std::lround(dist));
                        save_perimeter_json(*entry, stored.x, stored.y, orig_w, orig_h, radius);
                        json_modified = true;
                    }
                    break;
                case DragMode::Edge:
                    if (drag_moved_) {
                        int inset = static_cast<int>(std::lround(drag_edge_inset_percent_));
                        inset = std::clamp(inset, 0, 200);
                        save_edge_json(*entry, inset);
                        json_modified = true;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (json_modified) {
        save_current_room_assets_json();
        if (!drag_spawn_id_.empty()) {
            active_spawn_group_id_ = drag_spawn_id_;
        }
        refresh_spawn_group_config_ui();
    }

    if (drag_was_moved) {
        suppress_next_left_click_ = true;
    }

    reset_drag_state();
}

void RoomEditor::reset_drag_state() {
    dragging_ = false;
    drag_anchor_asset_ = nullptr;
    drag_mode_ = DragMode::None;
    drag_states_.clear();
    drag_last_world_ = SDL_Point{0, 0};
    drag_room_center_ = SDL_Point{0, 0};
    drag_perimeter_circle_center_ = SDL_Point{0, 0};
    drag_perimeter_base_radius_ = 0.0;
    drag_perimeter_center_offset_world_ = SDL_Point{0, 0};
    drag_perimeter_orig_w_ = 0;
    drag_perimeter_orig_h_ = 0;
    drag_perimeter_curr_w_ = 0;
    drag_resolution_ = 0;
    drag_perimeter_curr_h_ = 0;
    drag_edge_area_ = nullptr;
    drag_edge_center_ = SDL_Point{0, 0};
    drag_edge_inset_percent_ = 100.0;
    drag_moved_ = false;
    drag_spawn_id_.clear();
}

nlohmann::json* RoomEditor::find_spawn_entry(const std::string& spawn_id) {
    if (!current_room_ || spawn_id.empty()) return nullptr;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    for (auto& entry : arr) {
        if (!entry.is_object()) continue;
        if (entry.contains("spawn_id") && entry["spawn_id"].is_string() &&
            entry["spawn_id"].get<std::string>() == spawn_id) {
            return &entry;
        }
    }
    return nullptr;
}

RoomEditor::SpawnEntryResolution RoomEditor::locate_spawn_entry(const std::string& spawn_id) {
    SpawnEntryResolution result;
    if (spawn_id.empty()) {
        return result;
    }

    if (current_room_) {
        auto& root = current_room_->assets_data();
        nlohmann::json& arr = ensure_spawn_groups_array(root);
        if (nlohmann::json* entry = find_spawn_entry(spawn_id)) {
            result.entry = entry;
            result.owner_array = &arr;
            result.source = SpawnEntryResolution::Source::Room;
            return result;
        }
    }

    if (assets_) {
        nlohmann::json& map_info = assets_->map_info_json();
        nlohmann::json* owner = nullptr;
        if (nlohmann::json* entry = find_spawn_entry_recursive(map_info, spawn_id, &owner)) {
            result.entry = entry;
            result.owner_array = owner;
            result.source = SpawnEntryResolution::Source::Map;
        }
    }

    return result;
}

const Area* RoomEditor::find_edge_area_for_entry(const nlohmann::json& entry) const {
    if (!current_room_) {
        return nullptr;
    }
    const std::string area_name = entry.value("area", std::string{});
    if (!area_name.empty()) {
        if (Area* area = current_room_->find_area(area_name)) {
            return area;
        }
    }
    if (current_room_->room_area) {
        return current_room_->room_area.get();
    }
    return nullptr;
}

SDL_Point RoomEditor::get_room_center() const {
    if (current_room_ && current_room_->room_area) {
        return current_room_->room_area->get_center();
    }
    return SDL_Point{0, 0};
}

std::pair<int, int> RoomEditor::get_room_dimensions() const {
    if (!current_room_ || !current_room_->room_area) return {0, 0};
    auto bounds = current_room_->room_area->get_bounds();
    int width = std::max(0, std::get<2>(bounds) - std::get<0>(bounds));
    int height = std::max(0, std::get<3>(bounds) - std::get<1>(bounds));
    return {width, height};
}

void RoomEditor::refresh_spawn_group_config_ui() {
    if (!current_room_) {
        if (spawn_group_panel_) {
            spawn_group_panel_->set_visible(false);
        }
        return;
    }
    ensure_spawn_group_config_ui();
    if (!spawn_group_panel_) {
        return;
    }

    spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
    spawn_group_panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    auto reopen = spawn_group_panel_->expanded_groups();

    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    if (sanitize_perimeter_spawn_groups(arr)) {
        save_current_room_assets_json();
    }
    rebuild_room_spawn_id_cache();

    const int default_resolution = current_room_->map_grid_settings().resolution;
    spawn_group_panel_->set_default_resolution(default_resolution);

    auto area_names_provider = [this]() {
        std::vector<std::string> names;
        if (!current_room_) {
            return names;
        }
        auto& data = current_room_->assets_data();
        if (data.contains("areas") && data["areas"].is_array()) {
            for (const auto& entry : data["areas"]) {
                if (!entry.is_object()) continue;
                const auto name_it = entry.find("name");
                if (name_it != entry.end() && name_it->is_string()) {
                    names.push_back(name_it->get<std::string>());
                }
            }
        }
        if (names.empty()) {
            for (const auto& named : current_room_->areas) {
                if (!named.name.empty()) {
                    names.push_back(named.name);
                }
            }
        }
        return names;
    };

    auto on_change = [this]() {
        if (!current_room_) {
            return;
        }
        save_current_room_assets_json();
        rebuild_room_spawn_id_cache();
        reopen_room_configurator();
    };

    auto on_entry_change = [this](const nlohmann::json& entry, const SpawnGroupConfig::ChangeSummary& summary) {
        if (!current_room_) {
            return;
        }
        auto& root = current_room_->assets_data();
        auto& arr = ensure_spawn_groups_array(root);
        const bool sanitized = sanitize_perimeter_spawn_groups(arr);
        save_current_room_assets_json();
        rebuild_room_spawn_id_cache();
        reopen_room_configurator();
        if (sanitized || summary.method_changed || summary.quantity_changed || summary.candidates_changed ||
            summary.resolution_changed) {
            respawn_spawn_group(entry);
        }
    };

    SpawnGroupConfig::ConfigureEntryCallback configure_entry = [area_names_provider, this](
                                                                 SpawnGroupConfig::EntryController& entry,
                                                                 const nlohmann::json&) {
        entry.set_area_names_provider(area_names_provider);
        if (current_room_) {
            const std::string label = current_room_->room_name.empty() ? std::string("Room") : current_room_->room_name;
            entry.set_ownership_label(label, SDL_Color{255, 224, 96, 255});
        }
    };

    SpawnEntryResolution resolved;
    if (active_spawn_group_id_) {
        resolved = locate_spawn_entry(*active_spawn_group_id_);
        if (resolved.source == SpawnEntryResolution::Source::Map && resolved.owner_array) {
            if (sanitize_perimeter_spawn_groups(*resolved.owner_array)) {
                if (assets_) {
                    assets_->persist_map_info_json();
                }
            }
        }
    }

    auto map_on_change = [this]() {
        if (!assets_) {
            return;
        }
        assets_->persist_map_info_json();
    };

    auto map_on_entry_change = [this](const nlohmann::json& entry, const SpawnGroupConfig::ChangeSummary& summary) {
        if (!assets_) {
            return;
        }
        bool sanitized = false;
        if (entry.is_object()) {
            const std::string id = entry.value("spawn_id", std::string{});
            SpawnEntryResolution current = locate_spawn_entry(id);
            if (current.owner_array) {
                sanitized = sanitize_perimeter_spawn_groups(*current.owner_array);
            }
        }
        assets_->persist_map_info_json();
        if (sanitized || summary.method_changed || summary.quantity_changed || summary.candidates_changed ||
            summary.resolution_changed) {
            assets_->notify_spawn_group_config_changed(entry);
        }
    };

    if (resolved.valid()) {
        if (resolved.source == SpawnEntryResolution::Source::Room) {
            spawn_group_panel_->bind_entry(*resolved.entry,
                                           on_change,
                                           on_entry_change,
                                           SpawnGroupConfig::EntryCallbacks{},
                                           configure_entry);
        } else {
            spawn_group_panel_->bind_entry(*resolved.entry,
                                           map_on_change,
                                           map_on_entry_change,
                                           SpawnGroupConfig::EntryCallbacks{},
                                           configure_entry);
        }
        spawn_group_panel_->set_scroll_enabled(false);
    } else {
        spawn_group_panel_->load(arr, on_change, on_entry_change, configure_entry);
        spawn_group_panel_->restore_expanded_groups(reopen);
        spawn_group_panel_->set_scroll_enabled(true);
    }
    update_spawn_group_config_anchor();
}

void RoomEditor::update_spawn_group_config_anchor() {
    if (!spawn_group_panel_) {
        return;
    }
    spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
    spawn_group_panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    SDL_Point anchor = spawn_groups_anchor_point();
    spawn_group_panel_->set_anchor(anchor.x, anchor.y);
}

SDL_Point RoomEditor::spawn_groups_anchor_point() const {
    SDL_Rect reference = room_config_bounds_;
    if (room_cfg_ui_) {
        const SDL_Rect rect = room_cfg_ui_->panel_rect();
        if (rect.w > 0 || rect.h > 0) {
            reference = rect;
        }
    }
    int anchor_x = reference.x + reference.w + 16;
    int anchor_y = reference.y;
    return SDL_Point{anchor_x, anchor_y};
}

void RoomEditor::clear_active_spawn_group_target() {
    active_spawn_group_id_.reset();
}

void RoomEditor::sync_spawn_group_panel_with_selection() {
    Asset* primary = selected_assets_.empty() ? nullptr : selected_assets_.front();
    std::string spawn_id;
    if (primary) {
        spawn_id = primary->spawn_id;
    }

    if (spawn_id.empty()) {
        if (spawn_group_panel_) {
            spawn_group_panel_->close();
        }
        clear_active_spawn_group_target();
        return;
    }

    if (active_spawn_group_id_ && *active_spawn_group_id_ == spawn_id &&
        spawn_group_panel_ && spawn_group_panel_->is_visible()) {
        return;
    }

    open_spawn_group_editor_by_id(spawn_id);
}

void RoomEditor::sanitize_perimeter_spawn_groups() {
    if (!current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    if (sanitize_perimeter_spawn_groups(arr)) {
        save_current_room_assets_json();
    }
}

bool RoomEditor::sanitize_perimeter_spawn_groups(nlohmann::json& groups) {
    return devmode::spawn::sanitize_perimeter_spawn_groups(groups);
}

std::optional<RoomEditor::PerimeterOverlay> RoomEditor::compute_perimeter_overlay_for_drag() {
    if (!dragging_) return std::nullopt;
    if (drag_mode_ != DragMode::Perimeter && drag_mode_ != DragMode::PerimeterCenter) {
        return std::nullopt;
    }
    Asset* reference = drag_anchor_asset_;
    if (!reference) {
        for (const auto& state : drag_states_) {
            if (state.asset) {
                reference = state.asset;
                break;
            }
        }
    }
    if (!reference) return std::nullopt;
    PerimeterOverlay overlay;
    overlay.center = drag_perimeter_circle_center_;
    double dx = static_cast<double>(reference->pos.x - overlay.center.x);
    double dy = static_cast<double>(reference->pos.y - overlay.center.y);
    overlay.radius = std::hypot(dx, dy);
    if (!std::isfinite(overlay.radius) || overlay.radius <= 0.0) {
        return std::nullopt;
    }
    return overlay;
}

std::optional<RoomEditor::PerimeterOverlay> RoomEditor::compute_perimeter_overlay_for_spawn(const std::string& spawn_id) {
    if (spawn_id.empty() || !current_room_) return std::nullopt;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    nlohmann::json* entry = nullptr;
    for (auto& item : arr) {
        if (!item.is_object()) continue;
        if (item.contains("spawn_id") && item["spawn_id"].is_string() && item["spawn_id"].get<std::string>() == spawn_id) {
            entry = &item;
            break;
        }
    }
    if (!entry) return std::nullopt;
    std::string method = entry->value("position", std::string{});
    if (method == "Exact Position") method = "Exact";
    if (method != "Perimeter") return std::nullopt;
    PerimeterOverlay overlay;
    overlay.center = get_room_center();
    auto [room_w, room_h] = get_room_dimensions();
    int orig_w = std::max(1, entry->value("origional_width", room_w));
    int orig_h = std::max(1, entry->value("origional_height", room_h));
    int stored_dx = entry->value("dx", 0);
    int stored_dy = entry->value("dy", 0);
    RelativeRoomPosition relative(SDL_Point{stored_dx, stored_dy}, orig_w, orig_h);
    SDL_Point scaled = relative.scaled_offset(room_w, room_h);
    overlay.center.x += scaled.x;
    overlay.center.y += scaled.y;
    overlay.radius = entry->value("radius", 0.0);
    if (overlay.radius <= 0.0 && active_assets_) {
        for (Asset* asset : *active_assets_) {
            if (!asset || asset->spawn_id != spawn_id) continue;
            double dx = static_cast<double>(asset->pos.x - overlay.center.x);
            double dy = static_cast<double>(asset->pos.y - overlay.center.y);
            overlay.radius = std::hypot(dx, dy);
            if (overlay.radius > 0.0) break;
        }
    }
    if (!std::isfinite(overlay.radius) || overlay.radius <= 0.0) {
        return std::nullopt;
    }
    return overlay;
}

void RoomEditor::add_spawn_group_internal() {
    if (!current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    nlohmann::json entry;
    const std::string new_spawn_id = generate_spawn_id();
    entry["spawn_id"] = new_spawn_id;
    const int add_default_resolution = current_room_ ? current_room_->map_grid_settings().resolution
                                                     : MapGridSettings::defaults().resolution;
    devmode::spawn::ensure_spawn_group_entry_defaults(entry, "New Spawn", add_default_resolution);
    arr.push_back(entry);

    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i].is_object()) arr[i]["priority"] = static_cast<int>(i);
    }
    sanitize_perimeter_spawn_groups(arr);
    save_current_room_assets_json();
    rebuild_room_spawn_id_cache();
    active_spawn_group_id_ = new_spawn_id;
    refresh_spawn_group_config_ui();
    reopen_room_configurator();
    open_spawn_group_editor_by_id(new_spawn_id);
}

void RoomEditor::delete_spawn_group_internal(const std::string& spawn_id) {
    if (!remove_spawn_group_by_id(spawn_id)) {
        return;
    }
    save_current_room_assets_json();
    if (active_spawn_group_id_ && *active_spawn_group_id_ == spawn_id) {
        clear_active_spawn_group_target();
    }
    rebuild_room_spawn_id_cache();
    refresh_spawn_group_config_ui();
    reopen_room_configurator();
    if (assets_) {
        assets_->refresh_active_asset_lists();
    }
}

bool RoomEditor::remove_spawn_group_by_id(const std::string& spawn_id) {
    if (spawn_id.empty() || !current_room_) return false;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    auto it = std::remove_if(arr.begin(), arr.end(), [&](nlohmann::json& entry) {
        if (!entry.is_object()) return false;
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) return false;
        return entry["spawn_id"].get<std::string>() == spawn_id;
    });
    if (it == arr.end()) {
        return false;
    }
    arr.erase(it, arr.end());

    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i].is_object()) arr[i]["priority"] = static_cast<int>(i);
    }

    if (assets_) {
        std::vector<Asset*> to_delete;
        for (Asset* asset : assets_->all) {
            if (!asset || asset->dead) continue;
            if (asset == player_) continue;
            if (asset->spawn_id == spawn_id) {
                to_delete.push_back(asset);
            }
        }
        for (Asset* asset : to_delete) {
            purge_asset(asset);
            auto& all = assets_->all;
            all.erase(std::remove(all.begin(), all.end(), asset), all.end());
            asset->Delete();
        }
    }
    return true;
}

void RoomEditor::move_spawn_group_internal(const std::string& spawn_id, int dir) {
    if (!current_room_ || spawn_id.empty() || (dir != -1 && dir != +1)) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    if (!arr.is_array() || arr.size() <= 1) return;
    size_t current_index = arr.size();
    for (size_t i = 0; i < arr.size(); ++i) {
        const auto& e = arr[i];
        if (!e.is_object()) continue;
        if (e.contains("spawn_id") && e["spawn_id"].is_string() && e["spawn_id"].get<std::string>() == spawn_id) {
            current_index = i;
            break;
        }
    }
    if (current_index >= arr.size()) return;
    const int target = static_cast<int>(current_index) + dir;
    if (target < 0 || target >= static_cast<int>(arr.size())) return;
    reorder_spawn_group_internal(spawn_id, static_cast<size_t>(target));
}

void RoomEditor::reorder_spawn_group_internal(const std::string& spawn_id, size_t target_index) {
    if (!current_room_ || spawn_id.empty()) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    if (!arr.is_array() || arr.empty()) return;

    size_t current_index = arr.size();
    for (size_t i = 0; i < arr.size(); ++i) {
        const auto& entry = arr[i];
        if (!entry.is_object()) continue;
        if (entry.contains("spawn_id") && entry["spawn_id"].is_string() && entry["spawn_id"].get<std::string>() == spawn_id) {
            current_index = i;
            break;
        }
    }
    if (current_index >= arr.size()) return;

    const size_t bounded_index = std::min(target_index, arr.size() - 1);
    if (current_index == bounded_index) return;

    nlohmann::json entry = std::move(arr[current_index]);
    const auto erase_pos = arr.begin() + static_cast<nlohmann::json::difference_type>(current_index);
    arr.erase(erase_pos);
    size_t insert_index = std::min(bounded_index, arr.size());
    const auto insert_pos = arr.begin() + static_cast<nlohmann::json::difference_type>(insert_index);
    arr.insert(insert_pos, std::move(entry));

    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i].is_object()) arr[i]["priority"] = static_cast<int>(i);
    }
    save_current_room_assets_json();
    rebuild_room_spawn_id_cache();
    refresh_spawn_group_config_ui();
    reopen_room_configurator();
}

void RoomEditor::open_spawn_group_editor_by_id(const std::string& spawn_id) {
    if (spawn_id.empty()) {
        return;
    }
    if (!current_room_) {
        return;
    }

    ensure_spawn_group_config_ui();
    if (!spawn_group_panel_) {
        return;
    }

    active_spawn_group_id_ = spawn_id;

    refresh_spawn_group_config_ui();

    SDL_Point anchor = spawn_groups_anchor_point();
    spawn_group_panel_->set_anchor(anchor.x, anchor.y);
    spawn_group_panel_->set_screen_dimensions(screen_w_, screen_h_);
    spawn_group_panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});

    FloatingDockableManager::instance().open_floating(
        "Spawn Group Config",
        spawn_group_panel_.get(),
        [this]() {
            if (!spawn_group_panel_) {
                return;
            }
            spawn_group_panel_->set_visible(false);
        });

    spawn_group_panel_->set_visible(true);
    spawn_group_panel_->set_expanded(true);
    spawn_group_panel_->request_open_spawn_group(spawn_id, anchor.x, anchor.y);
    Input dummy;
    spawn_group_panel_->update(dummy, screen_w_, screen_h_);
}

void RoomEditor::reopen_room_configurator() {
    if (!room_cfg_ui_) return;
    if (!room_config_dock_open_) {
        return;
    }
    if (!room_cfg_ui_->refresh_spawn_groups(current_room_)) {
        room_cfg_ui_->open(current_room_);
    }
}

void RoomEditor::rebuild_room_spawn_id_cache() {
    room_spawn_ids_.clear();
    if (!current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    for (const auto& entry : arr) {
        if (!entry.is_object()) continue;
        if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
            room_spawn_ids_.insert(entry["spawn_id"].get<std::string>());
        }
    }
}

bool RoomEditor::is_room_spawn_id(const std::string& spawn_id) const {
    if (spawn_id.empty()) return false;
    return room_spawn_ids_.find(spawn_id) != room_spawn_ids_.end();
}

bool RoomEditor::asset_belongs_to_room(const Asset* ) const {
    return true;
}

void RoomEditor::handle_spawn_config_change(const nlohmann::json& entry) {

    respawn_spawn_group(entry);
}

std::unique_ptr<vibble::grid::Occupancy> RoomEditor::build_room_grid(const std::string& ignore_spawn_id) const {
    if (!current_room_ || !current_room_->room_area) return nullptr;
    MapGridSettings grid_settings = current_room_->map_grid_settings();
    const int resolution = std::max(0, grid_settings.resolution);
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    auto occupancy = std::make_unique<vibble::grid::Occupancy>(*current_room_->room_area, resolution, grid_service);
    if (!assets_) return occupancy;
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) continue;
        if (!asset_belongs_to_room(asset)) continue;
        if (!asset->spawn_id.empty() && asset->spawn_id == ignore_spawn_id) continue;
        SDL_Point pos{asset->pos.x, asset->pos.y};
        if (current_room_->room_area && !current_room_->room_area->contains_point(pos)) continue;
        if (auto* vertex = occupancy->vertex_at_world(pos)) {
            occupancy->set_occupied(vertex, true);
        }
    }
    return occupancy;
}

void RoomEditor::integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned) {
    if (!assets_) return;
    if (spawned.empty()) return;
    for (auto& uptr : spawned) {
        if (!uptr) continue;
        Asset* raw = uptr.get();
        set_camera_recursive(raw, &assets_->getView());
        set_assets_owner_recursive(raw, assets_);
        raw->finalize_setup();
        assets_->owned_assets.emplace_back(std::move(uptr));
        assets_->all.push_back(raw);
    }
    assets_->initialize_active_assets(assets_->getView().get_screen_center());
    assets_->refresh_active_asset_lists();
    spawned.clear();
}

void RoomEditor::respawn_spawn_group(const nlohmann::json& entry) {
    if (!assets_ || !current_room_ || !current_room_->room_area) return;
    if (!entry.is_object()) return;
    std::string spawn_id = entry.value("spawn_id", std::string{});
    if (spawn_id.empty()) return;

    std::vector<Asset*> to_remove;
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) continue;
        if (!asset_belongs_to_room(asset)) continue;
        if (asset == player_) continue;
        if (asset->spawn_id == spawn_id) {
            to_remove.push_back(asset);
        }
    }
    for (Asset* asset : to_remove) {
        purge_asset(asset);
        auto& all = assets_->all;
        all.erase(std::remove(all.begin(), all.end(), asset), all.end());
        asset->Delete();
    }

    auto occupancy = build_room_grid(spawn_id);
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();

    nlohmann::json root;
    root["spawn_groups"] = nlohmann::json::array();
    root["spawn_groups"].push_back(entry);
    std::vector<nlohmann::json> sources{root};
    AssetSpawnPlanner planner(sources, *current_room_->room_area, assets_->library());
    const auto& queue = planner.get_spawn_queue();
    if (queue.empty()) return;

    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library = assets_->library().all();
    std::vector<std::unique_ptr<Asset>> spawned;
    std::vector<Area> exclusion;
    std::mt19937 rng(std::random_device{}());
    Check checker(false);
    SpawnContext ctx(rng, checker, exclusion, asset_info_library, spawned, &assets_->library(), grid_service, occupancy.get());
    if (occupancy) {
        ctx.set_spawn_resolution(occupancy->resolution());
    }
    std::vector<const Area*> trail_areas;
    if (current_room_) {
        auto add_trail_area = [&trail_areas](const Area* candidate, const std::string& type) {
            if (!candidate) return;
            std::string lowered = type;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (lowered == "trail") {
                trail_areas.push_back(candidate);
            }
        };
        if (current_room_->room_area) {
            add_trail_area(current_room_->room_area.get(), current_room_->room_area->get_type());
        }
        for (const auto& named : current_room_->areas) {
            add_trail_area(named.area.get(), named.type);
        }
    }
    ctx.set_trail_areas(std::move(trail_areas));
    ExactSpawner exact;
    CenterSpawner center;
    RandomSpawner random;
    PerimeterSpawner perimeter;
    EdgeSpawner edge;
    PercentSpawner percent;
    const Area* area = current_room_->room_area.get();
    for (const auto& info : queue) {
        const std::string& pos = info.position;
        if (pos == "Exact" || pos == "Exact Position") {
            exact.spawn(info, area, ctx);
        } else if (pos == "Center") {
            center.spawn(info, area, ctx);
        } else if (pos == "Perimeter") {
            perimeter.spawn(info, area, ctx);
        } else if (pos == "Edge") {
            edge.spawn(info, area, ctx);
        } else if (pos == "Percent") {
            percent.spawn(info, area, ctx);
        } else {
            random.spawn(info, area, ctx);
        }
    }
    integrate_spawned_assets(spawned);
}

void RoomEditor::regenerate_current_room() {
    if (!assets_ || !current_room_) return;
    auto& room_json = current_room_->assets_data();
    SDL_Point center{0, 0};
    std::unique_ptr<Area> old_area_copy;
    if (current_room_->room_area) {
        auto c = current_room_->room_area->get_center();
        center.x = c.x;
        center.y = c.y;
        old_area_copy = std::make_unique<Area>(*current_room_->room_area);
    }

    std::string player_asset_name;
    if (player_ && player_->info) {
        player_asset_name = player_->info->name;
    } else if (assets_->player && assets_->player->info) {
        player_asset_name = assets_->player->info->name;
    } else {
        for (const auto& [name, info] : assets_->library().all()) {
            if (info && info->type == asset_types::player) {
                player_asset_name = name;
                break;
            }
        }
    }

    int min_w = room_json.value("min_width", 64);
    int max_w = room_json.value("max_width", min_w);
    int min_h = room_json.value("min_height", 64);
    int max_h = room_json.value("max_height", min_h);
    int edge = room_json.value("edge_smoothness", 2);
    std::string geometry = room_json.value("geometry", std::string("Square"));
    if (!geometry.empty()) geometry[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(geometry[0])));

    std::mt19937 rng(std::random_device{}());
    if (min_w > max_w) std::swap(min_w, max_w);
    if (min_h > max_h) std::swap(min_h, max_h);
    std::string lowered_geom = geometry;
    std::transform(lowered_geom.begin(), lowered_geom.end(), lowered_geom.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    int width = 0;
    int height = 0;
    if (lowered_geom == "circle") {
        auto infer_radius = [&](int w_min, int w_max, int h_min, int h_max) {
            int diameter = 0;
            diameter = std::max(diameter, std::max(w_min, w_max));
            diameter = std::max(diameter, std::max(h_min, h_max));
            if (diameter <= 0) return 0;
            return std::max(1, diameter / 2);
};
        int radius_value = room_json.value("radius", -1);
        int min_radius = room_json.value("min_radius", radius_value);
        int max_radius = room_json.value("max_radius", radius_value);
        if (min_radius <= 0 && radius_value > 0) min_radius = radius_value;
        if (max_radius <= 0 && radius_value > 0) max_radius = radius_value;
        if (min_radius <= 0) {
            min_radius = infer_radius(min_w, max_w, min_h, max_h);
        }
        if (max_radius <= 0) {
            max_radius = infer_radius(min_w, max_w, min_h, max_h);
        }
        const int kMinimumRadius = 100;
        if (min_radius < kMinimumRadius) {
            min_radius = kMinimumRadius;
        }
        if (max_radius < kMinimumRadius) {
            max_radius = kMinimumRadius;
        }
        if (max_radius < min_radius) {
            max_radius = min_radius;
        }
        std::uniform_int_distribution<int> dist_r(min_radius, max_radius);
        int chosen_radius = std::max(1, dist_r(rng));
        width = height = chosen_radius * 2;
        int min_diameter = std::max(0, min_radius) * 2;
        int max_diameter = std::max(min_diameter, std::max(0, max_radius) * 2);
        room_json["radius"] = chosen_radius;
        room_json["min_radius"] = min_radius;
        room_json["max_radius"] = max_radius;
        room_json["min_width"] = min_diameter;
        room_json["max_width"] = max_diameter;
        room_json["min_height"] = min_diameter;
        room_json["max_height"] = max_diameter;
    } else {
        std::uniform_int_distribution<int> dist_w(min_w, max_w);
        std::uniform_int_distribution<int> dist_h(min_h, max_h);
        width = std::max(1, dist_w(rng));
        height = std::max(1, dist_h(rng));
        room_json.erase("radius");
    }

    const std::string map_id = assets_ ? assets_->map_id() : std::string{};
    nlohmann::json map_info_json = devmode::room_editor_detail::resolve_map_info_blob(
        assets_,
        manifest_store_,
        map_id);

    double map_radius_value = map_layers::map_radius_from_map_info(map_info_json);
    const int map_radius = map_radius_value > 0.0 ? static_cast<int>(std::lround(map_radius_value)) : 0;
    int map_w = map_radius > 0 ? map_radius * 2 : std::max(width * 2, 1);
    int map_h = map_radius > 0 ? map_radius * 2 : std::max(height * 2, 1);
    Area new_area(current_room_->room_name.empty() ? std::string("room") : current_room_->room_name, center, width, height, geometry, edge, map_w, map_h);

    double old_area_size = old_area_copy ? old_area_copy->get_area() : 0.0;
    double new_area_size = new_area.get_area();

    std::unordered_set<std::string> spawn_ids;
    if (const nlohmann::json* groups = find_spawn_groups_array(room_json)) {
        for (const auto& item : *groups) {
            if (item.contains("spawn_id") && item["spawn_id"].is_string()) {
                spawn_ids.insert(item["spawn_id"].get<std::string>());
            }
        }
    }

    std::vector<Asset*> to_remove;
    std::unordered_set<Asset*> removal_set;
    auto queue_for_removal = [&](Asset* asset) {
        if (!asset || asset->dead) {
            return;
        }
        if (removal_set.insert(asset).second) {
            to_remove.push_back(asset);
        }
    };

    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) continue;
        if (!asset->spawn_id.empty() && spawn_ids.count(asset->spawn_id)) {
            queue_for_removal(asset);
        }
    }

    if (old_area_copy) {
        for (Asset* asset : assets_->all) {
            if (!asset || asset->dead) continue;
            SDL_Point pos{asset->pos.x, asset->pos.y};
            if (old_area_copy->contains_point(pos)) {
                queue_for_removal(asset);
            }
        }
    }

    if (new_area_size > old_area_size) {
        for (Asset* asset : assets_->all) {
            if (!asset || asset->dead) continue;
            SDL_Point pos{asset->pos.x, asset->pos.y};
            if (new_area.contains_point(pos)) {
                queue_for_removal(asset);
            }
        }
    }

    for (Asset* asset : to_remove) {
        purge_asset(asset);
        if (assets_->player == asset) {
            assets_->player = nullptr;
        }
        if (player_ == asset) {
            player_ = nullptr;
        }
        auto& all = assets_->all;
        all.erase(std::remove(all.begin(), all.end(), asset), all.end());
        asset->Delete();
    }

    current_room_->room_area = std::make_unique<Area>(new_area);

    std::vector<nlohmann::json> planner_sources{room_json};
    std::vector<AssetSpawnPlanner::SourceContext> planner_contexts;
    AssetSpawnPlanner::SourceContext room_context;
    room_context.json_ref = &room_json;
    planner_contexts.push_back(room_context);
    if (room_json.value("inherits_map_assets", false) && map_info_json.contains("map_assets_data") &&
        map_info_json["map_assets_data"].is_object()) {
        planner_sources.push_back(map_info_json["map_assets_data"]);
        AssetSpawnPlanner::SourceContext map_context;
        planner_contexts.push_back(map_context);
    }
    current_room_->planner = std::make_unique<AssetSpawnPlanner>(planner_sources, *current_room_->room_area, assets_->library(), planner_contexts);

    auto occupancy = build_room_grid(std::string{});
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library = assets_->library().all();
    std::vector<std::unique_ptr<Asset>> spawned;
    std::vector<Area> exclusion;
    Check checker(false);
    std::mt19937 regen_rng(std::random_device{}());
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    SpawnContext ctx(regen_rng, checker, exclusion, asset_info_library, spawned, &assets_->library(), grid_service, occupancy.get());
    if (occupancy) {
        ctx.set_spawn_resolution(occupancy->resolution());
    }
    ExactSpawner exact;
    CenterSpawner center_spawn;
    RandomSpawner random;
    PerimeterSpawner perimeter;
    PercentSpawner percent;
    const Area* area_ptr = current_room_->room_area.get();
    const auto& queue = current_room_->planner->get_spawn_queue();
    for (const auto& info : queue) {
        const std::string& pos = info.position;
        if (pos == "Exact" || pos == "Exact Position") {
            exact.spawn(info, area_ptr, ctx);
        } else if (pos == "Center") {
            center_spawn.spawn(info, area_ptr, ctx);
        } else if (pos == "Perimeter") {
            perimeter.spawn(info, area_ptr, ctx);
        } else if (pos == "Percent") {
            percent.spawn(info, area_ptr, ctx);
        } else {
            random.spawn(info, area_ptr, ctx);
        }
    }
    integrate_spawned_assets(spawned);

    if (old_area_copy && new_area_size < old_area_size) {
        std::vector<std::pair<std::string, int>> edge_options;
        int edge_spacing = 100;
        if (map_info_json.contains("map_edge_data") && map_info_json["map_edge_data"].is_object()) {
            const auto& edge_json = map_info_json["map_edge_data"];
            if (edge_json.contains("batch_assets")) {
                const auto& batch = edge_json["batch_assets"];
                edge_spacing = (batch.value("grid_spacing_min", edge_spacing) + batch.value("grid_spacing_max", edge_spacing)) / 2;
                for (const auto& asset_entry : batch.value("batch_assets", std::vector<nlohmann::json>{})) {
                    if (asset_entry.contains("name") && asset_entry["name"].is_string()) {
                        int weight = asset_entry.value("percent", 1);
                        edge_options.emplace_back(asset_entry["name"].get<std::string>(), weight);
                    }
                }
            }
        }

        if (!edge_options.empty()) {
            const int edge_resolution = std::clamp(
                static_cast<int>(std::lround(std::log2(static_cast<double>(std::max(1, edge_spacing))))),
                0,
                vibble::grid::kMaxResolution);
            vibble::grid::Grid& grid_service = vibble::grid::global_grid();
            vibble::grid::Occupancy edge_grid(*old_area_copy, edge_resolution, grid_service);
            auto vertices = edge_grid.vertices_in_area(*old_area_copy);
            if (!vertices.empty()) {
                std::vector<int> weights;
                weights.reserve(edge_options.size());
                for (const auto& opt : edge_options) {
                    weights.push_back(std::max(1, opt.second));
                }
                std::discrete_distribution<int> pick(weights.begin(), weights.end());
                std::mt19937 edge_rng(std::random_device{}());
                std::vector<std::unique_ptr<Asset>> edge_spawned;
                for (auto* vertex : vertices) {
                    if (!vertex) continue;
                    if (current_room_->room_area->contains_point(vertex->world)) continue;
                    int idx = pick(edge_rng);
                    const std::string& asset_name = edge_options[idx].first;
                    auto info = assets_->library().get(asset_name);
                    if (!info) continue;
                    std::string spawn_id = generate_spawn_id();
                    Area spawn_area(asset_name, vertex->world, 1, 1, "Point", 1, 1, 1);
                    auto asset = std::make_unique<Asset>(info, spawn_area, vertex->world, 0, nullptr, spawn_id, std::string(asset_types::boundary));
                    edge_spawned.push_back(std::move(asset));
                }
                integrate_spawned_assets(edge_spawned);
            }
        }
    }

    Asset* existing_player = nullptr;
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead || !asset->info) {
            continue;
        }
        if (asset->info->type == asset_types::player) {
            existing_player = asset;
            break;
        }
    }

    if (existing_player) {
        assets_->player = existing_player;
        player_ = existing_player;
    } else if (!player_asset_name.empty() && current_room_->room_area) {
        auto is_clear = [&](SDL_Point point) {
            for (Asset* asset : assets_->all) {
                if (!asset || asset->dead) {
                    continue;
                }
                Area impassable = asset->get_area("impassable");
                if (!impassable.get_points().empty() && impassable.contains_point(point)) {
                    return false;
                }
            }
            return true;
        };

        auto bounds = current_room_->room_area->get_bounds();
        int minx = std::get<0>(bounds);
        int miny = std::get<1>(bounds);
        int maxx = std::get<2>(bounds);
        int maxy = std::get<3>(bounds);
        std::uniform_int_distribution<int> dist_x(minx, maxx);
        std::uniform_int_distribution<int> dist_y(miny, maxy);

        SDL_Point spawn_point = current_room_->room_area->get_center();
        bool found_spot = current_room_->room_area->contains_point(spawn_point) && is_clear(spawn_point);
        if (!found_spot) {
            for (int attempt = 0; attempt < 200 && !found_spot; ++attempt) {
                SDL_Point candidate{dist_x(regen_rng), dist_y(regen_rng)};
                if (!current_room_->room_area->contains_point(candidate)) {
                    continue;
                }
                if (is_clear(candidate)) {
                    spawn_point = candidate;
                    found_spot = true;
                }
            }
        }
        if (!found_spot) {
            int step = std::max(1, std::min(maxx - minx + 1, maxy - miny + 1) / 25);
            for (int y = miny; y <= maxy && !found_spot; y += step) {
                for (int x = minx; x <= maxx && !found_spot; x += step) {
                    SDL_Point candidate{x, y};
                    if (!current_room_->room_area->contains_point(candidate)) {
                        continue;
                    }
                    if (is_clear(candidate)) {
                        spawn_point = candidate;
                        found_spot = true;
                    }
                }
            }
        }
        if (found_spot) {
            if (Asset* spawned_player = assets_->spawn_asset(player_asset_name, spawn_point)) {
                spawned_player->set_owning_room_name(current_room_->room_name);
                assets_->player = spawned_player;
                player_ = spawned_player;
            }
        }
    }

    refresh_spawn_group_config_ui();
    reopen_room_configurator();
}

void RoomEditor::update_exact_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height) {
    const int dx = asset.pos.x - center.x;
    const int dy = asset.pos.y - center.y;
    entry["dx"] = dx;
    entry["dy"] = dy;
    if (width > 0) entry["origional_width"] = width;
    if (height > 0) entry["origional_height"] = height;
    if (entry.contains("exact_dx")) entry.erase("exact_dx");
    if (entry.contains("exact_dy")) entry.erase("exact_dy");
    if (entry.contains("exact_origin_width")) entry.erase("exact_origin_width");
    if (entry.contains("exact_origin_height")) entry.erase("exact_origin_height");
    if (entry.contains("ep_x_min")) entry.erase("ep_x_min");
    if (entry.contains("ep_x_max")) entry.erase("ep_x_max");
    if (entry.contains("ep_y_min")) entry.erase("ep_y_min");
    if (entry.contains("ep_y_max")) entry.erase("ep_y_max");
}

void RoomEditor::update_percent_json(nlohmann::json& entry, const Asset& asset, SDL_Point center, int width, int height) {
    if (width <= 0 || height <= 0) return;
    auto clamp_percent = [](int v) { return std::max(-100, std::min(100, v)); };
    double half_w = static_cast<double>(width) / 2.0;
    double half_h = static_cast<double>(height) / 2.0;
    if (half_w <= 0.0 || half_h <= 0.0) return;
    double dx = static_cast<double>(asset.pos.x - center.x);
    double dy = static_cast<double>(asset.pos.y - center.y);
    int percent_x = clamp_percent(static_cast<int>(std::lround((dx / half_w) * 100.0)));
    int percent_y = clamp_percent(static_cast<int>(std::lround((dy / half_h) * 100.0)));
    entry["p_x_min"] = percent_x;
    entry["p_x_max"] = percent_x;
    entry["p_y_min"] = percent_y;
    entry["p_y_max"] = percent_y;
    if (entry.contains("percent_x_min")) entry.erase("percent_x_min");
    if (entry.contains("percent_x_max")) entry.erase("percent_x_max");
    if (entry.contains("percent_y_min")) entry.erase("percent_y_min");
    if (entry.contains("percent_y_max")) entry.erase("percent_y_max");
}

void RoomEditor::save_perimeter_json(nlohmann::json& entry, int dx, int dy, int orig_w, int orig_h, int radius) {
    entry["dx"] = dx;
    entry["dy"] = dy;
    entry["origional_width"] = orig_w;
    entry["origional_height"] = orig_h;
    entry["radius"] = radius;
    static const std::array<const char*, 11> legacy_keys = {
        "percentage_shift_from_center",
        "percentage_shift_from_center_min",
        "percentage_shift_from_center_max",
        "border_shift_min",
        "border_shift_max",
        "perimeter_x_offset",
        "perimeter_x_offset_min",
        "perimeter_x_offset_max",
        "perimeter_y_offset",
        "perimeter_y_offset_min",
        "perimeter_y_offset_max"
};
    for (const char* key : legacy_keys) {
        if (entry.contains(key)) {
            entry.erase(key);
        }
    }
    for (auto it = entry.begin(); it != entry.end(); ) {
        if (it.key().rfind("sector_", 0) == 0) {
            it = entry.erase(it);
        } else {
            ++it;
        }
    }
}

void RoomEditor::save_edge_json(nlohmann::json& entry, int inset_percent) {
    int clamped = std::clamp(inset_percent, 0, 200);
    entry["edge_inset_percent"] = clamped;
}

double RoomEditor::edge_length_along_direction(const Area& area,
                                                   SDL_Point center,
                                                   SDL_FPoint direction) const {
    const auto& pts = area.get_points();
    const size_t count = pts.size();
    if (count < 2) {
        return 0.0;
    }
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < count; ++i) {
        const SDL_Point& a = pts[i];
        const SDL_Point& b = pts[(i + 1) % count];
        if (auto distance = ray_segment_distance(center, direction, a, b)) {
            if (*distance >= 0.0 && *distance < best) {
                best = *distance;
            }
        }
    }
    if (!std::isfinite(best) || best <= 0.0) {
        return 0.0;
    }
    return best;
}






