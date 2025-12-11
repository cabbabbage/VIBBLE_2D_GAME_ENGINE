#include "room_editor.hpp"

#include <algorithm>

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "asset/asset_utils.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/room_editor_map_info.hpp"
#include "dev_mode/asset_info_ui.hpp"
#include "dev_mode/dev_controls_persistence.hpp"
#include "map_layers_common.hpp"
#include "dev_mode/asset_library_ui.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/DockableCollapsible.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode_color_utils.hpp"
#include "spawn_group_config/SpawnGroupConfig.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "dev_mode/dev_footer_bar.hpp"
#include "room_config/room_configurator.hpp"
#include "dev_mode/FloatingDockableManager.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "dev_mode/widgets.hpp"
#include "dm_styles.hpp"
#include "room_overlay_renderer.hpp"
#include "animation_update/animation_update.hpp"
#include "render/warped_screen_grid.hpp"
#include "map_generation/room.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/asset_spawner.hpp"
#include "spawn/check.hpp"
#include "spawn/methods/center_spawner.hpp"
#include "spawn/methods/exact_spawner.hpp"
#include "spawn/methods/perimeter_spawner.hpp"
#include "spawn/methods/edge_spawner.hpp"
#include "spawn/methods/percent_spawner.hpp"
#include "spawn/methods/random_spawner.hpp"
#include "spawn/spawn_context.hpp"
#include "utils/input.hpp"
#include "utils/grid.hpp"
#include "utils/grid_occupancy.hpp"
#include "utils/map_grid_settings.hpp"
#include "utils/relative_room_position.hpp"

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
#include "utils/log.hpp"

using devmode::spawn::ensure_spawn_groups_array;
using devmode::spawn::find_spawn_groups_array;
using devmode::spawn::generate_spawn_id;

namespace {

constexpr int kClipboardNudge = 16;
constexpr float kCameraScaleEpsilon = 1e-4f;

int floor_div(int value, int divisor) {
    if (divisor == 0) {
        return 0;
    }
    if (value >= 0) {
        return value / divisor;
    }
    return (value - divisor + 1) / divisor;
}

int64_t make_cell_key(int cell_x, int cell_y) {
    return (static_cast<int64_t>(cell_x) << 32) ^ static_cast<uint32_t>(cell_y);
}

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

static bool is_visible_pixel_at(SDL_Renderer* renderer, SDL_Point screen_point) {
    if (!renderer) return true;

    Uint32 pixel = 0;
    SDL_Rect r{ screen_point.x, screen_point.y, 1, 1 };

    Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
#if SDL_MAJOR_VERSION >= 2
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer, &info) == 0 && info.num_texture_formats > 0) {

        fmt = info.texture_formats[0];
    }
#endif

    if (SDL_RenderReadPixels(renderer, &r, fmt, &pixel, sizeof(pixel)) != 0) {

        return true;
    }

    Uint8 a = 255;
    SDL_PixelFormat* pf = SDL_AllocFormat(fmt);
    if (pf) {
        Uint8 r8, g8, b8;
        SDL_GetRGBA(pixel, pf, &r8, &g8, &b8, &a);
        SDL_FreeFormat(pf);
    }
    return a > 0;
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
        vibble::log::debug(std::string{"[RoomEditor] "} + message);
    } catch (...) {}
}

}

RoomEditor::RoomEditor(Assets* owner, int screen_w, int screen_h)
    : assets_(owner), screen_w_(screen_w), screen_h_(screen_h) {
    update_room_config_bounds();
    rebuild_room_spawn_id_cache();
}

RoomEditor::~RoomEditor() {
    release_label_font();
    invalidate_all_room_labels();
    label_cache_.clear();
}

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

void RoomEditor::copy_selected_spawn_group() {
    auto spawn_id_opt = selected_spawn_group_id();
    if (!spawn_id_opt) {
        show_notice("Select a room spawn group to copy.");
        return;
    }

    const std::string& spawn_id = *spawn_id_opt;
    SpawnEntryResolution resolved = locate_spawn_entry(spawn_id);
    if (!resolved.valid() || resolved.source != SpawnEntryResolution::Source::Room || !resolved.entry) {
        show_notice("Map-wide spawn groups cannot be copied.");
        return;
    }

    if (spawn_group_is_boundary(spawn_id)) {
        show_notice("Boundary spawn groups cannot be copied.");
        return;
    }

    spawn_group_clipboard_.emplace();
    spawn_group_clipboard_->entry = *resolved.entry;
    spawn_group_clipboard_->entry.erase("priority");
    const std::string display_name = spawn_group_clipboard_->entry.value("display_name", std::string{"Spawn Group"});
    std::string base = strip_copy_suffix(display_name);
    if (base.empty()) {
        base = display_name;
    }
    if (base.empty()) {
        base = "Spawn Group";
    }
    spawn_group_clipboard_->base_display_name = std::move(base);
    spawn_group_clipboard_->paste_count = 0;
}

void RoomEditor::paste_spawn_group_from_clipboard() {
    if (!spawn_group_clipboard_) {
        show_notice("Clipboard is empty. Copy a spawn group first.");
        return;
    }

    Room* target_room = resolve_room_for_clipboard_action();
    if (!target_room || !target_room->room_area) {
        show_notice("No valid room available for paste.");
        return;
    }

    if (target_room != current_room_) {
        if (assets_) {
            assets_->set_editor_current_room(target_room);
        } else {
            set_current_room(target_room);
        }
    }

    if (!current_room_) {
        show_notice("Unable to determine active room for paste.");
        return;
    }

    auto& root = current_room_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);

    nlohmann::json entry = spawn_group_clipboard_->entry;
    const std::string new_id = generate_spawn_id();
    entry["spawn_id"] = new_id;
    const std::string next_name = next_clipboard_display_name();
    if (!next_name.empty()) {
        entry["display_name"] = next_name;
    }

    const std::string display_name = entry.value("display_name", std::string{"Spawn Group"});
    const int default_resolution = current_room_ ? current_room_->map_grid_settings().resolution : MapGridSettings::defaults().resolution;
    devmode::spawn::ensure_spawn_group_entry_defaults(entry, display_name, default_resolution);
    remap_clipboard_entry_to_room(entry, current_room_);

    groups.push_back(entry);
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].is_object()) {
            groups[i]["priority"] = static_cast<int>(i);
        }
    }

    sanitize_perimeter_spawn_groups(groups);
    save_current_room_assets_json();
    rebuild_room_spawn_id_cache();
    refresh_spawn_group_config_ui();
    reopen_room_configurator();

    nlohmann::json& inserted = groups.back();
    respawn_spawn_group(inserted);

    active_spawn_group_id_ = new_id;
    select_spawn_group_assets(new_id);
}

std::optional<std::string> RoomEditor::selected_spawn_group_id() const {
    if (selected_assets_.empty()) {
        return std::nullopt;
    }
    std::optional<std::string> result;
    for (Asset* asset : selected_assets_) {
        if (!asset) {
            continue;
        }
        if (!asset_belongs_to_room(asset)) {
            continue;
        }
        if (asset->spawn_id.empty()) {
            return std::nullopt;
        }
        if (!result) {
            result = asset->spawn_id;
        } else if (*result != asset->spawn_id) {
            return std::nullopt;
        }
    }
    if (!result || result->empty()) {
        return std::nullopt;
    }
    return result;
}

bool RoomEditor::spawn_group_is_boundary(const std::string& spawn_id) const {
    if (spawn_id.empty() || !assets_) {
        return false;
    }
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) {
            continue;
        }
        if (!asset_belongs_to_room(asset)) {
            continue;
        }
        if (asset->spawn_id == spawn_id) {
            if (asset->info && asset->info->type == asset_types::boundary) {
                return true;
            }
        }
    }
    return false;
}

Room* RoomEditor::resolve_room_for_clipboard_action() const {
    const Assets* assets = assets_;
    if (!assets) {
        return current_room_;
    }
    if (!input_) {
        return current_room_;
    }

    SDL_Point screen{input_->getX(), input_->getY()};
    SDL_Point world = screen;
    if (auto mapped_point = input_->screen_to_world(screen)) {
        world = *mapped_point;
    } else {
        SDL_FPoint mapped = assets->getView().screen_to_map(screen);
        world = SDL_Point{static_cast<int>(std::lround(mapped.x)), static_cast<int>(std::lround(mapped.y))};
    }

    if (current_room_ && current_room_->room_area && current_room_->room_area->contains_point(world)) {
        return current_room_;
    }

    for (Room* room : assets->rooms()) {
        if (!room || !room->room_area) {
            continue;
        }
        if (room->room_area->contains_point(world)) {
            return room;
        }
    }
    return current_room_;
}

void RoomEditor::select_spawn_group_assets(const std::string& spawn_id) {
    const std::vector<Asset*> previous_selection = selected_assets_;
    selected_assets_.clear();
    auto selection_changed = [&]() {
        if (previous_selection.size() != selected_assets_.size()) {
            return true;
        }
        return !std::equal(selected_assets_.begin(), selected_assets_.end(), previous_selection.begin());
};
    if (spawn_id.empty()) {
        sync_spawn_group_panel_with_selection();
        if (selection_changed()) {
            mark_highlight_dirty();
        }
        update_highlighted_assets();
        return;
    }

    if (spawn_group_locked(spawn_id)) {
        sync_spawn_group_panel_with_selection();
        if (selection_changed()) {
            mark_highlight_dirty();
        }
        update_highlighted_assets();
        return;
    }
    if (!assets_) {
        sync_spawn_group_panel_with_selection();
        if (selection_changed()) {
            mark_highlight_dirty();
        }
        update_highlighted_assets();
        return;
    }

    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) {
            continue;
        }
        if (!asset_belongs_to_room(asset)) {
            continue;
        }
        if (asset->spawn_id == spawn_id) {
            selected_assets_.push_back(asset);
        }
    }

    sync_spawn_group_panel_with_selection();
    if (selection_changed()) {
        mark_highlight_dirty();
    }
    update_highlighted_assets();
}

void RoomEditor::remap_clipboard_entry_to_room(nlohmann::json& entry, Room* room) {
    if (!room || !room->room_area) {
        return;
    }

    auto bounds = room->room_area->get_bounds();
    const int width = std::max(1, std::get<2>(bounds) - std::get<0>(bounds));
    const int height = std::max(1, std::get<3>(bounds) - std::get<1>(bounds));

    std::string method = entry.value("position", std::string{});
    if (method == "Exact Position") {
        method = "Exact";
    }

    if (method == "Exact" || method == "Perimeter") {
        int stored_dx = entry.value("dx", 0);
        int stored_dy = entry.value("dy", 0);
        int orig_w = std::max(1, entry.value("origional_width", width));
        int orig_h = std::max(1, entry.value("origional_height", height));
        RelativeRoomPosition relative(SDL_Point{stored_dx, stored_dy}, orig_w, orig_h);
        SDL_Point scaled = relative.scaled_offset(width, height);
        entry["dx"] = scaled.x;
        entry["dy"] = scaled.y;
        entry["origional_width"] = width;
        entry["origional_height"] = height;
        ensure_clipboard_position_is_valid(entry, room);
    } else if (method == "Percent") {
        entry["origional_width"] = width;
        entry["origional_height"] = height;
    }
}

void RoomEditor::ensure_clipboard_position_is_valid(nlohmann::json& entry, Room* room) {
    if (!room || !room->room_area) {
        return;
    }

    std::string method = entry.value("position", std::string{});
    if (method == "Exact Position") {
        method = "Exact";
    }
    if (method != "Exact" && method != "Perimeter") {
        return;
    }

    SDL_Point center = room->room_area->get_center();
    int dx = entry.value("dx", 0);
    int dy = entry.value("dy", 0);
    SDL_Point candidate{center.x + dx, center.y + dy};
    if (room->room_area->contains_point(candidate)) {
        return;
    }

    const std::array<SDL_Point, 8> adjustments{{
        SDL_Point{kClipboardNudge, 0},
        SDL_Point{-kClipboardNudge, 0},
        SDL_Point{0, kClipboardNudge},
        SDL_Point{0, -kClipboardNudge},
        SDL_Point{kClipboardNudge, kClipboardNudge},
        SDL_Point{kClipboardNudge, -kClipboardNudge},
        SDL_Point{-kClipboardNudge, kClipboardNudge},
        SDL_Point{-kClipboardNudge, -kClipboardNudge},
    }};

    for (const SDL_Point& delta : adjustments) {
        SDL_Point test{candidate.x + delta.x, candidate.y + delta.y};
        if (room->room_area->contains_point(test)) {
            entry["dx"] = test.x - center.x;
            entry["dy"] = test.y - center.y;
            return;
        }
    }

    entry["dx"] = 0;
    entry["dy"] = 0;
}

std::string RoomEditor::strip_copy_suffix(const std::string& name) {
    if (name.empty()) {
        return name;
    }
    const std::string marker = " (Copy";
    const size_t pos = name.rfind(marker);
    if (pos == std::string::npos) {
        return name;
    }
    if (name.back() != ')') {
        return name;
    }
    const std::string inside = name.substr(pos + 2, name.size() - pos - 3);
    if (inside == "Copy") {
        return name.substr(0, pos);
    }
    const std::string prefix = "Copy ";
    if (inside.rfind(prefix, 0) == 0) {
        const bool digits = std::all_of(inside.begin() + prefix.size(), inside.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
        if (digits) {
            return name.substr(0, pos);
        }
    }
    return name;
}

std::string RoomEditor::next_clipboard_display_name() {
    if (!spawn_group_clipboard_) {
        return {};
    }
    ++spawn_group_clipboard_->paste_count;
    std::string base = spawn_group_clipboard_->base_display_name;
    if (base.empty()) {
        base = "Spawn Group";
    }
    if (spawn_group_clipboard_->paste_count == 1) {
        return base + " (Copy)";
    }
    return base + " (Copy " + std::to_string(spawn_group_clipboard_->paste_count) + ")";
}

void RoomEditor::show_notice(const std::string& message) const {
    if (!assets_) {
        return;
    }
    assets_->show_dev_notice(message);
}

void RoomEditor::mark_highlight_dirty() {
    highlight_dirty_ = true;
}

void RoomEditor::set_input(Input* input) {
    input_ = input;
}

void RoomEditor::set_player(Asset* player) {
    player_ = player;
    mark_spatial_index_dirty();
}

void RoomEditor::set_active_assets(std::vector<Asset*>& actives, std::uint64_t generation) {
    const bool pointer_changed = active_assets_ != &actives;
    active_assets_ = &actives;
    if (pointer_changed || active_assets_version_ != generation) {
        active_assets_version_ = generation;
        mark_highlight_dirty();
        mark_spatial_index_dirty();
    }
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

        spawn_group_panel_->set_work_area(FloatingPanelLayoutManager::instance().usableRect());
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

        header_visibility_callback_(false);
    }
    if (room_cfg_ui_) {
        room_cfg_ui_->set_header_visibility_controller([this](bool visible) {
            room_config_panel_visible_ = visible;
            if (header_visibility_callback_) {

                header_visibility_callback_(visible);
            }
        });
    }
    if (info_ui_) {
        info_ui_->set_header_visibility_callback([this](bool visible) {
            asset_info_panel_visible_ = visible;
            if (header_visibility_callback_) {

                header_visibility_callback_(visible);
            }
        });
    }
}

void RoomEditor::set_map_assets_panel_callback(std::function<void()> cb) {
    open_map_assets_panel_callback_ = std::move(cb);
}

void RoomEditor::set_boundary_assets_panel_callback(std::function<void()> cb) {
    open_boundary_assets_panel_callback_ = std::move(cb);
}

void RoomEditor::set_current_room(Room* room) {
    room_editor_trace("[RoomEditor] set_current_room begin");
    if (room) {
        room_editor_trace(std::string("[RoomEditor] target room -> ") + room->room_name);
    } else {
        room_editor_trace("[RoomEditor] target room -> <null>");
    }

    Room* previous_room = current_room_;
    const bool room_changed = (room != current_room_);

    if (room != current_room_) {
        room_editor_trace("[RoomEditor] clearing active spawn group target");
        clear_active_spawn_group_target();
    }

    current_room_ = room;
    if (room_changed) {
        invalidate_label_cache(previous_room);
        invalidate_label_cache(current_room_);
    }
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
    mark_spatial_index_dirty();

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
        mouse_controls_enabled_last_frame_ = false;
        blocking_panel_visible_.fill(false);
    }

    WarpedScreenGrid* cam = assets_ ? &assets_->getView() : nullptr;
    if (enabled_) {
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
        if (cam && !preserve_camera_state) {
            cam->set_manual_zoom_override(false);
            cam->clear_focus_override();
        }
        if (library_ui_) library_ui_->close();
        if (info_ui_) info_ui_->close();
        if (spawn_group_panel_) spawn_group_panel_->set_visible(false);
        clear_active_spawn_group_target();
        clear_selection();
        reset_click_state();
        set_room_config_visible(false);
        refresh_room_config_visibility();
    }

    if (input_) input_->clearClickBuffer();
}

void RoomEditor::update(const Input& input) {
    handle_shortcuts(input);

    auto enforce_mouse_controls_disabled = [this]() {
        const bool panel_visible   = spawn_group_panel_ && spawn_group_panel_->is_visible();
        const bool has_spawn_target = active_spawn_group_id_.has_value();
        const bool has_selection   = !selected_assets_.empty();
        const bool has_highlight   = !highlighted_assets_.empty();
        const bool has_hover       = hovered_asset_ != nullptr;

        if (!panel_visible && !has_spawn_target && !has_selection && !has_highlight && !has_hover) {
            return;
        }

        if (has_spawn_target) {
            clear_active_spawn_group_target();
        }

        if (has_selection || has_highlight || has_hover) {
            clear_selection();
            clear_highlighted_assets();
        }
};

    if (!enabled_) {
        if (mouse_controls_enabled_last_frame_) {
            enforce_mouse_controls_disabled();
        }
        mouse_controls_enabled_last_frame_ = false;
        return;
    }

    handle_delete_shortcut(input);

    const int mx = input.getX();
    const int my = input.getY();
    const bool ui_blocked = is_ui_blocking_input(mx, my);

    if (!should_enable_mouse_controls()) {
        enforce_mouse_controls_disabled();
        if (assets_) {
            pan_zoom_.cancel(assets_->getView());
        }
        mouse_controls_enabled_last_frame_ = false;
        return;
    }

    mouse_controls_enabled_last_frame_ = true;

    if (!ui_blocked || dragging_) {
        handle_mouse_input(input);
    } else if (assets_) {
        pan_zoom_.cancel(assets_->getView());
    }

    update_highlighted_assets();
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

    update_room_config_bounds();

    if (library_ui_ && library_ui_->is_visible()) {
        if (manifest_store_) {
            library_ui_->update(input, screen_w_, screen_h_, assets_->library(), *assets_, *manifest_store_);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RoomEditor] Manifest store unavailable; asset library UI update skipped.");
        }
    }

    if (library_ui_) {
if (auto selected = library_ui_->consume_selection()) {
    last_selected_from_library_ = selected;
    const bool had_pending_spawn = pending_spawn_world_pos_.has_value();
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
                    if (hovered_asset_ != spawned) {
                        hovered_asset_ = spawned;
                    }
                    mark_highlight_dirty();
                    update_highlighted_assets();
                    sync_spawn_group_panel_with_selection();
                    spawned_asset = true;
                }
            }
        }
    }
    if (!spawned_asset && !had_pending_spawn) {
        pending_spawn_world_pos_.reset();
        open_asset_info_editor(selected);
    }
        }

        if (auto area_sel = library_ui_->consume_area_selection()) {
            const bool had_pending_spawn = pending_spawn_world_pos_.has_value();
            if (pending_spawn_world_pos_ && current_room_ && assets_) {
                SDL_Point world = *pending_spawn_world_pos_;
                pending_spawn_world_pos_.reset();

                Room* src_room = nullptr;
                for (Room* r : assets_->rooms()) {
                    if (r && r->room_name == area_sel->room_name) { src_room = r; break; }
                }
                if (src_room) {

                    nlohmann::json* src_entry = nullptr;
                    nlohmann::json& src_root = src_room->assets_data();
                    if (src_root.is_object() && src_root.contains("areas") && src_root["areas"].is_array()) {
                        for (auto& entry : src_root["areas"]) {
                            if (entry.is_object() && entry.value("name", std::string{}) == area_sel->area_name) {
                                src_entry = &entry; break;
                            }
                        }
                    }
                    if (src_entry) {

                        nlohmann::json copy = *src_entry;

                        std::string base = copy.value("name", area_sel->area_name);
                        if (base.empty()) base = "area";
                        std::string candidate = base;
                        int suffix = 1;
                        auto name_conflict = [&](const std::string& name){
                            for (const auto& na : current_room_->areas) {
                                if (na.name == name) return true;
                            }
                            return false;
};
                        while (name_conflict(candidate)) {
                            candidate = base + "_" + std::to_string(suffix++);
                        }
                        copy["name"] = candidate;

                        auto dims_of = [&](Room* room){
                            int w=0,h=0; if (room && room->room_area) {
                                auto b = room->room_area->get_bounds();
                                w = std::max(1, std::get<2>(b) - std::get<0>(b));
                                h = std::max(1, std::get<3>(b) - std::get<1>(b));
                            }
                            return std::pair<int,int>(w,h);
};
                        auto [src_w, src_h] = dims_of(src_room);
                        if (!copy.contains("origional_width") && src_w > 0) copy["origional_width"] = src_w;
                        if (!copy.contains("origional_height") && src_h > 0) copy["origional_height"] = src_h;

                        SDL_Point center{0,0};
                        if (current_room_->room_area) { auto c = current_room_->room_area->get_center(); center.x=c.x; center.y=c.y; }
                        copy["anchor_relative_to_center"] = true;
                        copy["anchor"] = nlohmann::json::object({ {"x", world.x - center.x}, {"y", world.y - center.y} });

                        nlohmann::json& dst_root = current_room_->assets_data();
                        if (!dst_root.contains("areas") || !dst_root["areas"].is_array()) {
                            dst_root["areas"] = nlohmann::json::array();
                        }
                        dst_root["areas"].push_back(copy);
                        current_room_->save_assets_json();

                        ensure_area_anchor_spawn_entry(current_room_, candidate);
                    }
                }
            } else if (!had_pending_spawn) {

                pending_spawn_world_pos_.reset();
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

    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->update(input, screen_w_, screen_h_);
    } else if (active_modal_ == ActiveModal::AssetInfo) {
        active_modal_ = ActiveModal::None;
    }

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
                if (!pointer_based || result.pointer_blocked) {
                    input_->consumeEvent(event);
                }
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

    if (auto* dropdown = DMDropdown::active_dropdown()) {
        if (dropdown->handle_event(event)) {
            if (pointer_event && input_) {
                input_->clearClickBuffer();
            }
            return true;
        }
    }

    if (pointer_based && pointer_blocked) {
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

    return false;
}

bool RoomEditor::is_shift_key_down() const {
    if (!input_) {
        return false;
    }
    return input_->isScancodeDown(SDL_SCANCODE_LSHIFT) || input_->isScancodeDown(SDL_SCANCODE_RSHIFT);
}

void RoomEditor::invalidate_label_cache(Room* room) {
    if (!room) {
        return;
    }
    auto it = label_cache_.find(room);
    if (it == label_cache_.end()) {
        return;
    }
    if (it->second.texture) {
        SDL_DestroyTexture(it->second.texture);
        it->second.texture = nullptr;
    }
    it->second.text_size = SDL_Point{0, 0};
    it->second.last_name.clear();
    it->second.last_color = SDL_Color{0, 0, 0, 0};
    it->second.dirty = true;
}

void RoomEditor::invalidate_all_room_labels() {
    for (auto& [room, entry] : label_cache_) {
        (void)room;
        if (entry.texture) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
        }
        entry.text_size = SDL_Point{0, 0};
        entry.last_name.clear();
        entry.last_color = SDL_Color{0, 0, 0, 0};
        entry.dirty = true;
    }
}

void RoomEditor::prune_label_cache(const std::vector<Room*>& rooms) {
    std::unordered_set<Room*> active(rooms.begin(), rooms.end());
    for (auto it = label_cache_.begin(); it != label_cache_.end();) {
        if (active.find(it->first) == active.end()) {
            if (it->second.texture) {
                SDL_DestroyTexture(it->second.texture);
            }
            it = label_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void RoomEditor::render_room_labels(SDL_Renderer* renderer) {
    if (!enabled_) return;
    if (!renderer || !assets_) return;

    ensure_label_font();
    if (!label_font_) return;

    const std::vector<Room*>& rooms = assets_->rooms();
    if (rooms.empty()) return;

    prune_label_cache(rooms);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    label_rects_.clear();

    struct LabelInfo {
        Room* room = nullptr;
        SDL_FPoint desired_center{0.0f, 0.0f};
        float priority = 0.0f;
};

    std::vector<LabelInfo> render_queue;
    render_queue.reserve(rooms.size());

    SDL_FPoint screen_center{static_cast<float>(screen_w_) * 0.5f,
                             static_cast<float>(screen_h_) * 0.5f};

    WarpedScreenGrid& view = assets_->getView();

    for (Room* room : rooms) {
        if (!room || !room->room_area) continue;

        SDL_Point center = room->room_area->get_center();
        SDL_FPoint screen_pt = view.map_to_screen(center);
        SDL_FPoint desired_center{screen_pt.x,
                                  screen_pt.y - kLabelVerticalOffset};

        float dx = desired_center.x - screen_center.x;
        float dy = desired_center.y - screen_center.y;
        float dist2 = dx * dx + dy * dy;

        render_queue.push_back(LabelInfo{room, desired_center, dist2});
    }

    std::sort(render_queue.begin(), render_queue.end(), [](const LabelInfo& a, const LabelInfo& b) {
        if (a.priority == b.priority) {
            return a.room < b.room;
        }
        return a.priority < b.priority;
    });

    for (const auto& info : render_queue) {
        if (!info.room) continue;
        render_room_label(renderer, info.room, info.desired_center);
    }
}

void RoomEditor::render_room_label(SDL_Renderer* renderer, Room* room, SDL_FPoint desired_center) {
    if (!room || !room->room_area || !assets_) return;
    if (!label_font_) return;

    const std::string name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
    SDL_Color base_color = room->display_color();

    auto& cache = label_cache_[room];
    if (cache.last_name != name || !colors_equal(cache.last_color, base_color)) {
        cache.dirty = true;
    }

    if (cache.dirty) {
        SDL_Color text_color = display_color_luminance(base_color) > 0.55f
                                   ? SDL_Color{20, 20, 20, 255}
                                   : kLabelText;

        SDL_Surface* text_surface = TTF_RenderUTF8_Blended(label_font_, name.c_str(), text_color);
        if (!text_surface) {
            return;
        }

        SDL_Texture* new_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
        if (!new_texture) {
            SDL_FreeSurface(text_surface);
            return;
        }

        if (cache.texture) {
            SDL_DestroyTexture(cache.texture);
        }
        cache.texture = new_texture;
        cache.text_size = SDL_Point{text_surface->w, text_surface->h};
        cache.last_name = name;
        cache.last_color = base_color;
        cache.dirty = false;

        SDL_FreeSurface(text_surface);
    }

    if (!cache.texture || cache.text_size.x <= 0 || cache.text_size.y <= 0) {
        return;
    }

    SDL_Rect bg_rect = label_background_rect(cache.text_size.x, cache.text_size.y, desired_center);
    bg_rect = resolve_edge_overlap(bg_rect, desired_center);

    label_rects_.push_back(bg_rect);

    SDL_Color bg_color = with_alpha(lighten(base_color, 0.08f), 205);
    SDL_Color border_color = with_alpha(darken(base_color, 0.3f), 235);

    const int radius = std::min(DMStyles::CornerRadius(), std::min(bg_rect.w, bg_rect.h) / 2);
    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(bg_rect.w, bg_rect.h) / 2));
    dm_draw::DrawBeveledRect( renderer, bg_rect, radius, bevel, bg_color, bg_color, bg_color, false, 0.0f, 0.0f);
    dm_draw::DrawRoundedOutline( renderer, bg_rect, radius, 1, border_color);

    SDL_Rect dst{bg_rect.x + kLabelPadding, bg_rect.y + kLabelPadding, cache.text_size.x, cache.text_size.y};
    SDL_RenderCopy(renderer, cache.texture, nullptr, &dst);
}

SDL_Rect RoomEditor::label_background_rect(int text_w, int text_h, SDL_FPoint desired_center) const {
    int rect_w = text_w + kLabelPadding * 2;
    int rect_h = text_h + kLabelPadding * 2;

    SDL_Rect rect{};
    rect.w = rect_w;
    rect.h = rect_h;

    if (screen_w_ <= 0 || screen_h_ <= 0) {
        rect.x = static_cast<int>(std::lround(desired_center.x - static_cast<float>(rect_w) * 0.5f));
        rect.y = static_cast<int>(std::lround(desired_center.y - static_cast<float>(rect_h) * 0.5f));
        return rect;
    }

    const float half_w = static_cast<float>(rect_w) * 0.5f;
    const float half_h = static_cast<float>(rect_h) * 0.5f;
    const float min_x = half_w;
    const float max_x = static_cast<float>(screen_w_) - half_w;
    const float min_y = half_h;
    const float max_y = static_cast<float>(screen_h_) - half_h;

    auto clamp_center = [&](const SDL_FPoint& point) {
        SDL_FPoint clamped = point;
        clamped.x = std::clamp(clamped.x, min_x, max_x);
        clamped.y = std::clamp(clamped.y, min_y, max_y);
        return clamped;
};

    SDL_FPoint center = clamp_center(desired_center);

    const bool inside = desired_center.x >= min_x && desired_center.x <= max_x &&
                        desired_center.y >= min_y && desired_center.y <= max_y;

    if (!inside) {
        SDL_FPoint screen_center{static_cast<float>(screen_w_) * 0.5f,
                                 static_cast<float>(screen_h_) * 0.5f};
        const float dx = desired_center.x - screen_center.x;
        const float dy = desired_center.y - screen_center.y;
        const float epsilon = 0.0001f;

        if (std::fabs(dx) > epsilon || std::fabs(dy) > epsilon) {
            float t_min = 1.0f;

            auto update_t = [&](float boundary, float origin, float delta) {
                if (std::fabs(delta) < epsilon) return;
                float t = (boundary - origin) / delta;
                if (t >= 0.0f) {
                    t_min = std::min(t_min, t);
                }
};

            if (dx > 0.0f) update_t(max_x, screen_center.x, dx);
            else if (dx < 0.0f) update_t(min_x, screen_center.x, dx);

            if (dy > 0.0f) update_t(max_y, screen_center.y, dy);
            else if (dy < 0.0f) update_t(min_y, screen_center.y, dy);

            center.x = screen_center.x + dx * t_min;
            center.y = screen_center.y + dy * t_min;
            center = clamp_center(center);
        }
    }

    rect.x = static_cast<int>(std::lround(center.x - half_w));
    rect.y = static_cast<int>(std::lround(center.y - half_h));
    return rect;
}

SDL_Rect RoomEditor::resolve_edge_overlap(SDL_Rect rect, SDL_FPoint desired_center) {
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return rect;
    }

    const int tolerance = 1;
    const bool touches_left = rect.x <= tolerance;
    const bool touches_right = rect.x + rect.w >= screen_w_ - tolerance;
    const bool touches_top = rect.y <= tolerance;
    const bool touches_bottom = rect.y + rect.h >= screen_h_ - tolerance;

    if (touches_top || touches_bottom) {
        rect = resolve_horizontal_edge_overlap(rect, desired_center.x, touches_top);
    }

    if (touches_left || touches_right) {
        rect = resolve_vertical_edge_overlap(rect, desired_center.y, touches_left);
    }

    return rect;
}

SDL_Rect RoomEditor::resolve_horizontal_edge_overlap(SDL_Rect rect, float desired_center_x, bool top_edge) {
    if (screen_w_ <= 0) return rect;

    const int min_x = 0;
    const int max_x = std::max(0, screen_w_ - rect.w);
    if (max_x <= min_x) {
        rect.x = min_x;
        return rect;
    }

    std::vector<SDL_Rect> same_edge_rects;
    same_edge_rects.reserve(label_rects_.size());
    const int tolerance = 1;

    for (const SDL_Rect& other : label_rects_) {
        bool other_on_edge = top_edge ? other.y <= tolerance
                                      : other.y + other.h >= screen_h_ - tolerance;
        if (other_on_edge) {
            same_edge_rects.push_back(other);
        }
    }

    if (same_edge_rects.empty()) {
        rect.x = std::clamp(static_cast<int>(std::lround(desired_center_x - rect.w * 0.5f)), min_x, max_x);
        return rect;
    }

    std::vector<int> to_process;
    to_process.reserve(same_edge_rects.size() * 2 + 3);

    int target_x = std::clamp(static_cast<int>(std::lround(desired_center_x - rect.w * 0.5f)), min_x, max_x);
    to_process.push_back(target_x);
    to_process.push_back(min_x);
    to_process.push_back(max_x);

    std::vector<int> visited;
    visited.reserve(to_process.size());

    float best_penalty = std::numeric_limits<float>::max();
    int best_x = target_x;
    bool found_position = false;

    while (!to_process.empty()) {
        int candidate_x = to_process.back();
        to_process.pop_back();

        if (std::find(visited.begin(), visited.end(), candidate_x) != visited.end()) {
            continue;
        }
        visited.push_back(candidate_x);

        SDL_Rect candidate = rect;
        candidate.x = candidate_x;

        std::vector<SDL_Rect> overlapping;
        for (const SDL_Rect& other : same_edge_rects) {
            if (rects_overlap(candidate, other)) {
                overlapping.push_back(other);
            }
        }

        if (overlapping.empty()) {
            float center_x = static_cast<float>(candidate.x) + static_cast<float>(candidate.w) * 0.5f;
            float penalty = std::fabs(center_x - desired_center_x);
            if (penalty < best_penalty - 0.01f || (!found_position && penalty <= best_penalty + 0.01f)) {
                best_penalty = penalty;
                best_x = candidate_x;
                found_position = true;
                if (penalty <= 0.01f) {
                    break;
                }
            }
            continue;
        }

        for (const SDL_Rect& other : overlapping) {
            int left = std::clamp(other.x - rect.w, min_x, max_x);
            int right = std::clamp(other.x + other.w, min_x, max_x);

            if (std::find(visited.begin(), visited.end(), left) == visited.end()) {
                to_process.push_back(left);
            }
            if (std::find(visited.begin(), visited.end(), right) == visited.end()) {
                to_process.push_back(right);
            }
        }
    }

    rect.x = found_position ? best_x : target_x;
    return rect;
}

SDL_Rect RoomEditor::resolve_vertical_edge_overlap(SDL_Rect rect, float desired_center_y, bool left_edge) {
    if (screen_h_ <= 0) return rect;

    const int min_y = 0;
    const int max_y = std::max(0, screen_h_ - rect.h);
    if (max_y <= min_y) {
        rect.y = min_y;
        return rect;
    }

    std::vector<SDL_Rect> same_edge_rects;
    same_edge_rects.reserve(label_rects_.size());
    const int tolerance = 1;

    for (const SDL_Rect& other : label_rects_) {
        bool other_on_edge = left_edge ? other.x <= tolerance
                                       : other.x + other.w >= screen_w_ - tolerance;
        if (other_on_edge) {
            same_edge_rects.push_back(other);
        }
    }

    if (same_edge_rects.empty()) {
        rect.y = std::clamp(static_cast<int>(std::lround(desired_center_y - rect.h * 0.5f)), min_y, max_y);
        return rect;
    }

    std::vector<int> to_process;
    to_process.reserve(same_edge_rects.size() * 2 + 3);

    int target_y = std::clamp(static_cast<int>(std::lround(desired_center_y - rect.h * 0.5f)), min_y, max_y);
    to_process.push_back(target_y);
    to_process.push_back(min_y);
    to_process.push_back(max_y);

    std::vector<int> visited;
    visited.reserve(to_process.size());

    float best_penalty = std::numeric_limits<float>::max();
    int best_y = target_y;
    bool found_position = false;

    while (!to_process.empty()) {
        int candidate_y = to_process.back();
        to_process.pop_back();

        if (std::find(visited.begin(), visited.end(), candidate_y) != visited.end()) {
            continue;
        }
        visited.push_back(candidate_y);

        SDL_Rect candidate = rect;
        candidate.y = candidate_y;

        std::vector<SDL_Rect> overlapping;
        for (const SDL_Rect& other : same_edge_rects) {
            if (rects_overlap(candidate, other)) {
                overlapping.push_back(other);
            }
        }

        if (overlapping.empty()) {
            float center_y = static_cast<float>(candidate.y) + static_cast<float>(candidate.h) * 0.5f;
            float penalty = std::fabs(center_y - desired_center_y);
            if (penalty < best_penalty - 0.01f || (!found_position && penalty <= best_penalty + 0.01f)) {
                best_penalty = penalty;
                best_y = candidate_y;
                found_position = true;
                if (penalty <= 0.01f) {
                    break;
                }
            }
            continue;
        }

        for (const SDL_Rect& other : overlapping) {
            int up = std::clamp(other.y - rect.h, min_y, max_y);
            int down = std::clamp(other.y + other.h, min_y, max_y);

            if (std::find(visited.begin(), visited.end(), up) == visited.end()) {
                to_process.push_back(up);
            }
            if (std::find(visited.begin(), visited.end(), down) == visited.end()) {
                to_process.push_back(down);
            }
        }
    }

    rect.y = found_position ? best_y : target_y;
    return rect;
}

bool RoomEditor::rects_overlap(const SDL_Rect& a, const SDL_Rect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

void RoomEditor::ensure_label_font() {
    if (label_font_) return;
    label_font_ = TTF_OpenFont(dm::FONT_PATH, 18);
}

void RoomEditor::release_label_font() {
    if (label_font_) {
        TTF_CloseFont(label_font_);
        label_font_ = nullptr;
    }
}

void RoomEditor::render_overlays(SDL_Renderer* renderer) {
    if (!assets_) {
        return;
    }
    const WarpedScreenGrid& cam = assets_->getView();

    if (renderer) {
        if (current_room_ && current_room_->room_area) {
            const auto style = dm_draw::ResolveRoomBoundsOverlayStyle(current_room_->display_color());
            dm_draw::RenderRoomBoundsOverlay( renderer, assets_->getView(), *current_room_->room_area, style);
        }
        render_room_labels(renderer);
    }

    if (renderer && enabled_) {

        int mx = input_ ? input_->getX() : 0;
        int my = input_ ? input_->getY() : 0;
        if (!is_ui_blocking_input(mx, my)) {
            SDL_FPoint screen_f = cam.map_to_screen(snapped_cursor_world_);
            SDL_Point screen{ static_cast<int>(std::lround(screen_f.x)), static_cast<int>(std::lround(screen_f.y)) };

            if (assets_) {

            }
            SDL_Color color = DMStyles::HighlightColor();
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 220);
            const int cross = 8;
            SDL_RenderDrawLine(renderer, screen.x - cross, screen.y, screen.x + cross, screen.y);
            SDL_RenderDrawLine(renderer, screen.x, screen.y - cross, screen.x, screen.y + cross);
        }

        if (is_shift_key_down()) {
            auto fetch_bounds = [&](Asset* asset, SDL_Rect& out_rect) -> bool {
                if (!asset) return false;

                auto it = asset_bounds_cache_.find(asset);
                if (it != asset_bounds_cache_.end()) {
                    out_rect = it->second.bounds;
                    return true;
                }

                const float scale = std::max(kCameraScaleEpsilon, cam.get_scale());
                const float inv_scale = 1.0f / scale;
                const float ref_h = compute_reference_screen_height(cam, inv_scale);
                int screen_y = 0;
                return compute_asset_screen_bounds(cam, ref_h, inv_scale, asset, out_rect, screen_y);
};

            ensure_spatial_index(cam);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const int outline_thickness = 2;
            for (Asset* asset : highlighted_assets_) {
                if (!asset_belongs_to_room(asset)) continue;
                SDL_Rect bounds{};
                if (!fetch_bounds(asset, bounds)) {
                    continue;
                }
                const bool is_selected = std::find(selected_assets_.begin(), selected_assets_.end(), asset) != selected_assets_.end();
                SDL_Color color = is_selected ? DMStyles::AccentButton().hover_bg : DMStyles::HighlightColor();
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 210);
                for (int i = 0; i < outline_thickness; ++i) {
                    SDL_Rect r{
                        bounds.x - i,
                        bounds.y - i,
                        bounds.w + i * 2,
                        bounds.h + i * 2
};
                    SDL_RenderDrawRect(renderer, &r);
                }
            }
        }
    }

    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->render(renderer, screen_w_, screen_h_);
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
            const double scale = std::max(0.0001, static_cast<double>(cam.get_scale()));
            const double inv_scale = 1.0 / scale;
            SDL_FPoint screen_center_f = cam.map_to_screen(overlay->center);
            SDL_Point screen_center{static_cast<int>(std::lround(screen_center_f.x)),
                                    static_cast<int>(std::lround(screen_center_f.y))};
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

        auto draw_dashed_polyline_world = [&](const std::vector<SDL_Point>& path, SDL_Color color) {
            if (path.size() < 2) return;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 210);
            const int dash = 8;
            const int gap  = 6;
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                SDL_Point a = path[i];
                SDL_Point b = path[i + 1];
                const double dx = static_cast<double>(b.x - a.x);
                const double dy = static_cast<double>(b.y - a.y);
                const double len = std::hypot(dx, dy);
                if (len <= 1e-6) continue;
                const int total = static_cast<int>(std::lround(len));
                double ux = dx / len;
                double uy = dy / len;
                int cursor = 0;
                bool draw = true;
                while (cursor < total) {
                    int seg = draw ? dash : gap;
                    int end = std::min(total, cursor + seg);
                    if (draw) {

                        SDL_FPoint s_world{ static_cast<float>(a.x + ux * cursor), static_cast<float>(a.y + uy * cursor) };
                        SDL_FPoint e_world{ static_cast<float>(a.x + ux * end),    static_cast<float>(a.y + uy * end) };
                        SDL_FPoint s_screen_f = cam.map_to_screen_f(s_world);
                        SDL_FPoint e_screen_f = cam.map_to_screen_f(e_world);
                        SDL_Point s{ static_cast<int>(std::lround(s_screen_f.x)), static_cast<int>(std::lround(s_screen_f.y)) };
                        SDL_Point e{ static_cast<int>(std::lround(e_screen_f.x)), static_cast<int>(std::lround(e_screen_f.y)) };
                        SDL_RenderDrawLine(renderer, s.x, s.y, e.x, e.y);
                    }
                    cursor = end;
                    draw = !draw;
                }
            }
};

        auto edge_path = compute_edge_path_for_drag();
        if (!edge_path) {
            std::string edge_spawn_id;
            if (hovered_asset_ && hovered_asset_->spawn_method == "Edge" && !hovered_asset_->spawn_id.empty()) {
                edge_spawn_id = hovered_asset_->spawn_id;
            } else {
                for (Asset* asset : selected_assets_) {
                    if (!asset) continue;
                    if (asset->spawn_method == "Edge" && !asset->spawn_id.empty()) {
                        edge_spawn_id = asset->spawn_id;
                        break;
                    }
                }
            }
            if (!edge_spawn_id.empty()) {
                edge_path = compute_edge_path_for_spawn(edge_spawn_id);
            }
        }
        if (edge_path && !edge_path->empty()) {
            SDL_Color color = DMStyles::AccentButton().hover_bg;
            draw_dashed_polyline_world(*edge_path, color);
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
    set_blocking_panel_visible(BlockingPanel::AssetLibrary, library_ui_ && library_ui_->is_visible());
}

void RoomEditor::open_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    if (active_modal_ == ActiveModal::AssetInfo && (!library_ui_ || !library_ui_->is_visible())) {
        pulse_active_modal_header();
        return;
    }
    library_ui_->open();
    set_blocking_panel_visible(BlockingPanel::AssetLibrary, library_ui_ && library_ui_->is_visible());
}

void RoomEditor::close_asset_library() {
    if (library_ui_) library_ui_->close();
    set_blocking_panel_visible(BlockingPanel::AssetLibrary, library_ui_ && library_ui_->is_visible());
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

void RoomEditor::open_animation_editor_for_asset(const std::shared_ptr<AssetInfo>& info) {
    open_asset_info_editor(info);
    if (info_ui_) {
        info_ui_->open_animation_editor_panel();
    }
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
    if (spawn_group_panel_) {
        spawn_group_panel_->set_manifest_store(manifest_store_);
    }
    if (room_cfg_ui_) {
        room_cfg_ui_->set_manifest_store(manifest_store_);
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

bool RoomEditor::is_asset_info_lighting_section_expanded() const {
    return info_ui_ && info_ui_->is_lighting_section_expanded();
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
    auto& arr  = ensure_spawn_groups_array(root);

    int width = 0;
    int height = 0;
    SDL_Point center{0, 0};

    if (current_room_->room_area) {
        auto bounds = current_room_->room_area->get_bounds();
        width  = std::max(1, std::get<2>(bounds) - std::get<0>(bounds));
        height = std::max(1, std::get<3>(bounds) - std::get<1>(bounds));
        auto c = current_room_->room_area->get_center();
        center.x = c.x;
        center.y = c.y;
    }

    std::string spawn_id = generate_spawn_id();

    nlohmann::json entry;
    entry["spawn_id"]        = spawn_id;
    entry["position"]        = "Exact";
    entry["dx"]              = asset->pos.x - center.x;
    entry["dy"]              = asset->pos.y - center.y;
    if (width  > 0) entry["origional_width"]  = width;
    if (height > 0) entry["origional_height"] = height;
    entry["display_name"]    = info->name;

    const int default_resolution =
        current_room_ ? current_room_->map_grid_settings().resolution : MapGridSettings::defaults().resolution;

    devmode::spawn::ensure_spawn_group_entry_defaults(entry, info->name, default_resolution);

    entry["candidates"].push_back({{"name", info->name}, {"chance", 100}});

    arr.push_back(entry);
    save_current_room_assets_json();

    asset->spawn_id     = spawn_id;
    asset->spawn_method = "Exact";

    if (asset) {
        refresh_asset_spatial_entry(assets_->getView(), asset);
        ensure_spatial_index(assets_->getView());
    }

    mark_highlight_dirty();

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

void RoomEditor::open_room_config_for(Asset* asset) {
    if (!asset || asset->spawn_id.empty()) {
        open_room_config();
        return;
    }
    set_room_config_visible(true);
    if (room_cfg_ui_) {
        room_cfg_ui_->focus_spawn_group(asset->spawn_id);
    }
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
    const int template_resolution = current_room_ ? current_room_->map_grid_settings().resolution : MapGridSettings::defaults().resolution;
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
    nlohmann::json preserved_identity = nlohmann::json::object();
    static const std::array<const char*, 3> preserved_keys{ "name", "key", "room_key" };
    for (const char* key : preserved_keys) {
        if (target_root.contains(key)) {
            preserved_identity[key] = target_root[key];
        }
    }

    target_root = std::move(template_root);

    for (auto& [key, value] : preserved_identity.items()) {
        target_root[key] = value;
    }

    regenerate_current_room();

    rebuild_room_spawn_id_cache();
    save_current_room_assets_json();
}

void RoomEditor::focus_camera_on_asset(Asset* asset, double zoom_factor, int duration_steps) {
    if (!asset || !assets_) return;

    if (info_ui_ && info_ui_->is_visible()) {
        return;
    }

    WarpedScreenGrid& cam = assets_->getView();
    cam.set_manual_zoom_override(true);
    cam.pan_and_zoom_to_asset(asset, zoom_factor, duration_steps);
    mark_spatial_index_dirty();
}

void RoomEditor::focus_camera_on_room_center(bool reframe_zoom) {
    if (!enabled_ || !assets_) return;
    if (!current_room_ || !current_room_->room_area) return;

    WarpedScreenGrid& cam = assets_->getView();
    const SDL_Point center = current_room_->room_area->get_center();
    cam.set_manual_zoom_override(true);
    cam.set_focus_override(center);

    if (reframe_zoom) {
        cam.zoom_to_area(*current_room_->room_area, 0);
    }
    mark_spatial_index_dirty();
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
    const bool had_selection = !selected_assets_.empty();
    const bool had_highlight = !highlighted_assets_.empty();
    const bool had_hover = hovered_asset_ != nullptr;
    selected_assets_.clear();
    highlighted_assets_.clear();
    hovered_asset_ = nullptr;
    reset_drag_state();
    sync_spawn_group_panel_with_selection();
    if (had_selection || had_highlight || had_hover) {
        mark_highlight_dirty();
    }
    if (!active_assets_) return;
    for (Asset* asset : *active_assets_) {
        if (!asset) continue;
        asset->set_selected(false);
        asset->set_highlighted(false);
    }
}

void RoomEditor::clear_highlighted_assets() {
    const bool had_highlight = !highlighted_assets_.empty();
    const size_t prev_selection_size = selected_assets_.size();
    Asset* prev_hover = hovered_asset_;
    highlighted_assets_.clear();
    if (!active_assets_) {
        selected_assets_.clear();
        hovered_asset_ = nullptr;
        if (had_highlight || prev_selection_size != selected_assets_.size() || hovered_asset_ != prev_hover) {
            mark_highlight_dirty();
        }
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
    if (had_highlight || prev_selection_size != selected_assets_.size() || hovered_asset_ != prev_hover) {
        mark_highlight_dirty();
    }
}

void RoomEditor::purge_asset(Asset* asset) {
    if (!asset) return;
    bool highlight_sources_changed = false;
    if (hovered_asset_ == asset) {
        hovered_asset_ = nullptr;
        hover_miss_frames_ = 0;
        highlight_sources_changed = true;
    }
    remove_asset_from_spatial_index(asset);
    auto erase_from = [asset, &highlight_sources_changed](std::vector<Asset*>& vec) {
        const auto before = vec.size();
        vec.erase(std::remove(vec.begin(), vec.end(), asset), vec.end());
        if (vec.size() != before) {
            highlight_sources_changed = true;
        }
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
    if (highlight_sources_changed) {
        mark_highlight_dirty();
    }
}

void RoomEditor::set_zoom_scale_factor(double factor) {
    zoom_scale_factor_ = (factor > 0.0) ? factor : 1.0;
    pan_zoom_.set_zoom_scale_factor(zoom_scale_factor_);
}

bool RoomEditor::is_spawn_group_panel_visible() const {
    return spawn_group_panel_ && spawn_group_panel_->is_visible();
}

void RoomEditor::set_blocking_panel_visible(BlockingPanel panel, bool visible) {
    const size_t index = static_cast<size_t>(panel);
    if (index >= blocking_panel_visible_.size()) {
        return;
    }
    blocking_panel_visible_[index] = visible;
}

bool RoomEditor::any_blocking_panel_visible() const {
    return std::any_of(blocking_panel_visible_.begin(),
                       blocking_panel_visible_.end(),
                       [](bool state) { return state; });
}

void RoomEditor::handle_mouse_input(const Input& input) {
    if (!input_) return;

    WarpedScreenGrid& cam = assets_->getView();
    const float prev_scale = cam.get_scale();
    const SDL_Point prev_center = cam.get_screen_center();

    const SDL_Point screen_pt{ input_->getX(), input_->getY() };
    const bool left_down                = input_->isDown(Input::LEFT);
    const bool left_pressed_this_frame  = input_->wasPressed(Input::LEFT);
    const bool left_released_this_frame = input_->wasReleased(Input::LEFT);
    const bool shift_down =
        input.isScancodeDown(SDL_SCANCODE_LSHIFT) || input.isScancodeDown(SDL_SCANCODE_RSHIFT);

    Asset* hit_before_pan = hit_test_asset(screen_pt, nullptr);
    const bool pointer_blocks_pan = dragging_ ||
                                    (shift_down && hit_before_pan && !hit_before_pan->spawn_id.empty() && (left_down || left_pressed_this_frame));

    pan_zoom_.handle_input(cam, input, pointer_blocks_pan);
    if (std::fabs(cam.get_scale() - prev_scale) > 1e-6 ||
        cam.get_screen_center().x != prev_center.x ||
        cam.get_screen_center().y != prev_center.y) {
        mark_spatial_index_dirty();
    }

    const SDL_FPoint world_f = cam.screen_to_map(screen_pt);
    SDL_Point world_pt{ (int)std::lround(world_f.x), (int)std::lround(world_f.y) };

    cursor_snap_resolution_ = current_grid_resolution();
    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    snapped_cursor_world_ = grid_service.snap_to_vertex(world_pt, cursor_snap_resolution_);

    Asset* hit = hit_test_asset(screen_pt, nullptr);

    auto rebuild_highlight = [this]() {
        highlighted_assets_.clear();
        if (!selected_assets_.empty()) {
            highlighted_assets_.insert(highlighted_assets_.end(), selected_assets_.begin(), selected_assets_.end());
        }
        if (hovered_asset_) {
            if (std::find(highlighted_assets_.begin(),
                          highlighted_assets_.end(),
                          hovered_asset_) == highlighted_assets_.end()) {
                highlighted_assets_.push_back(hovered_asset_);
            }
        }
        mark_highlight_dirty();
};

    static bool       prev_left_down = false;
    static SDL_Point  press_screen   = {0,0};
    static Asset*     pressed_asset  = nullptr;
    static bool       was_dragged    = false;
    static const int  kDragPx        = 4;

    if (!shift_down && !left_down && !dragging_) {
        pressed_asset = nullptr;
        was_dragged = false;
    }

    if (suppress_next_left_click_) {
        if (click_buffer_frames_ > 0) {
            --click_buffer_frames_;
        } else {
            suppress_next_left_click_ = false;
        }
    }

    if (shift_down && left_down && !prev_left_down) {

        pressed_asset = hit;
        was_dragged   = false;
        press_screen  = screen_pt;

        if (pressed_asset) {

            selected_assets_.clear();
            bool select_group = !pressed_asset->spawn_id.empty();
            if (select_group && active_assets_) {
                for (Asset* a : *active_assets_) {
                    if (!asset_belongs_to_room(a)) continue;
                    if (a->spawn_id == pressed_asset->spawn_id) {
                        selected_assets_.push_back(a);
                    }
                }
            } else {
                if (asset_belongs_to_room(pressed_asset)) {
                    selected_assets_.push_back(pressed_asset);
                }
            }
            sync_spawn_group_panel_with_selection();

            hovered_asset_ = pressed_asset;
            rebuild_highlight();
        } else {

            if (!selected_assets_.empty() || !highlighted_assets_.empty() || hovered_asset_) {
                selected_assets_.clear();
                highlighted_assets_.clear();
                hovered_asset_ = nullptr;
                sync_spawn_group_panel_with_selection();
                mark_highlight_dirty();
            }
        }
    }

    if (left_down && pressed_asset) {
        const int dx = screen_pt.x - press_screen.x;
        const int dy = screen_pt.y - press_screen.y;
        const int dist2 = dx*dx + dy*dy;

        if (!was_dragged && shift_down && dist2 > kDragPx*kDragPx) {
            was_dragged = true;
            dragging_ = true;
            drag_last_world_ = snapped_cursor_world_;
            const bool ctrl_modifier = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
            begin_drag_session(snapped_cursor_world_, ctrl_modifier);
        }

        if (was_dragged && dragging_) {
            update_drag_session(snapped_cursor_world_);

            if (hovered_asset_ != pressed_asset) {
                hovered_asset_ = pressed_asset;
                rebuild_highlight();
            }
        }
    }

    if (!left_down && prev_left_down && pressed_asset) {
        if (pressed_asset) {
            if (was_dragged) {

                if (dragging_) {
                    finalize_drag_session();
                    dragging_ = false;
                }

                suppress_next_left_click_ = true;
                click_buffer_frames_      = 3;

                selected_assets_.clear();
                highlighted_assets_.clear();
                hovered_asset_ = nullptr;
                sync_spawn_group_panel_with_selection();
            } else {

                if (hovered_asset_ == pressed_asset) {
                    open_room_config_for(pressed_asset);

                    suppress_next_left_click_ = true;
                    click_buffer_frames_      = 3;

                    hovered_asset_ = pressed_asset;
                    rebuild_highlight();
                }

            }
        }

        pressed_asset = nullptr;
        was_dragged   = false;
    }

    if (!dragging_) {
        if (hovered_asset_ != hit) {
            hovered_asset_ = hit;
            rebuild_highlight();
        }
    }

    const bool any_left_activity = left_pressed_this_frame || left_released_this_frame || left_down;
    if (!dragging_ && !suppress_next_left_click_ && !any_left_activity) {
        handle_click(input);
    }

    prev_left_down = left_down;
}

Asset* RoomEditor::hit_test_asset(SDL_Point screen_point, SDL_Renderer* ) const {
    if (!active_assets_ || !assets_) return nullptr;

    const WarpedScreenGrid& cam = assets_->getView();

    if (!ensure_spatial_index(cam)) {

        return hit_test_asset_fallback(cam, screen_point);
    }

    const std::vector<Asset*> candidates = gather_candidate_assets_for_point(screen_point);
    if (!candidates.empty()) {
        Asset* best = nullptr;
        int best_bottom = std::numeric_limits<int>::max();
        int best_top = std::numeric_limits<int>::max();
        int best_screen_y = std::numeric_limits<int>::max();
        int best_z = std::numeric_limits<int>::min();
        int best_area = std::numeric_limits<int>::max();

        auto consider_candidate = [&](Asset* asset,
                                      const SDL_Rect& rect,
                                      int screen_y,
                                      int z_index) {
            if (!asset) return;
            if (!asset->spawn_id.empty() && spawn_group_locked(asset->spawn_id)) {
                return;
            }
            if (!SDL_PointInRect(&screen_point, &rect)) {
                return;
            }
            const int bottom = rect.y + rect.h;
            const int top = rect.y;
            const int area = rect.w * rect.h;
            const bool is_better =
                !best ||
                bottom < best_bottom ||
                (bottom == best_bottom && top < best_top) || (bottom == best_bottom && top == best_top && screen_y < best_screen_y) || (bottom == best_bottom && top == best_top && screen_y == best_screen_y && z_index > best_z) || (bottom == best_bottom && top == best_top && screen_y == best_screen_y && z_index == best_z && area < best_area);
            if (is_better) {
                best = asset;
                best_bottom = bottom;
                best_top = top;
                best_screen_y = screen_y;
                best_z = z_index;
                best_area = area;
            }
};

        for (Asset* asset : candidates) {
            auto bc = asset_bounds_cache_.find(asset);
            if (bc == asset_bounds_cache_.end()) {
                continue;
            }
            const AssetSpatialEntry& entry = bc->second;
            consider_candidate(asset, entry.bounds, entry.screen_y, entry.z_index);
        }

        if (best) {
            return best;
        }
    }

    return hit_test_asset_fallback(cam, screen_point);
}

void RoomEditor::mark_spatial_index_dirty() const {
    spatial_index_dirty_ = true;
    cached_camera_state_valid_ = false;
    cached_reference_height_valid_ = false;
    cached_reference_screen_height_ = 1.0f;
    asset_bounds_cache_.clear();
    spatial_grid_.clear();
}

bool RoomEditor::camera_state_changed(const WarpedScreenGrid& cam) const {
    if (!cached_camera_state_valid_) {
        return false;
    }
    const float scale = cam.get_scale();
    if (std::fabs(scale - cached_camera_scale_) > kCameraScaleEpsilon) {
        return true;
    }
    SDL_Point center = cam.get_screen_center();
    if (center.x != cached_camera_center_.x || center.y != cached_camera_center_.y) {
        return true;
    }
    if (cam.parallax_enabled() != cached_camera_parallax_enabled_) {
        return true;
    }
    if (cam.realism_enabled() != cached_camera_realism_enabled_) {
        return true;
    }
    return false;
}

bool RoomEditor::ensure_spatial_index(const WarpedScreenGrid& cam) const {
    if (!active_assets_) {
        return false;
    }

    if (camera_state_changed(cam)) {
        mark_spatial_index_dirty();
    }

    if (spatial_index_dirty_) {
        rebuild_spatial_index(cam);
    }

    return !spatial_index_dirty_;
}

float RoomEditor::compute_reference_screen_height(const WarpedScreenGrid& cam, float inv_scale) const {
    float reference_screen_height = 1.0f;
    Asset* player_asset = player_ ? player_ : (assets_ ? assets_->player : nullptr);
    if (!player_asset) {
        return reference_screen_height;
    }

    SDL_Texture* player_frame = player_asset->get_current_frame();
    int pw = player_asset->cached_w;
    int ph = player_asset->cached_h;
    if ((pw == 0 || ph == 0) && player_frame) {
        SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
    }
    if ((pw == 0 || ph == 0) && player_asset->info) {
        pw = player_asset->info->original_canvas_width;
        ph = player_asset->info->original_canvas_height;
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
    if (reference_screen_height <= 0.0f) {
        reference_screen_height = 1.0f;
    }
    return reference_screen_height;
}

bool RoomEditor::compute_asset_screen_bounds(const WarpedScreenGrid& cam,
                                             float reference_height,
                                             float inv_scale,
                                             Asset* asset,
                                             SDL_Rect& out_rect,
                                             int& out_screen_y) const {
    if (!asset) {
        return false;
    }

    SDL_Texture* tex = asset->get_current_frame();

    int fw = asset->cached_w;
    int fh = asset->cached_h;
    if ((fw == 0 || fh == 0) && tex) {
        SDL_QueryTexture(tex, nullptr, nullptr, &fw, &fh);
        if (asset->cached_w == 0) asset->cached_w = fw;
        if (asset->cached_h == 0) asset->cached_h = fh;
    }
    if ((fw == 0 || fh == 0) && asset->info) {
        fw = asset->info->original_canvas_width;
        fh = asset->info->original_canvas_height;
        if (asset->cached_w == 0) asset->cached_w = fw;
        if (asset->cached_h == 0) asset->cached_h = fh;
    }
    if (fw <= 0 || fh <= 0) return false;

    float base_scale = 1.0f;
    if (asset->info && std::isfinite(asset->info->scale_factor) && asset->info->scale_factor >= 0.0f) {
        base_scale = asset->info->scale_factor;
    }

    const float scaled_fw = static_cast<float>(fw) * base_scale;
    const float scaled_fh = static_cast<float>(fh) * base_scale;
    const float base_sw = scaled_fw * inv_scale;
    const float base_sh = scaled_fh * inv_scale;

    const float world_x = static_cast<float>(asset->pos.x);
    const float world_y = static_cast<float>(asset->pos.y);
    const WarpedScreenGrid::RenderEffects effects =
        cam.compute_render_effects(
            SDL_Point{ static_cast<int>(std::lround(world_x)), static_cast<int>(std::lround(world_y)) },
            base_sh,
            reference_height,
            WarpedScreenGrid::RenderSmoothingKey(asset));

    const float scaled_sw = base_sw * effects.distance_scale;
    const float scaled_sh = base_sh * effects.distance_scale;
    const float final_visible_h = scaled_sh * effects.vertical_scale;

    const int sw = std::max(1, static_cast<int>(std::lround(static_cast<double>(scaled_sw))));
    const int sh = std::max(1, static_cast<int>(std::lround(static_cast<double>(final_visible_h))));
    if (sw <= 0 || sh <= 0) return false;

    SDL_Point world_point{
        static_cast<int>(std::lround(world_x)), static_cast<int>(std::lround(world_y)) };
    float center_x = effects.screen_position.x;
    if (assets_) {

        if (!(asset && assets_->player == asset)) {

        }
    }
    const int   left     = static_cast<int>(std::lround(center_x - static_cast<float>(sw) * 0.5f));
    const int   top      = static_cast<int>(std::lround(effects.screen_position.y)) - sh;
    out_rect             = SDL_Rect{left, top, sw, sh};
    out_screen_y         = static_cast<int>(std::lround(effects.screen_position.y));
    return true;
}

void RoomEditor::rebuild_spatial_index(const WarpedScreenGrid& cam) const {
    asset_bounds_cache_.clear();
    spatial_grid_.clear();

    const float scale = std::max(0.0001f, cam.get_scale());
    const float inv_scale = 1.0f / scale;
    const float reference_height = compute_reference_screen_height(cam, inv_scale);

    if (active_assets_) {
        for (Asset* asset : *active_assets_) {
            if (!asset) continue;
            SDL_Rect rect{0, 0, 0, 0};
            int screen_y = 0;
            if (!compute_asset_screen_bounds(cam, reference_height, inv_scale, asset, rect, screen_y)) {
                continue;
            }
            insert_asset_entry(asset, rect, screen_y);
        }
    }

    cached_camera_scale_ = cam.get_scale();
    cached_camera_center_ = cam.get_screen_center();
    cached_camera_parallax_enabled_ = cam.parallax_enabled();
    cached_camera_realism_enabled_ = cam.realism_enabled();
    cached_camera_state_valid_ = true;
    cached_reference_screen_height_ = reference_height;
    cached_reference_height_valid_ = true;
    spatial_index_dirty_ = false;
}

void RoomEditor::insert_asset_entry(Asset* asset, const SDL_Rect& rect, int screen_y) const {
    if (!asset || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    AssetSpatialEntry entry;
    entry.bounds = rect;
    entry.screen_y = screen_y;
    entry.z_index = asset->z_index;

    const int left = floor_div(rect.x, kSpatialCellSize);
    const int right = floor_div(rect.x + rect.w - 1, kSpatialCellSize);
    const int top = floor_div(rect.y, kSpatialCellSize);
    const int bottom = floor_div(rect.y + rect.h - 1, kSpatialCellSize);

    for (int cx = left; cx <= right; ++cx) {
        for (int cy = top; cy <= bottom; ++cy) {
            add_asset_to_cell(asset, cx, cy, entry.cells);
        }
    }

    asset_bounds_cache_[asset] = std::move(entry);
}

void RoomEditor::add_asset_to_cell(Asset* asset, int cell_x, int cell_y, std::vector<int64_t>& cell_keys) const {
    if (!asset) return;
    const int64_t key = make_cell_key(cell_x, cell_y);
    auto& bucket = spatial_grid_[key];
    bucket.push_back(asset);
    cell_keys.push_back(key);
}

void RoomEditor::remove_asset_from_spatial_index(Asset* asset) const {
    if (!asset) return;
    auto it = asset_bounds_cache_.find(asset);
    if (it == asset_bounds_cache_.end()) {
        return;
    }
    const std::vector<int64_t> cells = it->second.cells;
    for (int64_t key : cells) {
        auto grid_it = spatial_grid_.find(key);
        if (grid_it == spatial_grid_.end()) {
            continue;
        }
        auto& bucket = grid_it->second;
        bucket.erase(std::remove(bucket.begin(), bucket.end(), asset), bucket.end());
        if (bucket.empty()) {
            spatial_grid_.erase(grid_it);
        }
    }
    asset_bounds_cache_.erase(it);
}

void RoomEditor::refresh_asset_spatial_entry(const WarpedScreenGrid& cam, Asset* asset) const {
    if (!asset) return;
    if (spatial_index_dirty_ || !cached_camera_state_valid_ || !cached_reference_height_valid_) {
        return;
    }

    remove_asset_from_spatial_index(asset);

    const float scale = std::max(0.0001f, cam.get_scale());
    const float inv_scale = 1.0f / scale;
    SDL_Rect rect{0, 0, 0, 0};
    int screen_y = 0;
    if (!compute_asset_screen_bounds(cam, cached_reference_screen_height_, inv_scale, asset, rect, screen_y)) {
        return;
    }
    insert_asset_entry(asset, rect, screen_y);
}

void RoomEditor::refresh_spatial_entries_for_dragged_assets() {
    if (!assets_) {
        return;
    }
    const WarpedScreenGrid& cam = assets_->getView();
    if (spatial_index_dirty_ || !cached_camera_state_valid_ || !cached_reference_height_valid_) {
        return;
    }

    for (const auto& state : drag_states_) {
        if (!state.asset) continue;
        refresh_asset_spatial_entry(cam, state.asset);
    }
}

void RoomEditor::sync_dragged_assets_immediately() {
    bool moved_any = false;
    for (auto& state : drag_states_) {
        Asset* asset = state.asset;
        if (!asset) {
            continue;
        }
        SDL_Point current{asset->pos.x, asset->pos.y};
        if (current.x == state.last_synced_pos.x && current.y == state.last_synced_pos.y) {
            continue;
        }
        asset->clear_grid_residency_cache();
        asset->sync_transform_to_position();
        asset->mark_composite_dirty();
        if (assets_) {
            (void)assets_->world_grid().move_asset(asset, state.last_synced_pos, current);
        }
        state.last_synced_pos = current;
        moved_any = true;
    }
    if (moved_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

std::vector<Asset*> RoomEditor::gather_candidate_assets_for_point(SDL_Point screen_point) const {
    std::vector<Asset*> result;
    if (spatial_grid_.empty()) {
        return result;
    }

    const int cell_x = floor_div(screen_point.x, kSpatialCellSize);
    const int cell_y = floor_div(screen_point.y, kSpatialCellSize);
    std::unordered_set<Asset*> unique;
    unique.reserve(16);

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            const int64_t key = make_cell_key(cell_x + dx, cell_y + dy);
            auto it = spatial_grid_.find(key);
            if (it == spatial_grid_.end()) {
                continue;
            }
            for (Asset* asset : it->second) {
                if (!asset) continue;
                if (unique.insert(asset).second) {
                    result.push_back(asset);
                }
            }
        }
    }

    return result;
}

Asset* RoomEditor::hit_test_asset_fallback(const WarpedScreenGrid& cam, SDL_Point screen_point) const {
    if (!active_assets_) {
        return nullptr;
    }

    const float scale = std::max(0.0001f, cam.get_scale());
    const float inv_scale = 1.0f / scale;
    const float reference_height = compute_reference_screen_height(cam, inv_scale);

    Asset* best = nullptr;
    int best_bottom = std::numeric_limits<int>::max();
    int best_top = std::numeric_limits<int>::max();
    int best_screen_y = std::numeric_limits<int>::max();
    int best_z = std::numeric_limits<int>::min();
    int best_area = std::numeric_limits<int>::max();

    auto consider_candidate = [&](Asset* asset,
                                  const SDL_Rect& rect,
                                  int screen_y,
                                  int z_index) {
        if (!asset) return;
        if (!asset->spawn_id.empty() && spawn_group_locked(asset->spawn_id)) {
            return;
        }
        if (!SDL_PointInRect(&screen_point, &rect)) {
            return;
        }
        const int bottom = rect.y + rect.h;
        const int top = rect.y;
        const int area = rect.w * rect.h;
        const bool is_better =
            !best ||
            bottom < best_bottom ||
            (bottom == best_bottom && top < best_top) || (bottom == best_bottom && top == best_top && screen_y < best_screen_y) || (bottom == best_bottom && top == best_top && screen_y == best_screen_y && z_index > best_z) || (bottom == best_bottom && top == best_top && screen_y == best_screen_y && z_index == best_z && area < best_area);
        if (is_better) {
            best = asset;
            best_bottom = bottom;
            best_top = top;
            best_screen_y = screen_y;
            best_z = z_index;
            best_area = area;
        }
};

    for (Asset* asset : *active_assets_) {
        if (!asset) continue;
        if (!asset->spawn_id.empty() && spawn_group_locked(asset->spawn_id)) {
            continue;
        }

        SDL_Rect rect{0, 0, 0, 0};
        int screen_y = 0;
        if (!compute_asset_screen_bounds(cam, reference_height, inv_scale, asset, rect, screen_y)) {
            continue;
        }

        consider_candidate(asset, rect, screen_y, asset->z_index);
    }

    return best;
}

void RoomEditor::update_hover_state(Asset* hit) {
    Asset* previous = hovered_asset_;
    if (hit) {
        hovered_asset_ = hit;
        hover_miss_frames_ = 0;
    } else {
        if (++hover_miss_frames_ >= 3) {
            hovered_asset_ = nullptr;
            hover_miss_frames_ = 3;
        }
    }
    if (hovered_asset_ != previous) {
        mark_highlight_dirty();
    }
}

std::optional<std::string> RoomEditor::find_room_area_at_point(SDL_Point world_point) {
    if (!current_room_) {
        return std::nullopt;
    }

    nlohmann::json& root = current_room_->assets_data();

    struct AreaMetadata {
        int z = 0;
        bool visible = true;
        std::size_t order = 0;
};

    std::unordered_map<std::string, AreaMetadata> metadata;
    std::size_t order_counter = 0;

    if (root.is_object() && root.contains("areas") && root["areas"].is_array()) {
        for (const auto& entry : root["areas"]) {
            if (!entry.is_object()) {
                ++order_counter;
                continue;
            }

            std::string name = entry.value("name", std::string{});
            if (name.empty()) {
                ++order_counter;
                continue;
            }

            AreaMetadata data;
            data.z = entry.value("z", 0);
            data.visible = !(entry.contains("visible") && entry["visible"].is_boolean() && !entry["visible"].get<bool>());
            data.order = order_counter;
            metadata.insert_or_assign(name, data);

            ++order_counter;
        }
    }

    std::size_t fallback_order = order_counter;
    std::optional<std::string> best_name;
    int best_z = std::numeric_limits<int>::min();
    std::size_t best_order = 0;
    bool have_best_order = false;

    auto consider_area = [&](const std::string& name, const Area* area) {
        if (!area) {
            return;
        }

        auto it = metadata.find(name);
        AreaMetadata info;
        if (it != metadata.end()) {
            info = it->second;
        } else {
            info.order = fallback_order++;
        }

        if (!info.visible) {
            return;
        }

        if (!area->contains_point(world_point)) {
            return;
        }

        bool take = false;
        if (!best_name) {
            take = true;
        } else if (info.z > best_z) {
            take = true;
        } else if (info.z == best_z) {
            if (!have_best_order || info.order >= best_order) {
                take = true;
            }
        }

        if (take) {
            best_name = name;
            best_z = info.z;
            best_order = info.order;
            have_best_order = true;
        }
};

    for (const auto& named : current_room_->areas) {
        if (named.name.empty()) {
            continue;
        }
        consider_area(named.name, named.area.get());
    }

    return best_name;
}

void RoomEditor::handle_click(const Input& input) {
    if (!input_) return;

    SDL_Point world_mouse = snapped_cursor_world_;

    bool selection_changed = false;
    bool highlight_changed = false;

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

        const bool shift_modifier =
            input.isScancodeDown(SDL_SCANCODE_LSHIFT) || input.isScancodeDown(SDL_SCANCODE_RSHIFT);
        auto open_library_at = [&](const SDL_Point& point) {
            pending_spawn_world_pos_ = point;
            open_asset_library();
            if (!is_asset_library_open()) {
                pending_spawn_world_pos_.reset();
            }
};

        if (hovered_asset_) {
            if (shift_modifier) {
                open_asset_info_editor_for_asset(hovered_asset_);
            } else {
                open_library_at(world_mouse);
            }
        } else {
            bool inside_room = true;
            if (current_room_ && current_room_->room_area) {
                inside_room = current_room_->room_area->contains_point(world_mouse);
            }
            if (inside_room) {
                open_library_at(world_mouse);
            } else {
                pending_spawn_world_pos_.reset();
                open_asset_library();
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

    click_buffer_frames_ = std::max(0, click_buffer_frames_ - 1);

    const bool asset_info_open =
        (active_modal_ == ActiveModal::AssetInfo) || (info_ui_ && info_ui_->is_visible());
    const bool floating_modal_open = FloatingDockableManager::instance().active_panel() != nullptr;

    if (asset_info_open || floating_modal_open) {
        return;
    }

    if (hovered_asset_) {
        Asset* nearest = hovered_asset_;
        if (!nearest) {
            if (!selected_assets_.empty()) selection_changed = true;
            selected_assets_.clear();
            if (!highlighted_assets_.empty()) highlight_changed = true;
            highlighted_assets_.clear();
            sync_spawn_group_panel_with_selection();
            return;
        }

        bool already_selected = false;
        for (Asset* a : selected_assets_) {
            if (a == nearest) {
                already_selected = true;
                break;
            }
        }

        if (!already_selected) {
            if (!selected_assets_.empty()) selection_changed = true;
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
        }
        sync_spawn_group_panel_with_selection();
    } else {
        if (!selected_assets_.empty()) selection_changed = true;
        selected_assets_.clear();
        if (!highlighted_assets_.empty()) highlight_changed = true;
        highlighted_assets_.clear();
        sync_spawn_group_panel_with_selection();

        const bool asset_info_open2 = (active_modal_ == ActiveModal::AssetInfo);
        const bool floating_modal_open2 = FloatingDockableManager::instance().active_panel() != nullptr;

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

    }
    if (selection_changed || highlight_changed) {
        mark_highlight_dirty();
    }
}

nlohmann::json* RoomEditor::find_area_entry_json(Room* room, const std::string& area_name) const {
    if (!room) return nullptr;
    nlohmann::json& root = room->assets_data();
    if (root.is_object() && root.contains("areas") && root["areas"].is_array()) {
        for (auto& entry : root["areas"]) {
            if (!entry.is_object()) continue;
            if (entry.value("name", std::string{}) == area_name) {
                return &entry;
            }
        }
    }
    return nullptr;
}

void RoomEditor::ensure_area_anchor_spawn_entry(Room* room, const std::string& area_name) {
    if (!room) return;
    nlohmann::json& root = room->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    nlohmann::json* existing = nullptr;
    for (auto& entry : groups) {
        if (!entry.is_object()) continue;
        const bool linked = entry.value("link_to_area", false);
        const std::string linked_area = entry.value("linked_area", std::string{});
        const std::string display = entry.value("display_name", std::string{});
        if ((linked && linked_area == area_name) || (!linked && display == area_name)) {
            existing = &entry;
            break;
        }
    }
    const int default_resolution = room->map_grid_settings().resolution;
    int width = 0, height = 0;
    if (room->room_area) {
        auto b = room->room_area->get_bounds();
        width = std::max(1, std::get<2>(b) - std::get<0>(b));
        height = std::max(1, std::get<3>(b) - std::get<1>(b));
    }
    if (!existing) {
        nlohmann::json entry = nlohmann::json::object();
        entry["display_name"] = area_name;
        entry["position"] = "Exact";
        entry["dx"] = 0;
        entry["dy"] = 0;
        if (width > 0) entry["origional_width"] = width;
        if (height > 0) entry["origional_height"] = height;
        entry["link_to_area"] = true;
        entry["linked_area"] = area_name;
        devmode::spawn::ensure_spawn_group_entry_defaults(entry, area_name, default_resolution);
        groups.push_back(std::move(entry));
        save_current_room_assets_json();
    } else {
        devmode::spawn::ensure_spawn_group_entry_defaults(*existing, area_name, default_resolution);
        if (existing->value("position", std::string{"Random"}) != std::string{"Exact"}) {
            (*existing)["position"] = "Exact";
        }
        if (width > 0 && !existing->contains("origional_width")) (*existing)["origional_width"] = width;
        if (height > 0 && !existing->contains("origional_height")) (*existing)["origional_height"] = height;
        if (!existing->value("link_to_area", false)) (*existing)["link_to_area"] = true;
        if (existing->value("linked_area", std::string{}) != area_name) (*existing)["linked_area"] = area_name;
        save_current_room_assets_json();
    }
}

void RoomEditor::begin_area_drag_session(const std::string& area_name, const SDL_Point& world_mouse) {
    area_dragging_ = true;
    area_drag_moved_ = false;
    area_drag_name_ = area_name;
    area_drag_last_world_ = world_mouse;
    area_drag_start_world_ = world_mouse;
    MapGridSettings map_settings = current_room_ ? current_room_->map_grid_settings() : MapGridSettings::defaults();
    map_settings.clamp();
    area_drag_resolution_ = vibble::grid::clamp_resolution(map_settings.resolution);

    ensure_area_anchor_spawn_entry(current_room_, area_drag_name_);
}

void RoomEditor::update_area_drag_session(const SDL_Point& world_mouse) {
    area_drag_last_world_ = world_mouse;
    area_drag_moved_ = true;
}

void RoomEditor::finalize_area_drag_session() {
    if (!current_room_ || area_drag_name_.empty()) {
        area_dragging_ = false;
        area_drag_moved_ = false;
        return;
    }

    vibble::grid::Grid& grid_service = vibble::grid::global_grid();
    SDL_Point snapped = grid_service.snap_to_vertex(area_drag_last_world_, area_drag_resolution_);

    SDL_Point center{0, 0};
    if (current_room_->room_area) {
        auto c = current_room_->room_area->get_center();
        center.x = c.x; center.y = c.y;
    }
    const int dx = snapped.x - center.x;
    const int dy = snapped.y - center.y;

    if (nlohmann::json* area_entry = find_area_entry_json(current_room_, area_drag_name_)) {
        (*area_entry)["anchor_relative_to_center"] = true;
        (*area_entry)["anchor"] = nlohmann::json::object({ {"x", dx}, {"y", dy} });

        current_room_->save_assets_json();
    }

    nlohmann::json& root = current_room_->assets_data();
    if (auto* groups_ptr = devmode::spawn::find_spawn_groups_array(root)) {
        for (auto& entry : const_cast<nlohmann::json&>(*groups_ptr)) {
            if (!entry.is_object()) continue;
            if (entry.value("link_to_area", false) && entry.value("linked_area", std::string{}) == area_drag_name_) {
                entry["position"] = "Exact";
                entry["dx"] = dx;
                entry["dy"] = dy;

                if (current_room_->room_area) {
                    auto b = current_room_->room_area->get_bounds();
                    const int w = std::max(1, std::get<2>(b) - std::get<0>(b));
                    const int h = std::max(1, std::get<3>(b) - std::get<1>(b));
                    if (!entry.contains("origional_width")) entry["origional_width"] = w;
                    if (!entry.contains("origional_height")) entry["origional_height"] = h;
                }
                break;
            }
        }
    }

    save_current_room_assets_json();
    area_dragging_ = false;
    area_drag_moved_ = false;
}

void RoomEditor::update_highlighted_assets() {
    if (!highlight_dirty_) {
        return;
    }
    highlight_dirty_ = false;
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
                if (spawn_group_locked(asset->spawn_id)) {
                    continue;
                }
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

    if (hovered_asset_ && asset_belongs_to_room(hovered_asset_) &&
        (hovered_asset_->spawn_id.empty() || !spawn_group_locked(hovered_asset_->spawn_id)) &&
        std::find(highlighted_assets_.begin(), highlighted_assets_.end(), hovered_asset_) == highlighted_assets_.end()) {
        highlighted_assets_.push_back(hovered_asset_);
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
    auto floating = FloatingDockableManager::instance().open_panels();
    for (DockableCollapsible* panel : floating) {
        if (!panel) continue;
        if (!panel->is_visible()) continue;
        if (spawn_group_panel_ && panel == spawn_group_panel_.get()) continue;
        if (panel->is_point_inside(mx, my)) {
            return true;
        }
    }

    return false;
}

bool RoomEditor::should_enable_mouse_controls() const {
    if (!enabled_) {
        return false;
    }

    if (active_modal_ != ActiveModal::None && active_modal_ != ActiveModal::AssetInfo) {
        return false;
    }

    return true;
}

void RoomEditor::handle_shortcuts(const Input& input) {
    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (!ctrl) return;

    if (input.wasScancodePressed(SDL_SCANCODE_C)) {
        copy_selected_spawn_group();
    }
    if (input.wasScancodePressed(SDL_SCANCODE_V)) {
        paste_spawn_group_from_clipboard();
    }

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

void RoomEditor::ensure_room_configurator() {
    if (!room_cfg_ui_) {
        room_cfg_ui_ = std::make_unique<RoomConfigurator>();
    }
    if (room_cfg_ui_) {
        room_cfg_ui_->set_manifest_store(manifest_store_);
        room_cfg_ui_->set_header_visibility_controller([this](bool visible) {
            room_config_panel_visible_ = visible;
            if (header_visibility_callback_) {
                header_visibility_callback_(room_config_panel_visible_ || asset_info_panel_visible_);
            }
        });
        room_cfg_ui_->set_bounds(room_config_bounds_);

        room_cfg_ui_->set_work_area(FloatingPanelLayoutManager::instance().usableRect());
        room_cfg_ui_->set_blocks_editor_interactions(false);
        room_cfg_ui_->set_on_close([this]() {
            if (!suppress_room_config_selection_clear_) {
                clear_selection();
            }
            room_config_dock_open_ = false;
            update_spawn_group_config_anchor();
        });
        room_cfg_ui_->set_spawn_group_callbacks(
            [this](const std::string& spawn_id) {
                if (active_modal_ == ActiveModal::AssetInfo) {
                    pulse_active_modal_header();
                    return;
                }

                set_room_config_visible(true);
                if (room_cfg_ui_) {
                    room_cfg_ui_->focus_spawn_group(spawn_id);
                }

                if (spawn_group_panel_) {
                    spawn_group_panel_->close();
                    spawn_group_panel_->set_visible(false);
                }
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
        invalidate_label_cache(current_room_);
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

    spawn_group_panel_->set_manifest_store(manifest_store_);
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
    const int side_margin = 0;
    const int available_width = std::max(0, screen_w_ - 2 * side_margin);
    const int max_width = std::max(320, available_width);
    const int desired_width = std::max(360, screen_w_ / 3);
    const int width = std::min(max_width, desired_width);

    SDL_Rect usable = FloatingPanelLayoutManager::instance().usableRect();
    const int height = std::max(1, usable.h > 0 ? usable.h : screen_h_);
    const int max_x = std::max(0, screen_w_ - width);
    const int desired_x = screen_w_ - width;
    const int x = std::clamp(desired_x, 0, max_x);
    const int y = usable.h > 0 ? usable.y : 0;
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
    if (!current_room_) return;

    std::vector<std::string> spawn_ids;
    spawn_ids.reserve(selected_assets_.size() + 1);
    auto append_spawn_id = [&spawn_ids](Asset* asset) {
        if (!asset) {
            return;
        }
        const std::string& id = asset->spawn_id;
        if (id.empty()) {
            return;
        }
        if (std::find(spawn_ids.begin(), spawn_ids.end(), id) == spawn_ids.end()) {
            spawn_ids.push_back(id);
        }
};

    for (Asset* asset : selected_assets_) {
        append_spawn_id(asset);
    }

    if (spawn_ids.empty()) {
        append_spawn_id(hovered_asset_);
    }

    if (spawn_ids.empty() && active_spawn_group_id_ && !active_spawn_group_id_->empty()) {
        spawn_ids.push_back(*active_spawn_group_id_);
    }

    bool deleted_any = false;
    for (const std::string& id : spawn_ids) {
        if (delete_spawn_group_internal(id)) {
            deleted_any = true;
        }
    }

    if (deleted_any) {
        clear_selection();
    }
}

void RoomEditor::begin_drag_session(const SDL_Point& world_mouse, bool ctrl_modifier) {

    if (!selected_assets_.empty()) {
        Asset* primary = selected_assets_.front();
        if (primary && !primary->spawn_id.empty() && spawn_group_locked(primary->spawn_id)) {
            return;
        }
    }

    if (room_config_dock_open_) {

        suppress_room_config_selection_clear_ = true;
        set_room_config_visible(false);
        suppress_room_config_selection_clear_ = false;
    }

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
    overlay_resolution_before_drag_.reset();

    MapGridSettings map_settings = current_room_ ? current_room_->map_grid_settings() : MapGridSettings::defaults();
    map_settings.clamp();

    int desired_resolution = cursor_snap_resolution_ > 0 ? cursor_snap_resolution_ : map_settings.resolution;
    drag_resolution_ = vibble::grid::clamp_resolution(desired_resolution);
    SpawnEntryResolution resolved_entry = drag_spawn_id_.empty() ? SpawnEntryResolution{} : locate_spawn_entry(drag_spawn_id_);
    nlohmann::json* spawn_entry = resolved_entry.entry;
    if (spawn_entry && drag_mode_ != DragMode::Exact) {
        drag_resolution_ = vibble::grid::clamp_resolution(spawn_entry->value("resolution", drag_resolution_));
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

        drag_mode_ = DragMode::Free;
    } else {
        drag_mode_ = DragMode::Free;
    }

    bool resolve_geometry = (method == "Exact" || method == "Exact Position" || method == "Perimeter");

    const bool editing_spawn_config = is_spawn_group_panel_visible() && active_spawn_group_id_.has_value();

    if (shared_footer_bar_) {
        if (editing_spawn_config) {

            drag_resolution_ = vibble::grid::clamp_resolution(shared_footer_bar_->grid_resolution());
            overlay_resolution_before_drag_.reset();
        } else {

            overlay_resolution_before_drag_ = shared_footer_bar_->grid_resolution();
            shared_footer_bar_->set_grid_resolution(vibble::grid::clamp_resolution(drag_resolution_));
        }
    } else {
        overlay_resolution_before_drag_.reset();
    }

    auto [room_w, room_h] = get_room_dimensions();
    drag_perimeter_curr_w_ = room_w;
    drag_perimeter_curr_h_ = room_h;
    drag_perimeter_orig_w_ = std::max(1, room_w);
    drag_perimeter_orig_h_ = std::max(1, room_h);
    drag_perimeter_center_offset_world_ = SDL_Point{0, 0};
    drag_perimeter_circle_center_ = drag_room_center_;

    if (spawn_entry) {
        resolve_geometry = spawn_entry->value( "resolve_geometry_to_room_size", resolve_geometry);
        int orig_w = std::max(1, drag_perimeter_curr_w_);
        int orig_h = std::max(1, drag_perimeter_curr_h_);
        if (resolve_geometry) {
            orig_w = std::max(1, spawn_entry->value("origional_width", orig_w));
            orig_h = std::max(1, spawn_entry->value("origional_height", orig_h));
        }
        drag_perimeter_orig_w_ = orig_w;
        drag_perimeter_orig_h_ = orig_h;
        const int stored_dx = spawn_entry->value("dx", 0);
        const int stored_dy = spawn_entry->value("dy", 0);
        RelativeRoomPosition relative(SDL_Point{stored_dx, stored_dy}, orig_w, orig_h);
        drag_perimeter_center_offset_world_ = relative.scaled_offset(room_w, room_h);
        drag_perimeter_circle_center_.x = drag_room_center_.x + drag_perimeter_center_offset_world_.x;
        drag_perimeter_circle_center_.y = drag_room_center_.y + drag_perimeter_center_offset_world_.y;
        if ((*spawn_entry).contains("radius") && (*spawn_entry)["radius"].is_number_integer()) {
            drag_perimeter_base_radius_ = std::max(0, (*spawn_entry)["radius"].get<int>());
            if (resolve_geometry && drag_perimeter_base_radius_ > 0.0) {
                const double width_ratio = static_cast<double>(std::max(1, room_w)) / static_cast<double>(orig_w);
                const double height_ratio = static_cast<double>(std::max(1, room_h)) / static_cast<double>(orig_h);
                const double ratio = (width_ratio + height_ratio) * 0.5;
                drag_perimeter_base_radius_ = std::max(0.0, drag_perimeter_base_radius_ * ratio);
            }
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
        state.last_synced_pos = asset->pos;
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

    auto invalidate_after_move = [this]() {
        sync_dragged_assets_immediately();
        for (auto& st : drag_states_) {
            if (st.asset) {
                auto it = asset_bounds_cache_.find(st.asset);
                if (it != asset_bounds_cache_.end()) {
                    asset_bounds_cache_.erase(it);
                }
            }
        }
        mark_spatial_index_dirty();
        mark_highlight_dirty();
        refresh_spatial_entries_for_dragged_assets();
};

    if (drag_mode_ == DragMode::Perimeter) {
        apply_perimeter_drag(world_mouse);
        drag_last_world_ = world_mouse;
        drag_moved_ = true;
        invalidate_after_move();
        ensure_spatial_index(assets_->getView());
        return;
    }

    if (drag_mode_ == DragMode::Edge) {
        apply_edge_drag(world_mouse);
        drag_last_world_ = world_mouse;
        drag_moved_ = true;
        invalidate_after_move();
        ensure_spatial_index(assets_->getView());
        return;
    }

    SDL_Point delta{world_mouse.x - drag_last_world_.x, world_mouse.y - drag_last_world_.y};
    const bool anchor_should_follow_pointer =
        (drag_mode_ == DragMode::Exact || drag_mode_ == DragMode::Percent);
    if (anchor_should_follow_pointer) {
        Asset* anchor_asset = drag_anchor_asset_;
        if (!anchor_asset && !drag_states_.empty()) {
            anchor_asset = drag_states_.front().asset;
        }
        if (anchor_asset) {

            vibble::grid::Grid& grid_service = vibble::grid::global_grid();
            SDL_Point snapped_pointer = grid_service.snap_to_vertex(world_mouse, drag_resolution_);
            delta.x = snapped_pointer.x - anchor_asset->pos.x;
            delta.y = snapped_pointer.y - anchor_asset->pos.y;
        }
    }

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

    invalidate_after_move();
    ensure_spatial_index(assets_->getView());

    update_spawn_json_during_drag();
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

    double new_radius = std::hypot(static_cast<double>(world_mouse.x - drag_perimeter_circle_center_.x), static_cast<double>(world_mouse.y - drag_perimeter_circle_center_.y));
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
            if (base <= 0.0) base = std::hypot(dx, dy);
            if (dx != 0.0 || dy != 0.0) {
                state_dir.x = static_cast<float>(dx / std::hypot(dx, dy));
                state_dir.y = static_cast<float>(dy / std::hypot(dx, dy));
            } else {
                state_dir.x = 0.0f;
                state_dir.y = -1.0f;
            }
        }
        double desired = base * ratio;
        int new_x = drag_perimeter_circle_center_.x + static_cast<int>(std::lround(static_cast<double>(state_dir.x) * desired));
        int new_y = drag_perimeter_circle_center_.y + static_cast<int>(std::lround(static_cast<double>(state_dir.y) * desired));
        if (state.asset->pos.x != new_x || state.asset->pos.y != new_y) {
            state.asset->pos.x = new_x;
            state.asset->pos.y = new_y;
            changed = true;
        }
    }
    if (changed) {
        drag_moved_ = true;
    }
    const double previous_percent = drag_edge_inset_percent_;
    if (std::fabs(previous_percent - drag_edge_inset_percent_) > 1e-6) {
        drag_moved_ = true;
    }

    const bool snapped = snap_dragged_assets_to_grid();
    if (changed || snapped) {
        refresh_spatial_entries_for_dragged_assets();
    }

    update_spawn_json_during_drag();
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
        double dir_len = std::hypot(static_cast<double>(reference_direction.x), static_cast<double>(reference_direction.y));
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

    double projected = dx_mouse * static_cast<double>(reference_direction.x) + dy_mouse * static_cast<double>(reference_direction.y);
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
            if (dx != 0.0 || dy != 0.0) {
                dir.x = static_cast<float>(dx / std::hypot(dx, dy));
                dir.y = static_cast<float>(dy / std::hypot(dx, dy));
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

    const bool snapped = snap_dragged_assets_to_grid();
    if (assets_changed || snapped) {
        refresh_spatial_entries_for_dragged_assets();
    }

    update_spawn_json_during_drag();
}

void RoomEditor::update_spawn_json_during_drag() {

    if (drag_spawn_id_.empty() || drag_states_.empty()) {
        return;
    }

    if (!spawn_group_panel_ || !spawn_group_panel_->is_visible()) {
        return;
    }

    SpawnEntryResolution resolved = locate_spawn_entry(drag_spawn_id_);
    nlohmann::json* entry = resolved.entry;
    if (!entry) {
        return;
    }

    Asset* primary = selected_assets_.empty() ? nullptr : selected_assets_.front();
    if (!primary) {
        return;
    }

    SDL_Point center = get_room_center();
    auto [width, height] = get_room_dimensions();

    switch (drag_mode_) {
        case DragMode::Exact:
            update_exact_json(*entry, *primary, center, width, height);
            break;

        case DragMode::Percent:
            update_percent_json(*entry, *primary, center, width, height);
            break;

        case DragMode::Perimeter:
        case DragMode::PerimeterCenter: {
            const int curr_w = std::max(1, drag_perimeter_curr_w_ > 0 ? drag_perimeter_curr_w_ : width);
            const int curr_h = std::max(1, drag_perimeter_curr_h_ > 0 ? drag_perimeter_curr_h_ : height);
            const int orig_w = std::max(1, drag_perimeter_orig_w_ > 0 ? drag_perimeter_orig_w_ : curr_w);
            const int orig_h = std::max(1, drag_perimeter_orig_h_ > 0 ? drag_perimeter_orig_h_ : curr_h);
            SDL_Point stored = RelativeRoomPosition::ToOriginal(drag_perimeter_center_offset_world_, orig_w, orig_h, curr_w, curr_h);
            const double dist = std::hypot(static_cast<double>(primary->pos.x - drag_perimeter_circle_center_.x), static_cast<double>(primary->pos.y - drag_perimeter_circle_center_.y));
            const int radius = static_cast<int>(std::lround(dist));
            save_perimeter_json(*entry, stored.x, stored.y, orig_w, orig_h, radius);
            break;
        }

        case DragMode::Edge: {
            int inset = static_cast<int>(std::lround(drag_edge_inset_percent_));
            inset = std::clamp(inset, 0, 200);
            save_edge_json(*entry, inset);
            break;
        }

        default:
            break;
    }

    if (spawn_group_panel_) {
        spawn_group_panel_->rebuild_rows();
    }
}

bool RoomEditor::snap_dragged_assets_to_grid() {
    if (drag_states_.empty()) return false;
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
        sync_dragged_assets_immediately();
    }
    return changed;
}

void RoomEditor::finalize_drag_session() {

    if (shared_footer_bar_ && overlay_resolution_before_drag_.has_value()) {
        shared_footer_bar_->set_grid_resolution(*overlay_resolution_before_drag_);
        overlay_resolution_before_drag_.reset();
    }

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
        SpawnEntryResolution resolved = locate_spawn_entry(drag_spawn_id_);
        nlohmann::json* entry = resolved.entry;
        if (entry) {
            bool request_respawn = false;
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

                        request_respawn = true;
                    }
                    break;
                default:
                    break;
            }

            if (drag_moved_) {
                const int snap_after_drag = current_grid_resolution();
                if (snap_after_drag > 0) {
                    (*entry)["resolution"] = snap_after_drag;
                    for (auto& st : drag_states_) {
                        if (st.asset) {
                            st.asset->grid_resolution = snap_after_drag;
                        }
                    }
                }
            }

            if (json_modified) {
                if (resolved.source == SpawnEntryResolution::Source::Room) {
                    save_current_room_assets_json();
                    if (request_respawn) {
                        respawn_spawn_group(*entry);
                    }
                } else if (resolved.source == SpawnEntryResolution::Source::Map) {
                    if (assets_) {
                        assets_->persist_map_info_json();
                        assets_->notify_spawn_group_config_changed(*entry);
                    }
                }
            }
        }
    }

    if (json_modified) {
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

    overlay_resolution_before_drag_.reset();
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

int RoomEditor::current_grid_resolution() const {
    if (shared_footer_bar_) {
        return vibble::grid::clamp_resolution(shared_footer_bar_->grid_resolution());
    }
    MapGridSettings settings = current_room_ ? current_room_->map_grid_settings() : MapGridSettings::defaults();
    settings.clamp();
    return vibble::grid::clamp_resolution(settings.resolution);
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
        bool sanitized = false;
        if (entry.is_object()) {
            const std::string id = entry.value("spawn_id", std::string{});
            SpawnEntryResolution current = locate_spawn_entry(id);
            if (current.owner_array) {
                sanitized = sanitize_perimeter_spawn_groups(*current.owner_array);
            }
        }
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

    const bool boundary_asset = primary && primary->info && primary->info->type == asset_types::boundary;
    SpawnEntryResolution resolved = locate_spawn_entry(spawn_id);
    auto owner_matches_section = [&](const char* section_key) -> bool {
        if (resolved.source != SpawnEntryResolution::Source::Map) {
            return false;
        }
        if (!resolved.owner_array || !assets_) {
            return false;
        }
        nlohmann::json& map_info = assets_->map_info_json();
        if (!map_info.is_object()) {
            return false;
        }
        auto section_it = map_info.find(section_key);
        if (section_it == map_info.end() || !section_it->is_object()) {
            return false;
        }
        auto groups_it = section_it->find("spawn_groups");
        if (groups_it == section_it->end() || !groups_it->is_array()) {
            return false;
        }
        return &(*groups_it) == resolved.owner_array;
};

    const bool map_assets_entry = owner_matches_section("map_assets_data");
    const bool boundary_entry   = owner_matches_section("map_boundary_data");

    auto close_spawn_group_panel = [&]() {
        if (spawn_group_panel_) {
            spawn_group_panel_->close();
            spawn_group_panel_->set_visible(false);
        }
};

    auto close_room_config_preserving_selection = [this]() {
        if (!room_config_dock_open_) {
            return;
        }
        suppress_room_config_selection_clear_ = true;
        set_room_config_visible(false);
        suppress_room_config_selection_clear_ = false;
};

    if (boundary_entry || boundary_asset) {
        close_spawn_group_panel();
        clear_active_spawn_group_target();
        close_room_config_preserving_selection();
        if (open_boundary_assets_panel_callback_) {
            open_boundary_assets_panel_callback_();
        }
        return;
    }

    if (map_assets_entry) {
        close_spawn_group_panel();
        clear_active_spawn_group_target();
        close_room_config_preserving_selection();
        if (open_map_assets_panel_callback_) {
            open_map_assets_panel_callback_();
        }
        return;
    }

    active_spawn_group_id_ = spawn_id;

    bool focused = false;
    if (room_cfg_ui_ && room_config_dock_open_) {
        focused = room_cfg_ui_->focus_spawn_group(spawn_id);
    }

    if (focused && spawn_group_panel_) {
        spawn_group_panel_->close();
        spawn_group_panel_->set_visible(false);
    }
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
    bool resolve_geometry = entry->value("resolve_geometry_to_room_size", true);
    int orig_w = std::max(1, entry->value("origional_width", room_w));
    int orig_h = std::max(1, entry->value("origional_height", room_h));
    if (!resolve_geometry) {
        orig_w = std::max(1, room_w);
        orig_h = std::max(1, room_h);
    }
    int stored_dx = entry->value("dx", 0);
    int stored_dy = entry->value("dy", 0);
    RelativeRoomPosition relative(SDL_Point{stored_dx, stored_dy}, orig_w, orig_h);
    SDL_Point scaled = relative.scaled_offset(room_w, room_h);
    overlay.center.x += scaled.x;
    overlay.center.y += scaled.y;
    int base_radius = entry->value("radius", 0);
    if (resolve_geometry) {
        const double width_ratio = static_cast<double>(std::max(1, room_w)) / static_cast<double>(std::max(1, orig_w));
        const double height_ratio = static_cast<double>(std::max(1, room_h)) / static_cast<double>(std::max(1, orig_h));
        const double ratio = (width_ratio + height_ratio) * 0.5;
        overlay.radius = static_cast<double>(base_radius) * ratio;
    } else {
        overlay.radius = static_cast<double>(base_radius);
    }
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

std::optional<std::vector<SDL_Point>> RoomEditor::compute_edge_path_for_drag() {
    if (!dragging_) return std::nullopt;
    if (drag_mode_ != DragMode::Edge) return std::nullopt;
    const Area* area = drag_edge_area_ ? drag_edge_area_ : (current_room_ ? current_room_->room_area.get() : nullptr);
    if (!area) return std::nullopt;
    SDL_Point center = drag_edge_center_;
    int inset = static_cast<int>(std::lround(drag_edge_inset_percent_));
    inset = std::clamp(inset, 0, 200);
    const auto& pts = area->get_points();
    if (pts.size() < 2) return std::nullopt;
    const double scale = std::clamp(static_cast<double>(inset) / 100.0, 0.0, 2.0);
    std::vector<SDL_Point> path;
    path.reserve(pts.size() + 1);
    for (const auto& p : pts) {
        const double vx = static_cast<double>(p.x - center.x);
        const double vy = static_cast<double>(p.y - center.y);
        SDL_Point q{ static_cast<int>(std::lround(center.x + vx * scale)),
                     static_cast<int>(std::lround(center.y + vy * scale)) };
        path.push_back(q);
    }
    if (!path.empty()) path.push_back(path.front());
    return path;
}

std::optional<std::vector<SDL_Point>> RoomEditor::compute_edge_path_for_spawn(const std::string& spawn_id) {
    if (spawn_id.empty() || !current_room_) return std::nullopt;
    nlohmann::json* entry = find_spawn_entry(spawn_id);
    if (!entry) return std::nullopt;
    std::string method = entry->value("position", std::string{});
    if (method == "Exact Position") method = "Exact";
    if (method != "Edge") return std::nullopt;
    const Area* area = find_edge_area_for_entry(*entry);
    if (!area) return std::nullopt;
    SDL_Point center = area->get_center();
    int inset = std::clamp(entry->value("edge_inset_percent", 100), 0, 200);
    const auto& pts = area->get_points();
    if (pts.size() < 2) return std::nullopt;
    const double scale = std::clamp(static_cast<double>(inset) / 100.0, 0.0, 2.0);
    std::vector<SDL_Point> path;
    path.reserve(pts.size() + 1);
    for (const auto& p : pts) {
        const double vx = static_cast<double>(p.x - center.x);
        const double vy = static_cast<double>(p.y - center.y);
        SDL_Point q{ static_cast<int>(std::lround(center.x + vx * scale)),
                     static_cast<int>(std::lround(center.y + vy * scale)) };
        path.push_back(q);
    }
    if (!path.empty()) path.push_back(path.front());
    return path;
}

void RoomEditor::add_spawn_group_internal() {
    if (!current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    nlohmann::json entry;
    const std::string new_spawn_id = generate_spawn_id();
    entry["spawn_id"] = new_spawn_id;
    const int add_default_resolution = current_grid_resolution();
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

bool RoomEditor::delete_spawn_group_internal(const std::string& spawn_id) {
    if (!remove_spawn_group_by_id(spawn_id)) {
        return false;
    }
    save_current_room_assets_json();
    if (assets_) {
        assets_->notify_spawn_group_removed(spawn_id);
    }
    if (active_spawn_group_id_ && *active_spawn_group_id_ == spawn_id) {
        clear_active_spawn_group_target();
    }
    rebuild_room_spawn_id_cache();
    refresh_spawn_group_config_ui();
    reopen_room_configurator();
    if (assets_) {
        assets_->refresh_active_asset_lists();
        mark_highlight_dirty();
    }
    return true;
}

bool RoomEditor::remove_spawn_group_by_id(const std::string& spawn_id) {
    if (spawn_id.empty() || !current_room_) return false;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    if (!arr.is_array() || arr.size() <= 1) return false;
    auto it = std::find_if(arr.begin(), arr.end(), [&spawn_id](const nlohmann::json& e) {
        if (!e.is_object()) return false;
        if (!e.contains("spawn_id") || !e["spawn_id"].is_string()) return false;
        return e["spawn_id"].get<std::string>() == spawn_id;
    });
    if (it == arr.end()) {
        return false;
    }
    arr.erase(it);
    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i].is_object()) {
            arr[i]["priority"] = static_cast<int>(i);
        }
    }
    return true;
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

    set_room_config_visible(true);
    if (room_cfg_ui_) {
        room_cfg_ui_->focus_spawn_group(spawn_id);
    }

    if (spawn_group_panel_) {
        spawn_group_panel_->close();
        spawn_group_panel_->set_visible(false);
    }
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
        raw = assets_->world_grid().create_asset_at_point(std::move(uptr));
        if (raw) {
            assets_->all.push_back(raw);
        }
    }
    assets_->initialize_active_assets(assets_->getView().get_screen_center());
    assets_->refresh_active_asset_lists();
    mark_spatial_index_dirty();
    spawned.clear();
    mark_highlight_dirty();
}

void RoomEditor::regenerate_current_room() {
    if (!assets_ || !current_room_ || !current_room_->room_area) {
        return;
    }

    auto& root = current_room_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    std::vector<nlohmann::json> entries;
    entries.reserve(groups.size());
    for (const auto& entry : groups) {
        if (entry.is_object()) {
            entries.push_back(entry);
        }
    }

    for (const auto& entry : entries) {
        respawn_spawn_group(entry);
    }

    rebuild_room_spawn_id_cache();
    save_current_room_assets_json();
}

Asset* RoomEditor::find_asset_spawn_owner(const std::string& spawn_id) const {
    if (spawn_id.empty() || !assets_) {
        return nullptr;
    }

    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) {
            continue;
        }
        if (!asset_belongs_to_room(asset)) {
            continue;
        }
        for (Asset* child : asset->asset_children) {
            if (!child || child->dead) {
                continue;
            }
            if (child->spawn_id == spawn_id) {
                return asset;
            }
        }
    }
    return nullptr;
}

void RoomEditor::respawn_asset_child_spawn_group(Asset* , const nlohmann::json& ) {

}

void RoomEditor::respawn_spawn_group(const nlohmann::json& entry) {
    if (!assets_ || !current_room_ || !current_room_->room_area) return;
    if (!entry.is_object()) return;
    std::string spawn_id = entry.value("spawn_id", std::string{});
    if (spawn_id.empty()) return;

    if (Asset* owner = find_asset_spawn_owner(spawn_id)) {
        respawn_asset_child_spawn_group(owner, entry);
        return;
    }

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
        if (asset) {
            asset->Delete();
            (void)assets_->world_grid().remove_asset(asset);
        }
    }
    assets_->rebuild_from_grid_state();
    assets_->refresh_active_asset_lists();

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
    int spawn_resolution = occupancy ? occupancy->resolution() : grid_service.default_resolution();
    checker.begin_session(grid_service, spawn_resolution);
    SpawnContext ctx(rng, checker, exclusion, asset_info_library, spawned, &assets_->library(), grid_service, occupancy.get());
    if (current_room_) {
        ctx.set_map_grid_settings(current_room_->map_grid_settings());
    }
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
    checker.reset_session();

    const Area* old_area_copy = current_room_->room_area.get();
    const double old_area_size = old_area_copy ? old_area_copy->get_size() : 0.0;
    const double new_area_size = old_area_size;

    if (old_area_copy && new_area_size < old_area_size) {
        nlohmann::json& map_info_json = assets_->map_info_json();
        std::vector<std::pair<std::string, int>> boundary_options;
        int boundary_spacing = 100;
        if (map_info_json.contains("map_boundary_data") && map_info_json["map_boundary_data"].is_object()) {
            const auto& boundary_json = map_info_json["map_boundary_data"];
            if (boundary_json.contains("batch_assets")) {
                const auto& batch = boundary_json["batch_assets"];
                boundary_spacing = (batch.value("grid_spacing_min", boundary_spacing) + batch.value("grid_spacing_max", boundary_spacing)) / 2;
                for (const auto& asset_entry : batch.value("batch_assets", std::vector<nlohmann::json>{})) {
                    if (asset_entry.contains("name") && asset_entry["name"].is_string()) {
                        int weight = asset_entry.value("percent", 1);
                        boundary_options.emplace_back(asset_entry["name"].get<std::string>(), weight);
                    }
                }
            }
        }

        if (!boundary_options.empty()) {
            const int boundary_resolution = std::clamp( static_cast<int>(std::lround(std::log2(static_cast<double>(std::max(1, boundary_spacing))))), 0, vibble::grid::kMaxResolution);
            vibble::grid::Grid& grid_service = vibble::grid::global_grid();
            vibble::grid::Occupancy boundary_grid(*old_area_copy, boundary_resolution, grid_service);
            auto vertices = boundary_grid.vertices_in_area(*old_area_copy);
            if (!vertices.empty()) {
                std::vector<int> weights;
                weights.reserve(boundary_options.size());
                for (const auto& opt : boundary_options) {
                    weights.push_back(std::max(1, opt.second));
                }
                std::discrete_distribution<int> pick(weights.begin(), weights.end());
                std::mt19937 boundary_rng(std::random_device{}());
                std::vector<std::unique_ptr<Asset>> boundary_spawned;
                for (auto* vertex : vertices) {
                    if (!vertex) continue;
                    if (current_room_->room_area->contains_point(vertex->world)) continue;
                    int idx = pick(boundary_rng);
                    const std::string& asset_name = boundary_options[idx].first;
                    auto info = assets_->library().get(asset_name);
                    if (!info) continue;
                    std::string spawn_id = generate_spawn_id();
                    Area spawn_area(asset_name, vertex->world, 1, 1, "Point", 1, 1, 1);
                    auto asset = std::make_unique<Asset>(info, spawn_area, vertex->world, 0, nullptr, spawn_id, std::string(asset_types::boundary));
                    boundary_spawned.push_back(std::move(asset));
                }
                integrate_spawned_assets(boundary_spawned);
            }
        }
    }

    std::string player_asset_name;
    if (assets_) {
        if (assets_->player && assets_->player->info) {
            player_asset_name = assets_->player->info->name;
        }
        if (player_asset_name.empty()) {
            for (const auto& pair : assets_->library().all()) {
                if (!pair.second) continue;
                if (pair.second->type == asset_types::player) {
                    player_asset_name = pair.second->name;
                    break;
                }
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
        std::mt19937 regen_rng(std::random_device{}());
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
}

void RoomEditor::save_perimeter_json(nlohmann::json& entry, int dx, int dy, int orig_w, int orig_h, int radius) {
    entry["dx"] = dx;
    entry["dy"] = dy;
    entry["origional_width"] = orig_w;
    entry["origional_height"] = orig_h;
    entry["radius"] = radius;
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
bool RoomEditor::spawn_group_locked(const std::string& spawn_id) const {
    if (spawn_id.empty()) return false;

    RoomEditor* self = const_cast<RoomEditor*>(this);
    SpawnEntryResolution resolved = self->locate_spawn_entry(spawn_id);
    if (!resolved.valid() || !resolved.entry) return false;
    try {
        const auto& e = *resolved.entry;
        if (e.is_object() && e.contains("locked") && e["locked"].is_boolean()) {
            return e["locked"].get<bool>();
        }
    } catch (...) {}
    return false;
}
