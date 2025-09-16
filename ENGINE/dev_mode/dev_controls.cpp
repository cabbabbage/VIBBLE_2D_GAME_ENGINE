#include "dev_controls.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "dev_mode/asset_info_ui.hpp"
#include "dev_mode/asset_library_ui.hpp"
#include "dev_mode/assets_config.hpp"
#include "dev_mode/room_configurator.hpp"
#include "dev_mode/widgets.hpp"
#include "render/camera.hpp"
#include "room/room.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <tuple>

#include <nlohmann/json.hpp>

namespace {
std::string generate_spawn_id() {
    static std::mt19937 rng(std::random_device{}());
    static const char* hex = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string s = "spn-";
    for (int i = 0; i < 12; ++i) s.push_back(hex[dist(rng)]);
    return s;
}
}

DevControls::DevControls(Assets* owner, int screen_w, int screen_h)
    : assets_(owner), screen_w_(screen_w), screen_h_(screen_h) {}

DevControls::~DevControls() = default;

void DevControls::set_input(Input* input) {
    input_ = input;
    ensure_area_editor();
}

void DevControls::set_player(Asset* player) {
    player_ = player;
}

void DevControls::set_active_assets(std::vector<Asset*>& actives) {
    active_assets_ = &actives;
}

void DevControls::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
}

void DevControls::set_current_room(Room* room) {
    current_room_ = room;
}

void DevControls::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!assets_) return;

    camera& cam = assets_->getView();
    if (enabled_) {
        cam.set_parallax_enabled(false);
        cam.set_manual_zoom_override(false);
        close_asset_info_editor();
    } else {
        cam.set_parallax_enabled(true);
        cam.set_manual_zoom_override(false);
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

void DevControls::update(const Input& input) {
    handle_shortcuts(input);

    if (!enabled_) return;
    if (!input_ || !active_assets_) return;

    const int mx = input.getX();
    const int my = input.getY();

    if (!is_ui_blocking_input(mx, my)) {
        handle_mouse_input(input);
    }
}

void DevControls::update_ui(const Input& input) {
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->update(input, screen_w_, screen_h_, assets_->library_, *assets_);
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

void DevControls::handle_sdl_event(const SDL_Event& event) {
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

    if (handled && input_) {
        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            input_->clearClickBuffer();
        }
    }
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
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
    DMDropdown::render_active_options(renderer);
}

void DevControls::toggle_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    library_ui_->toggle();
}

void DevControls::open_asset_library() {
    if (!library_ui_) library_ui_ = std::make_unique<AssetLibraryUI>();
    library_ui_->open();
}

void DevControls::close_asset_library() {
    if (library_ui_) library_ui_->close();
}

bool DevControls::is_asset_library_open() const {
    return library_ui_ && library_ui_->is_visible();
}

std::shared_ptr<AssetInfo> DevControls::consume_selected_asset_from_library() {
    if (!library_ui_) return nullptr;
    return library_ui_->consume_selection();
}

void DevControls::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
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

void DevControls::open_asset_info_editor_for_asset(Asset* asset) {
    if (!asset || !asset->info) return;
    std::cout << "Opening AssetInfoUI for asset: " << asset->info->name << std::endl;
    clear_selection();
    focus_camera_on_asset(asset, 0.8, 20);
    open_asset_info_editor(asset->info);
    if (info_ui_) info_ui_->set_target_asset(asset);
}

void DevControls::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
}

bool DevControls::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

void DevControls::open_asset_config_for_asset(Asset* asset) {
    if (!asset) return;
    if (!assets_cfg_ui_) {
        assets_cfg_ui_ = std::make_unique<AssetsConfig>();
        if (current_room_) {
            auto& assets_json = current_room_->assets_data()["assets"];
            assets_cfg_ui_->load(assets_json, [this]() {
                if (current_room_) current_room_->save_assets_json();
            });
        }
    }
    SDL_Point scr = assets_->getView().map_to_screen({asset->pos.x, asset->pos.y});
    std::string id = asset->spawn_id.empty() ? (asset->info ? asset->info->name : std::string{}) : asset->spawn_id;
    assets_cfg_ui_->open_asset_config(id, scr.x, scr.y);
}

void DevControls::finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info) {
    if (!asset || !info || !current_room_) return;
    auto& root = current_room_->assets_data();
    auto& arr = root["assets"];
    if (!arr.is_array()) arr = nlohmann::json::array();

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

    auto clamp_int = [](int v) { return std::max(0, std::min(100, v)); };
    int ep_x = 50;
    int ep_y = 50;
    if (width != 0 && height != 0) {
        ep_x = clamp_int(static_cast<int>(std::lround(((double)(asset->pos.x - center.x) / width) * 100.0 + 50.0)));
        ep_y = clamp_int(static_cast<int>(std::lround(((double)(asset->pos.y - center.y) / height) * 100.0 + 50.0)));
    }

    std::string spawn_id = generate_spawn_id();
    nlohmann::json entry;
    entry["name"] = info->name;
    entry["spawn_id"] = spawn_id;
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["position"] = "Exact Position";
    entry["exact_position"] = nullptr;
    entry["inherited"] = false;
    entry["check_overlap"] = false;
    entry["check_min_spacing"] = false;
    entry["tag"] = false;
    entry["ep_x_min"] = ep_x;
    entry["ep_x_max"] = ep_x;
    entry["ep_y_min"] = ep_y;
    entry["ep_y_max"] = ep_y;
    arr.push_back(entry);
    current_room_->save_assets_json();
    asset->spawn_id = spawn_id;
    asset->spawn_method = "Exact Position";
    if (assets_cfg_ui_) {
        assets_cfg_ui_->load(arr, [this]() {
            if (current_room_) current_room_->save_assets_json();
        });
    }
}

void DevControls::toggle_room_config() {
    if (!room_cfg_ui_) room_cfg_ui_ = std::make_unique<RoomConfigurator>();
    if (room_cfg_ui_->visible()) {
        room_cfg_ui_->close();
    } else {
        room_cfg_ui_->open(current_room_);
        room_cfg_ui_->set_position(10, 10);
    }
}

void DevControls::close_room_config() {
    if (room_cfg_ui_) room_cfg_ui_->close();
}

bool DevControls::is_room_config_open() const {
    return room_cfg_ui_ && room_cfg_ui_->visible();
}

void DevControls::begin_area_edit_for_selected_asset(const std::string& area_name) {
    ensure_area_editor();
    if (!area_editor_) return;

    Asset* target = nullptr;
    if (!selected_assets_.empty()) target = selected_assets_.front();
    if (!target) target = hovered_asset_;
    if (!target) target = player_;
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

void DevControls::focus_camera_on_asset(Asset* asset, double zoom_factor, int duration_steps) {
    if (!asset || !assets_) return;
    camera& cam = assets_->getView();
    cam.set_manual_zoom_override(true);
    cam.pan_and_zoom_to_asset(asset, zoom_factor, duration_steps);
}

void DevControls::reset_click_state() {
    click_buffer_frames_ = 0;
    rclick_buffer_frames_ = 0;
    last_click_time_ms_ = 0;
    last_click_asset_ = nullptr;
    dragging_ = false;
}

void DevControls::clear_selection() {
    selected_assets_.clear();
    highlighted_assets_.clear();
    hovered_asset_ = nullptr;
    dragging_ = false;
    if (!active_assets_) return;
    for (Asset* asset : *active_assets_) {
        if (!asset) continue;
        asset->set_selected(false);
        asset->set_highlighted(false);
    }
}

void DevControls::purge_asset(Asset* asset) {
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
}

void DevControls::set_zoom_scale_factor(double factor) {
    zoom_scale_factor_ = (factor > 0.0) ? factor : 1.0;
}

void DevControls::handle_mouse_input(const Input& input) {
    camera& cam = assets_->getView();

    const int wheel_y = input.getScrollY();
    if (wheel_y != 0) {
        const double step = (zoom_scale_factor_ > 0.0) ? zoom_scale_factor_ : 1.0;
        double eff = 1.0;
        if (wheel_y > 0) {
            eff = std::pow(step, wheel_y);
        } else if (wheel_y < 0) {
            eff = 1.0 / std::pow(step, -wheel_y);
        }
        const int base = 18;
        const int dur = std::max(6, base - 2 * std::min(6, std::abs(wheel_y)));
        cam.animate_zoom_multiply(eff, dur);
    }

    if (input.isScancodeDown(SDL_SCANCODE_ESCAPE)) {
        clear_selection();
        return;
    }

    if (!input_) return;
    if (!player_) return;

    const int mx = input_->getX();
    const int my = input_->getY();

    if (input_->isDown(Input::LEFT) && !selected_assets_.empty()) {
        const std::string& method = selected_assets_.front()->spawn_method;
        if (method == "Exact" || method == "Exact Position" || method == "Percent") {
            if (!dragging_) {
                dragging_ = true;
                drag_last_x_ = mx;
                drag_last_y_ = my;
            } else {
                const int dx = mx - drag_last_x_;
                const int dy = my - drag_last_y_;
                if (dx != 0 || dy != 0) {
                    for (Asset* asset : selected_assets_) {
                        if (!asset) continue;
                        asset->pos.x += dx;
                        asset->pos.y += dy;
                    }
                    drag_last_x_ = mx;
                    drag_last_y_ = my;
                }
            }
        }
    } else {
        dragging_ = false;
    }

    handle_hover();
    handle_click(input);
    update_highlighted_assets();
}

void DevControls::handle_hover() {
    if (!input_ || !player_ || !active_assets_) return;

    const int mx = input_->getX();
    const int my = input_->getY();

    Asset* nearest = nullptr;
    float nearest_d2 = std::numeric_limits<float>::max();

    for (Asset* asset : *active_assets_) {
        if (!asset || !asset->info) continue;
        const std::string& type = asset->info->type;
        if (type == "Boundary" || type == "boundary" || type == "Texture") continue;

        SDL_Point scr = assets_->getView().map_to_screen(SDL_Point{asset->pos.x, asset->pos.y});
        const float dx = float(mx - scr.x);
        const float dy = float(my - scr.y);
        const float d2 = dx * dx + dy * dy;

        if (d2 < nearest_d2) {
            nearest_d2 = d2;
            nearest = asset;
        }
    }

    if (nearest) {
        hovered_asset_ = nearest;
        hover_miss_frames_ = 0;
    } else {
        if (++hover_miss_frames_ >= 3) {
            hovered_asset_ = nullptr;
            hover_miss_frames_ = 3;
        }
    }
}

void DevControls::handle_click(const Input& input) {
    if (!input_ || !player_) return;

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
        if (!nearest->spawn_id.empty() && active_assets_) {
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

void DevControls::update_highlighted_assets() {
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

bool DevControls::is_ui_blocking_input(int mx, int my) const {
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

void DevControls::handle_shortcuts(const Input& input) {
    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (!ctrl) return;

    if (input.wasScancodePressed(SDL_SCANCODE_A)) {
        toggle_asset_library();
    }
    if (input.wasScancodePressed(SDL_SCANCODE_R)) {
        toggle_room_config();
    }
}

void DevControls::update_area_editor_focus() {
    ensure_area_editor();
    if (!area_editor_) return;

    const bool editing_overlay_active = area_editor_->is_active();
    if (!assets_) return;

    camera& cam = assets_->getView();
    if (editing_overlay_active) {
        Asset* focus = nullptr;
        if (!selected_assets_.empty()) focus = selected_assets_.front();
        if (!focus) focus = hovered_asset_;
        if (!focus) focus = player_;
        if (focus) {
            cam.set_manual_zoom_override(true);
            cam.set_focus_override(SDL_Point{focus->pos.x, focus->pos.y});
        }
    } else {
        cam.clear_focus_override();
    }
}

void DevControls::ensure_area_editor() {
    if (!area_editor_) {
        area_editor_ = std::make_unique<AreaOverlayEditor>();
        if (area_editor_) area_editor_->attach_assets(assets_);
    }
}
