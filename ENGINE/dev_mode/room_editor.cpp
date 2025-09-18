#include "room_editor.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "dev_mode/asset_info_ui.hpp"
#include "dev_mode/asset_library_ui.hpp"
#include "dev_mode/assets_config.hpp"
#include "dev_mode/room_configurator.hpp"
#include "dev_mode/widgets.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/check.hpp"
#include "spawn/methods/center_spawner.hpp"
#include "spawn/methods/exact_spawner.hpp"
#include "spawn/methods/perimeter_spawner.hpp"
#include "spawn/methods/percent_spawner.hpp"
#include "spawn/methods/random_spawner.hpp"
#include "spawn/spawn_context.hpp"
#include "spawn/spawn_logger.hpp"
#include "utils/input.hpp"
#include "utils/map_grid.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <cctype>
#include <limits>
#include <random>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {
std::string generate_room_spawn_id() {
    static std::mt19937 rng(std::random_device{}());
    static const char* hex = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s = "spn-";
    for (int i = 0; i < 12; ++i) s.push_back(hex[dist(rng)]);
    return s;
}

nlohmann::json& ensure_spawn_groups_array(nlohmann::json& root) {
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return root["spawn_groups"];
    }
    if (root.contains("assets") && root["assets"].is_array()) {
        root["spawn_groups"] = root["assets"];
        root.erase("assets");
        return root["spawn_groups"];
    }
    root["spawn_groups"] = nlohmann::json::array();
    return root["spawn_groups"];
}

const nlohmann::json* find_spawn_groups_array(const nlohmann::json& root) {
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return &root["spawn_groups"];
    }
    if (root.contains("assets") && root["assets"].is_array()) {
        return &root["assets"];
    }
    return nullptr;
}
}

RoomEditor::RoomEditor(Assets* owner, int screen_w, int screen_h)
    : assets_(owner), screen_w_(screen_w), screen_h_(screen_h) {
    regenerate_button_ = std::make_unique<DMButton>("Regenerate Current Room", &DMStyles::CreateButton(), 240, DMButton::height());
    position_regenerate_button();
}

RoomEditor::~RoomEditor() = default;

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
    position_regenerate_button();
}

void RoomEditor::set_current_room(Room* room) {
    const bool room_changed = (room != current_room_);

    current_room_ = room;
    if (current_room_) {
        ensure_spawn_groups_array(current_room_->assets_data());
    }

    if (enabled_ && room_changed && current_room_) {
        focus_camera_on_room_center();
    }
}

void RoomEditor::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!assets_) return;

    camera& cam = assets_->getView();
    if (enabled_) {
        cam.set_parallax_enabled(false);
        cam.set_manual_zoom_override(false);
        close_asset_info_editor();
        focus_camera_on_room_center();
    } else {
        cam.set_parallax_enabled(true);
        cam.set_manual_zoom_override(false);
        cam.clear_focus_override();
        if (library_ui_) library_ui_->close();
        if (room_cfg_ui_) room_cfg_ui_->close();
        if (info_ui_) info_ui_->close();
        if (assets_cfg_ui_) assets_cfg_ui_->close_all_asset_configs();
        if (area_editor_) area_editor_->cancel();
        clear_selection();
        reset_click_state();
        reopen_info_after_area_edit_ = false;
        info_for_reopen_.reset();
        last_area_editor_active_ = false;
    }

    if (input_) input_->clearClickBuffer();
}

void RoomEditor::update(const Input& input) {
    handle_shortcuts(input);

    if (!enabled_) return;
    if (!input_ || !active_assets_) return;

    handle_delete_shortcut(input);

    const int mx = input.getX();
    const int my = input.getY();

    if (!is_ui_blocking_input(mx, my)) {
        handle_mouse_input(input);
    }
}

void RoomEditor::update_ui(const Input& input) {
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->update(input, screen_w_, screen_h_, assets_->library(), *assets_);
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible()) {
        room_cfg_ui_->update(input);
    }

    ensure_area_editor();
    if (area_editor_) {
        const bool was = last_area_editor_active_;
        const bool now = area_editor_->is_active();
        if (now) {
            area_editor_->update(input, screen_w_, screen_h_);
        }
        if (was && !now) {
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
    }
    if (assets_cfg_ui_) {
        assets_cfg_ui_->update(input);
    }

    update_area_editor_focus();
}

void RoomEditor::handle_sdl_event(const SDL_Event& event) {
    if (auto* dropdown = DMDropdown::active_dropdown()) {
        dropdown->handle_event(event);
        return;
    }

    ensure_area_editor();
    if (area_editor_ && area_editor_->is_active()) {
        if (area_editor_->handle_event(event)) return;
    }

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

    bool handled = false;
    if (!handled && info_ui_ && info_ui_->is_visible() && info_ui_->is_point_inside(mx, my)) {
        info_ui_->handle_event(event);
        handled = true;
    }
    if (!handled && assets_cfg_ui_ && assets_cfg_ui_->any_visible() && assets_cfg_ui_->is_point_inside(mx, my)) {
        assets_cfg_ui_->handle_event(event);
        handled = true;
    }
    if (!handled && room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(mx, my)) {
        room_cfg_ui_->handle_event(event);
        handled = true;
    }
    if (!handled && library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking_at(mx, my)) {
        library_ui_->handle_event(event);
        handled = true;
    }

    if (!handled) {
        if (info_ui_ && info_ui_->is_visible()) {
            info_ui_->handle_event(event);
        } else if (assets_cfg_ui_ && assets_cfg_ui_->any_visible()) {
            assets_cfg_ui_->handle_event(event);
        } else if (room_cfg_ui_ && room_cfg_ui_->any_panel_visible()) {
            room_cfg_ui_->handle_event(event);
        } else if (library_ui_ && library_ui_->is_visible()) {
            library_ui_->handle_event(event);
        }
    }

    if (enabled_ && regenerate_button_) {
        regenerate_button_->set_rect(regenerate_button_rect_);
        bool clicked = regenerate_button_->handle_event(event);
        if (clicked && event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            regenerate_current_room();
            if (input_) input_->clearClickBuffer();
        }
    }

    if (handled && input_) {
        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            input_->clearClickBuffer();
        }
    }
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
    if (assets_cfg_ui_) {
        assets_cfg_ui_->render(renderer);
    }
    if (room_cfg_ui_ && room_cfg_ui_->any_panel_visible()) {
        room_cfg_ui_->render(renderer);
    }
    if (regenerate_button_ && enabled_) {
        regenerate_button_->set_rect(regenerate_button_rect_);
        regenerate_button_->render(renderer);
    }
    DMDropdown::render_active_options(renderer);
}

void RoomEditor::toggle_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    library_ui_->toggle();
}

void RoomEditor::open_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    library_ui_->open();
}

void RoomEditor::close_asset_library() {
    if (library_ui_) library_ui_->close();
}

bool RoomEditor::is_asset_library_open() const {
    return library_ui_ && library_ui_->is_visible();
}

std::shared_ptr<AssetInfo> RoomEditor::consume_selected_asset_from_library() {
    if (!library_ui_) return nullptr;
    return library_ui_->consume_selection();
}

void RoomEditor::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!info) return;
    if (library_ui_) library_ui_->close();
    if (assets_cfg_ui_) assets_cfg_ui_->close_all_asset_configs();
    if (!info_ui_) info_ui_ = std::make_unique<AssetInfoUI>();
    if (info_ui_) info_ui_->set_assets(assets_);
    if (info_ui_) {
        info_ui_->clear_info();
        info_ui_->set_info(info);
        info_ui_->set_target_asset(nullptr);
        info_ui_->open();
    }
}

void RoomEditor::open_asset_info_editor_for_asset(Asset* asset) {
    if (!asset || !asset->info) return;
    std::cout << "Opening AssetInfoUI for asset: " << asset->info->name << std::endl;
    clear_selection();
    focus_camera_on_asset(asset, 0.8, 20);
    open_asset_info_editor(asset->info);
    if (info_ui_) info_ui_->set_target_asset(asset);
}

void RoomEditor::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
}

bool RoomEditor::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

void RoomEditor::open_asset_config_for_asset(Asset* asset) {
    if (!asset) return;
    if (!assets_cfg_ui_) {
        assets_cfg_ui_ = std::make_unique<AssetsConfig>();
        if (current_room_) {
            auto& assets_json = ensure_spawn_groups_array(current_room_->assets_data());
            auto on_change = [this]() {
                if (current_room_) current_room_->save_assets_json();
            };
            auto on_entry = [this](const nlohmann::json& entry, const AssetConfigUI::ChangeSummary& summary) {
                handle_spawn_config_change(entry, summary);
            };
            assets_cfg_ui_->load(assets_json, std::move(on_change), std::move(on_entry));
        }
    }
    SDL_Point scr = assets_->getView().map_to_screen({asset->pos.x, asset->pos.y});
    std::string id = asset->spawn_id.empty() ? (asset->info ? asset->info->name : std::string{}) : asset->spawn_id;
    assets_cfg_ui_->open_asset_config(id, scr.x, scr.y);
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

    std::string spawn_id = generate_room_spawn_id();
    nlohmann::json entry;
    entry["spawn_id"] = spawn_id;
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["position"] = "Exact";
    entry["check_overlap"] = false;
    entry["enforce_spacing"] = false;
    entry["dx"] = asset->pos.x - center.x;
    entry["dy"] = asset->pos.y - center.y;
    if (width > 0) entry["origional_width"] = width;
    if (height > 0) entry["origional_height"] = height;
    entry["display_name"] = info->name;

    entry["candidates"] = nlohmann::json::array();
    entry["candidates"].push_back({{"name", "null"}, {"chance", 0}});
    entry["candidates"].push_back({{"name", info->name}, {"chance", 100}});

    arr.push_back(entry);
    current_room_->save_assets_json();
    asset->spawn_id = spawn_id;
    asset->spawn_method = "Exact";
    if (assets_cfg_ui_) {
        auto on_change = [this]() {
            if (current_room_) current_room_->save_assets_json();
        };
        auto on_entry = [this](const nlohmann::json& e, const AssetConfigUI::ChangeSummary& summary) {
            handle_spawn_config_change(e, summary);
        };
        assets_cfg_ui_->load(arr, std::move(on_change), std::move(on_entry));
    }
}

void RoomEditor::toggle_room_config() {
    if (!room_cfg_ui_) room_cfg_ui_ = std::make_unique<RoomConfigurator>();
    if (room_cfg_ui_->visible()) {
        room_cfg_ui_->close();
    } else {
        room_cfg_ui_->open(current_room_);
        room_cfg_ui_->set_position(10, 10);
    }
}

void RoomEditor::close_room_config() {
    if (room_cfg_ui_) room_cfg_ui_->close();
}

bool RoomEditor::is_room_config_open() const {
    return room_cfg_ui_ && room_cfg_ui_->visible();
}

void RoomEditor::begin_area_edit_for_selected_asset(const std::string& area_name) {
    ensure_area_editor();
    if (!area_editor_) return;

    Asset* target = nullptr;
    if (!selected_assets_.empty()) target = selected_assets_.front();
    if (!target) target = hovered_asset_;
    if (!target || !target->info) return;

    if (info_ui_ && info_ui_->is_visible()) {
        reopen_info_after_area_edit_ = true;
        info_for_reopen_ = target->info;
        info_ui_->close();
    } else {
        reopen_info_after_area_edit_ = false;
        info_for_reopen_.reset();
    }

    focus_camera_on_asset(target, 0.8, 20);
    area_editor_->begin(target->info.get(), target, area_name);
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
        cam.zoom_to_area(*current_room_->room_area, 25);
    }
}

void RoomEditor::reset_click_state() {
    click_buffer_frames_ = 0;
    rclick_buffer_frames_ = 0;
    last_click_time_ms_ = 0;
    last_click_asset_ = nullptr;
    reset_drag_state();
}

void RoomEditor::clear_selection() {
    selected_assets_.clear();
    highlighted_assets_.clear();
    hovered_asset_ = nullptr;
    reset_drag_state();
    if (!active_assets_) return;
    for (Asset* asset : *active_assets_) {
        if (!asset) continue;
        asset->set_selected(false);
        asset->set_highlighted(false);
    }
}

void RoomEditor::purge_asset(Asset* asset) {
    if (!asset) return;
    if (hovered_asset_ == asset) hovered_asset_ = nullptr;
    if (last_click_asset_ == asset) {
        last_click_asset_ = nullptr;
        last_click_time_ms_ = 0;
    }
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
}

void RoomEditor::set_zoom_scale_factor(double factor) {
    zoom_scale_factor_ = (factor > 0.0) ? factor : 1.0;
    pan_zoom_.set_zoom_scale_factor(zoom_scale_factor_);
}

void RoomEditor::handle_mouse_input(const Input& input) {
    camera& cam = assets_->getView();

    if (input.isScancodeDown(SDL_SCANCODE_ESCAPE)) {
        clear_selection();
        return;
    }

    if (!input_) return;

    const int mx = input_->getX();
    const int my = input_->getY();
    const bool ui_blocked = is_ui_blocking_input(mx, my);

    Asset* hit_asset = nullptr;
    if (!ui_blocked) {
        hit_asset = hit_test_asset(SDL_Point{mx, my});
    }

    pan_zoom_.handle_input(cam, input, ui_blocked || hit_asset != nullptr);

    SDL_Point world_mouse = cam.screen_to_map(SDL_Point{mx, my});

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

    handle_click(input);
    update_highlighted_assets();
}

Asset* RoomEditor::hit_test_asset(SDL_Point screen_point) const {
    if (!active_assets_ || !assets_) return nullptr;

    const camera& cam = assets_->getView();
    const float scale = std::max(0.0001f, cam.get_scale());
    const float inv_scale = 1.0f / scale;

    Asset* best = nullptr;
    int best_screen_y = std::numeric_limits<int>::min();
    int best_z_index = std::numeric_limits<int>::min();

    for (Asset* asset : *active_assets_) {
        if (!asset || !asset->info) continue;
        const std::string& type = asset->info->type;
        if (type == "Boundary" || type == "boundary" || type == "Texture") continue;

        SDL_Texture* tex = asset->get_final_texture();
        int fw = asset->cached_w;
        int fh = asset->cached_h;
        if ((fw == 0 || fh == 0) && tex) {
            SDL_QueryTexture(tex, nullptr, nullptr, &fw, &fh);
        }
        if (fw <= 0 || fh <= 0) continue;

        const SDL_Point center = cam.map_to_screen(SDL_Point{asset->pos.x, asset->pos.y});
        const int sw = static_cast<int>(std::lround(static_cast<double>(fw) * inv_scale));
        const int sh = static_cast<int>(std::lround(static_cast<double>(fh) * inv_scale));
        if (sw <= 0 || sh <= 0) continue;

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

    if (input_->wasClicked(Input::RIGHT)) {
        if (rclick_buffer_frames_ > 0) {
            --rclick_buffer_frames_;
            return;
        }
        rclick_buffer_frames_ = 2;
        if (hovered_asset_) {
            open_asset_info_editor_for_asset(hovered_asset_);
        }
        return;
    } else {
        rclick_buffer_frames_ = 0;
    }

    if (!input_->wasClicked(Input::LEFT)) {
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
        selected_assets_.clear();
        bool select_group = true;
        const std::string& method = nearest->spawn_method;
        if (method == "Exact" || method == "Exact Position" || method == "Percent") {
            select_group = false;
        }
        if (select_group && !nearest->spawn_id.empty() && active_assets_) {
            for (Asset* asset : *active_assets_) {
                if (asset && asset->spawn_id == nearest->spawn_id) {
                    selected_assets_.push_back(asset);
                }
            }
        } else {
            selected_assets_.push_back(nearest);
        }
        open_asset_config_for_asset(nearest);

        Uint32 now = SDL_GetTicks();
        if (last_click_asset_ == nearest && (now - last_click_time_ms_) <= 300) {
            last_click_time_ms_ = 0;
            last_click_asset_ = nullptr;
        } else {
            last_click_time_ms_ = now;
            last_click_asset_ = nearest;
        }
    } else {
        selected_assets_.clear();
        last_click_asset_ = nullptr;
        last_click_time_ms_ = 0;
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
            if (!asset) continue;
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
    if (info_ui_ && info_ui_->is_visible() && info_ui_->is_point_inside(mx, my)) {
        return true;
    }
    if (room_cfg_ui_ && room_cfg_ui_->visible() && room_cfg_ui_->is_point_inside(mx, my)) {
        return true;
    }
    if (library_ui_ && library_ui_->is_visible() && library_ui_->is_input_blocking_at(mx, my)) {
        return true;
    }
    if (area_editor_ && area_editor_->is_active()) {
        return true;
    }
    if (assets_cfg_ui_ && assets_cfg_ui_->any_visible() && assets_cfg_ui_->is_point_inside(mx, my)) {
        return true;
    }
    return false;
}

void RoomEditor::handle_shortcuts(const Input& input) {
    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (!ctrl) return;

    if (input.wasScancodePressed(SDL_SCANCODE_A)) {
        toggle_asset_library();
    }
    if (input.wasScancodePressed(SDL_SCANCODE_R)) {
        toggle_room_config();
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
        } else {
            focus_camera_on_room_center(false);
        }
    } else {
        focus_camera_on_room_center(false);
    }
}

void RoomEditor::ensure_area_editor() {
    if (!area_editor_) {
        area_editor_ = std::make_unique<AreaOverlayEditor>();
        if (area_editor_) area_editor_->attach_assets(assets_);
    }
}

void RoomEditor::handle_delete_shortcut(const Input& input) {
    if (!input.wasScancodePressed(SDL_SCANCODE_DELETE)) return;
    if (selected_assets_.empty() || !active_assets_ || !current_room_) return;

    Asset* primary = selected_assets_.front();
    if (!primary) return;
    const std::string& spawn_id = primary->spawn_id;
    if (spawn_id.empty()) return;

    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);

    auto it = std::remove_if(arr.begin(), arr.end(), [&](nlohmann::json& entry) {
        if (!entry.is_object()) return false;
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) return false;
        return entry["spawn_id"].get<std::string>() == spawn_id;
    });
    bool removed_json = (it != arr.end());
    arr.erase(it, arr.end());

    std::vector<Asset*> to_delete;
    for (Asset* asset : *active_assets_) {
        if (asset && asset->spawn_id == spawn_id) {
            to_delete.push_back(asset);
        }
    }
    for (Asset* asset : to_delete) {
        purge_asset(asset);
        if (asset) asset->Delete();
    }

    if (removed_json) {
        current_room_->save_assets_json();
        refresh_assets_config_ui();
    }

    clear_selection();
}

void RoomEditor::begin_drag_session(const SDL_Point& world_mouse, bool ctrl_modifier) {
    drag_mode_ = DragMode::None;
    drag_states_.clear();
    drag_spawn_id_.clear();
    drag_perimeter_base_radius_ = 0.0;
    drag_perimeter_start_offset_ = SDL_Point{0, 0};
    drag_moved_ = false;
    drag_room_center_ = get_room_center();
    drag_last_world_ = world_mouse;
    drag_anchor_asset_ = nullptr;

    if (selected_assets_.empty()) return;
    Asset* primary = selected_assets_.front();
    if (!primary) return;

    drag_anchor_asset_ = primary;
    drag_spawn_id_ = primary->spawn_id;

    const std::string& method = primary->spawn_method;
    if (method == "Exact" || method == "Exact Position") {
        drag_mode_ = DragMode::Exact;
    } else if (method == "Percent") {
        drag_mode_ = DragMode::Percent;
    } else if (method == "Perimeter") {
        drag_mode_ = ctrl_modifier ? DragMode::PerimeterCenter : DragMode::Perimeter;
    } else {
        drag_mode_ = DragMode::Free;
    }

    if (!drag_spawn_id_.empty()) {
        if (nlohmann::json* entry = find_spawn_entry(drag_spawn_id_)) {
            auto read_offset = [&](const char* single_key, const char* min_key, const char* max_key) -> int {
                if (entry->contains(single_key) && (*entry)[single_key].is_number_integer()) {
                    return (*entry)[single_key].get<int>();
                }
                if (entry->contains(min_key) && (*entry)[min_key].is_number_integer()) {
                    return (*entry)[min_key].get<int>();
                }
                if (entry->contains(max_key) && (*entry)[max_key].is_number_integer()) {
                    return (*entry)[max_key].get<int>();
                }
                return 0;
            };
            drag_perimeter_start_offset_.x = read_offset("perimeter_x_offset", "perimeter_x_offset_min", "perimeter_x_offset_max");
            drag_perimeter_start_offset_.y = read_offset("perimeter_y_offset", "perimeter_y_offset_min", "perimeter_y_offset_max");
        }
    }

    drag_states_.reserve(selected_assets_.size());
    for (Asset* asset : selected_assets_) {
        if (!asset) continue;
        DraggedAssetState state;
        state.asset = asset;
        state.start_pos = asset->pos;
        if (drag_mode_ == DragMode::Perimeter) {
            double dx = static_cast<double>(asset->pos.x - drag_room_center_.x);
            double dy = static_cast<double>(asset->pos.y - drag_room_center_.y);
            double len = std::hypot(dx, dy);
            state.start_distance = len;
            if (len > 1e-6) {
                state.direction.x = static_cast<float>(dx / len);
                state.direction.y = static_cast<float>(dy / len);
            } else {
                state.direction.x = 0.0f;
                state.direction.y = -1.0f;
            }
        }
        drag_states_.push_back(state);
    }

    if (drag_mode_ == DragMode::Perimeter) {
        double border_shift = 0.0;
        bool have_shift = false;
        if (!drag_spawn_id_.empty()) {
            if (nlohmann::json* entry = find_spawn_entry(drag_spawn_id_)) {
                if (entry->contains("percentage_shift_from_center") && (*entry)["percentage_shift_from_center"].is_number()) {
                    border_shift = (*entry)["percentage_shift_from_center"].get<double>();
                    have_shift = true;
                }
                if (!have_shift && entry->contains("border_shift_min") && (*entry)["border_shift_min"].is_number()) {
                    border_shift = (*entry)["border_shift_min"].get<double>();
                    have_shift = true;
                }
                if (!have_shift && entry->contains("border_shift_max") && (*entry)["border_shift_max"].is_number()) {
                    border_shift = (*entry)["border_shift_max"].get<double>();
                    have_shift = true;
                }
                if (!have_shift && entry->contains("border_shift") && (*entry)["border_shift"].is_number()) {
                    border_shift = (*entry)["border_shift"].get<double>();
                    have_shift = true;
                }
            }
        }
        double shift_ratio = 1.0 - border_shift / 100.0;
        if (shift_ratio <= 0.0) shift_ratio = 0.0001;
        double reference = 0.0;
        const DraggedAssetState* anchor_state = nullptr;
        for (const auto& state : drag_states_) {
            if (state.asset == drag_anchor_asset_) {
                anchor_state = &state;
                break;
            }
        }
        if (anchor_state) reference = anchor_state->start_distance;
        if (reference <= 0.0) {
            double dx = static_cast<double>(primary->pos.x - drag_room_center_.x);
            double dy = static_cast<double>(primary->pos.y - drag_room_center_.y);
            reference = std::hypot(dx, dy);
        }
        drag_perimeter_base_radius_ = reference / shift_ratio;
        if (!std::isfinite(drag_perimeter_base_radius_) || drag_perimeter_base_radius_ <= 0.0) {
            drag_perimeter_base_radius_ = reference;
        }
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

    double reference_length = ref->start_distance;
    SDL_FPoint dir = ref->direction;
    if (reference_length <= 1e-6) {
        double dx = static_cast<double>(ref->asset->pos.x - drag_room_center_.x);
        double dy = static_cast<double>(ref->asset->pos.y - drag_room_center_.y);
        reference_length = std::hypot(dx, dy);
        if (reference_length > 1e-6) {
            dir.x = static_cast<float>(dx / reference_length);
            dir.y = static_cast<float>(dy / reference_length);
        }
    }
    if (reference_length <= 1e-6) reference_length = 1.0;

    double target = (world_mouse.x - drag_room_center_.x) * dir.x +
                    (world_mouse.y - drag_room_center_.y) * dir.y;
    double new_length = std::max(0.0, target);
    double ratio = new_length / reference_length;
    if (!std::isfinite(ratio)) ratio = 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;

    bool changed = false;
    for (auto& state : drag_states_) {
        if (!state.asset) continue;
        double base = state.start_distance;
        SDL_FPoint state_dir = state.direction;
        if (base <= 0.0 || (state_dir.x == 0.0f && state_dir.y == 0.0f)) {
            double dx = static_cast<double>(state.asset->pos.x - drag_room_center_.x);
            double dy = static_cast<double>(state.asset->pos.y - drag_room_center_.y);
            double len = std::hypot(dx, dy);
            if (base <= 0.0) base = len;
            if (len > 1e-6) {
                state_dir.x = static_cast<float>(dx / len);
                state_dir.y = static_cast<float>(dy / len);
            }
        }
        double desired = base * ratio;
        int new_x = drag_room_center_.x + static_cast<int>(std::lround(state_dir.x * desired));
        int new_y = drag_room_center_.y + static_cast<int>(std::lround(state_dir.y * desired));
        if (state.asset->pos.x != new_x || state.asset->pos.y != new_y) {
            state.asset->pos.x = new_x;
            state.asset->pos.y = new_y;
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
                    if (drag_moved_ && drag_perimeter_base_radius_ > 0.0) {
                        double dist = std::hypot(static_cast<double>(primary->pos.x - center.x),
                                                 static_cast<double>(primary->pos.y - center.y));
                        double ratio = drag_perimeter_base_radius_ > 0.0 ? dist / drag_perimeter_base_radius_ : 1.0;
                        if (!std::isfinite(ratio)) ratio = 0.0;
                        if (ratio < 0.0) ratio = 0.0;
                        if (ratio > 1.0) ratio = 1.0;
                        double border_shift = (1.0 - ratio) * 100.0;
                        update_perimeter_border_json(*entry, border_shift);
                        json_modified = true;
                    }
                    break;
                case DragMode::PerimeterCenter:
                    if (drag_moved_) {
                        SDL_Point delta{0, 0};
                        const DraggedAssetState* anchor_state = nullptr;
                        for (const auto& state : drag_states_) {
                            if (state.asset == drag_anchor_asset_) {
                                anchor_state = &state;
                                break;
                            }
                        }
                        if (anchor_state && anchor_state->asset) {
                            delta.x = anchor_state->asset->pos.x - anchor_state->start_pos.x;
                            delta.y = anchor_state->asset->pos.y - anchor_state->start_pos.y;
                        }
                        if (delta.x != 0 || delta.y != 0) {
                            SDL_Point new_offset{drag_perimeter_start_offset_.x + delta.x,
                                                 drag_perimeter_start_offset_.y + delta.y};
                            update_perimeter_center_json(*entry, new_offset);
                            json_modified = true;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (json_modified && current_room_) {
        current_room_->save_assets_json();
        refresh_assets_config_ui();
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
    drag_perimeter_base_radius_ = 0.0;
    drag_perimeter_start_offset_ = SDL_Point{0, 0};
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

void RoomEditor::refresh_assets_config_ui() {
    if (!assets_cfg_ui_ || !current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = ensure_spawn_groups_array(root);
    assets_cfg_ui_->close_all_asset_configs();
    auto on_change = [this]() {
        if (current_room_) current_room_->save_assets_json();
    };
    auto on_entry = [this](const nlohmann::json& entry, const AssetConfigUI::ChangeSummary& summary) {
        handle_spawn_config_change(entry, summary);
    };
    assets_cfg_ui_->load(arr, std::move(on_change), std::move(on_entry));
}

void RoomEditor::position_regenerate_button() {
    if (!regenerate_button_) return;
    int button_w = regenerate_button_->rect().w;
    int button_h = regenerate_button_->rect().h;
    if (button_w <= 0) button_w = 240;
    if (button_h <= 0) button_h = DMButton::height();
    int x = 16;
    int y = screen_h_ - button_h - 16;
    if (y < 0) y = 0;
    regenerate_button_rect_ = SDL_Rect{x, y, button_w, button_h};
    regenerate_button_->set_rect(regenerate_button_rect_);
}

void RoomEditor::handle_spawn_config_change(const nlohmann::json& entry, const AssetConfigUI::ChangeSummary& summary) {
    if (!summary.method_changed && !summary.quantity_changed) return;
    bool respawn = summary.method_changed;
    if (!respawn && summary.quantity_changed) {
        const std::string& method = summary.method;
        if (method == "Random" || method == "Percent" || method == "Perimeter") {
            respawn = true;
        }
    }
    if (!respawn) return;
    respawn_spawn_group(entry);
}

std::unique_ptr<MapGrid> RoomEditor::build_room_grid(const std::string& ignore_spawn_id) const {
    if (!current_room_ || !current_room_->room_area) return nullptr;
    int spacing = 100;
    if (spacing <= 0) spacing = 100;
    auto grid = std::make_unique<MapGrid>(MapGrid::from_area_bounds(*current_room_->room_area, spacing));
    if (!assets_) return grid;
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) continue;
        if (!asset->spawn_id.empty() && asset->spawn_id == ignore_spawn_id) continue;
        SDL_Point pos{asset->pos.x, asset->pos.y};
        if (current_room_->room_area && !current_room_->room_area->contains_point(pos)) continue;
        grid->set_occupied_at(pos, true);
    }
    return grid;
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
        assets_->active_manager().activate(raw);
    }
    assets_->refresh_active_asset_lists();
    assets_->update_closest_assets(assets_->player, 3);
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

    auto grid = build_room_grid(spawn_id);

    nlohmann::json root;
    root["spawn_groups"] = nlohmann::json::array();
    root["spawn_groups"].push_back(entry);
    std::vector<nlohmann::json> sources{root};
    std::vector<std::string> paths;
    AssetSpawnPlanner planner(sources, *current_room_->room_area, assets_->library(), paths);
    const auto& queue = planner.get_spawn_queue();
    if (queue.empty()) return;

    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library = assets_->library().all();
    std::vector<std::unique_ptr<Asset>> spawned;
    std::vector<Area> exclusion;
    std::mt19937 rng(std::random_device{}());
    Check checker(false);
    SpawnLogger logger("", "");
    SpawnContext ctx(rng, checker, logger, exclusion, asset_info_library, spawned, &assets_->library(), grid ? grid.get() : nullptr);
    ExactSpawner exact;
    CenterSpawner center;
    RandomSpawner random;
    PerimeterSpawner perimeter;
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
    std::uniform_int_distribution<int> dist_w(min_w, max_w);
    std::uniform_int_distribution<int> dist_h(min_h, max_h);
    int width = std::max(1, dist_w(rng));
    int height = std::max(1, dist_h(rng));

    int map_radius = 0;
    nlohmann::json map_info_json;
    if (!current_room_->map_path.empty()) {
        std::ifstream map_info(current_room_->map_path + "/map_info.json");
        if (map_info.is_open()) {
            map_info >> map_info_json;
            map_radius = map_info_json.value("map_radius", 0);
        }
    }
    int map_w = map_radius > 0 ? map_radius * 2 : std::max(width * 2, 1);
    int map_h = map_radius > 0 ? map_radius * 2 : std::max(height * 2, 1);
    Area new_area(current_room_->room_name.empty() ? std::string("room") : current_room_->room_name,
                  center,
                  width,
                  height,
                  geometry,
                  edge,
                  map_w,
                  map_h);

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
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) continue;
        if (asset == player_) continue;
        bool remove = false;
        if (!asset->spawn_id.empty() && spawn_ids.count(asset->spawn_id)) {
            remove = true;
        } else if (asset->info) {
            std::string type = asset->info->type;
            std::string lowered;
            lowered.reserve(type.size());
            for (char ch : type) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            if (lowered == "boundary") {
                SDL_Point pos{asset->pos.x, asset->pos.y};
                bool inside_old = old_area_copy ? old_area_copy->contains_point(pos) : false;
                bool inside_new = new_area.contains_point(pos);
                if (inside_old || inside_new) {
                    remove = true;
                }
            }
        }
        if (remove) {
            to_remove.push_back(asset);
        }
    }

    for (Asset* asset : to_remove) {
        purge_asset(asset);
        auto& all = assets_->all;
        all.erase(std::remove(all.begin(), all.end(), asset), all.end());
        asset->Delete();
    }

    current_room_->room_area = std::make_unique<Area>(new_area);

    std::vector<nlohmann::json> planner_sources{room_json};
    std::vector<std::string> planner_paths;
    if (!current_room_->json_path.empty()) planner_paths.push_back(current_room_->json_path);
    current_room_->planner = std::make_unique<AssetSpawnPlanner>(planner_sources, *current_room_->room_area, assets_->library(), planner_paths);

    auto grid = build_room_grid(std::string{});
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library = assets_->library().all();
    std::vector<std::unique_ptr<Asset>> spawned;
    std::vector<Area> exclusion;
    Check checker(false);
    SpawnLogger logger("", "");
    std::mt19937 regen_rng(std::random_device{}());
    SpawnContext ctx(regen_rng, checker, logger, exclusion, asset_info_library, spawned, &assets_->library(), grid ? grid.get() : nullptr);
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
            MapGrid boundary_grid = MapGrid::from_area_bounds(*old_area_copy, boundary_spacing);
            auto points = boundary_grid.get_all_points_in_area(*old_area_copy);
            if (!points.empty()) {
                std::vector<int> weights;
                weights.reserve(boundary_options.size());
                for (const auto& opt : boundary_options) {
                    weights.push_back(std::max(1, opt.second));
                }
                std::discrete_distribution<int> pick(weights.begin(), weights.end());
                std::mt19937 boundary_rng(std::random_device{}());
                std::vector<std::unique_ptr<Asset>> boundary_spawned;
                for (auto* pt : points) {
                    if (!pt) continue;
                    if (current_room_->room_area->contains_point(pt->pos)) continue;
                    int idx = pick(boundary_rng);
                    const std::string& asset_name = boundary_options[idx].first;
                    auto info = assets_->library().get(asset_name);
                    if (!info) continue;
                    std::string spawn_id = generate_room_spawn_id();
                    Area spawn_area(asset_name, pt->pos, 1, 1, "Point", 1, 1, 1);
                    auto asset = std::make_unique<Asset>(info, spawn_area, pt->pos, 0, nullptr, spawn_id, std::string("Boundary"));
                    boundary_spawned.push_back(std::move(asset));
                }
                integrate_spawned_assets(boundary_spawned);
            }
        }
    }

    refresh_assets_config_ui();
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

void RoomEditor::update_perimeter_border_json(nlohmann::json& entry, double border_shift) {
    int value = static_cast<int>(std::lround(border_shift));
    value = std::max(0, std::min(100, value));
    entry["percentage_shift_from_center"] = value;
    if (entry.contains("border_shift")) entry.erase("border_shift");
    if (entry.contains("border_shift_min")) entry.erase("border_shift_min");
    if (entry.contains("border_shift_max")) entry.erase("border_shift_max");
}

void RoomEditor::update_perimeter_center_json(nlohmann::json& entry, SDL_Point offset) {
    entry["perimeter_x_offset"] = offset.x;
    entry["perimeter_x_offset_min"] = offset.x;
    entry["perimeter_x_offset_max"] = offset.x;
    entry["perimeter_y_offset"] = offset.y;
    entry["perimeter_y_offset_min"] = offset.y;
    entry["perimeter_y_offset_max"] = offset.y;
}
