#include "dev_mouse_controls.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "utils/input.hpp"
#include "render/camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

DevMouseControls::DevMouseControls(Input* m,
                                   Assets* assets,
                                   std::vector<Asset*>& actives,
                                   Asset* player_,
                                   int screen_w_,
                                   int screen_h_)
    : mouse(m),
      assets_(assets),
      active_assets(actives),
      player(player_),
      screen_w(screen_w_),
      screen_h(screen_h_),
      dragging_(false),
      drag_last_x_(0),
      drag_last_y_(0) {}

void DevMouseControls::handle_mouse_input(const Input& input) {
    // Camera centers on player via update_zoom; mapping uses camera directly

    // Scroll wheel zoom: schedule smooth zoom animation to a new target scale
    int wheelY = input.getScrollY();
    if (wheelY != 0 && assets_) {
        camera& cam = assets_->getView();
        const double step = (zoom_scale_factor_ > 0.0) ? zoom_scale_factor_ : 1.0;
        double eff = 1.0;
        if (wheelY > 0)      eff = std::pow(step,  wheelY);
        else if (wheelY < 0) eff = 1.0 / std::pow(step, -wheelY);
        // Duration inversely proportional to magnitude of change
        int base = 18;
        int dur = std::max(6, base - 2 * std::min(6, std::abs(wheelY)));
        cam.animate_zoom_multiply(eff, dur);
    }

    if (input.isScancodeDown(SDL_SCANCODE_ESCAPE)) {
        selected_assets.clear();
        highlighted_assets.clear();
        hovered_asset = nullptr;
        dragging_ = false;

        for (Asset* a : active_assets) {
            if (!a) continue;
            a->set_selected(false);
            a->set_highlighted(false);
        }
        return;
    }

    if (!mouse) return;

    static int last_mx = -1;
    static int last_my = -1;

    const int mx = mouse->getX();
    const int my = mouse->getY();

    if (mouse->isDown(Input::LEFT) && !selected_assets.empty()) {
        // Only allow dragging for Exact/Exact Position or Percent spawn methods
        const std::string& method = selected_assets.front()->spawn_method;
        if (method == "Exact" || method == "Exact Position" || method == "Percent") {
            if (!dragging_) {
                dragging_ = true;
                drag_last_x_ = mx;
                drag_last_y_ = my;
            } else {
                int dx = mx - drag_last_x_;
                int dy = my - drag_last_y_;
                if (dx != 0 || dy != 0) {
                    for (Asset* a : selected_assets) {
                        if (!a) continue;
                        a->pos.x += dx;
                        a->pos.y += dy;
                    }
                    drag_last_x_ = mx;
                    drag_last_y_ = my;
                }
            }
        }
    } else {
        dragging_ = false;
    }

    // Right-click asset library selection removed; library is a floating panel now

    handle_hover();
    handle_click(input);
    update_highlighted_assets();

    last_mx = mx;
    last_my = my;
}

void DevMouseControls::handle_hover() {
    if (!mouse || !player) return;

    const int mx = mouse->getX();
    const int my = mouse->getY();

    // Camera centers on player via update_zoom

    Asset* nearest = nullptr;
    float nearest_d2 = std::numeric_limits<float>::max();

    for (Asset* a : active_assets) {
        if (!a || !a->info) continue;
        const std::string& t = a->info->type;
        if (t == "Boundary" || t == "boundary" || t == "Texture") continue;

        SDL_Point scr = assets_->getView().map_to_screen(SDL_Point{a->pos.x, a->pos.y});
        float dx = float(mx - scr.x);
        float dy = float(my - scr.y);
        float d2 = dx * dx + dy * dy;

        if (d2 < nearest_d2) {
            nearest_d2 = d2;
            nearest = a;
        }
    }

    if (nearest) {
        hovered_asset = nearest;
        hover_miss_frames_ = 0;
    } else {
        if (++hover_miss_frames_ >= 3) {
            hovered_asset = nullptr;
            hover_miss_frames_ = 3;
        }
    }
}

void DevMouseControls::handle_click(const Input& input) {
    if (!mouse || !player) return;
    // Right-click opens Asset Info for the hovered asset
    if (mouse->wasClicked(Input::RIGHT)) {
        if (rclick_buffer_frames_ > 0) {
            rclick_buffer_frames_--;
            return;
        }
        rclick_buffer_frames_ = 2;
        if (assets_ && hovered_asset) {
            assets_->open_asset_info_editor_for_asset(hovered_asset);
        }
        return;
    } else {
        rclick_buffer_frames_ = 0;
    }

    // Left-click selects by spawn id and opens Asset Config for the clicked asset
    if (!mouse->wasClicked(Input::LEFT)) {
        click_buffer_frames_ = 0;
        return;
    }
    if (click_buffer_frames_ > 0) {
        click_buffer_frames_--;
        return;
    }
    click_buffer_frames_ = 2;

    Asset* nearest = hovered_asset;
    if (nearest) {
        selected_assets.clear();
        if (!nearest->spawn_id.empty()) {
            for (Asset* a : active_assets) {
                if (a && a->spawn_id == nearest->spawn_id) selected_assets.push_back(a);
            }
        } else {
            selected_assets.push_back(nearest);
        }
        if (assets_) {
            assets_->open_asset_config_for_asset(nearest);
        }

        Uint32 now = SDL_GetTicks();
        if (last_click_asset_ == nearest && (now - last_click_time_ms_) <= 300) {
            last_click_time_ms_ = 0;
            last_click_asset_ = nullptr;
        } else {
            last_click_time_ms_ = now;
            last_click_asset_ = nearest;
        }
    } else {
        selected_assets.clear();
        last_click_asset_ = nullptr;
        last_click_time_ms_ = 0;
    }
}

void DevMouseControls::update_highlighted_assets() {
    highlighted_assets = selected_assets;
    bool allow_hover_group = false;
    if (hovered_asset) {
        if (selected_assets.empty()) {
            allow_hover_group = true;
        } else {
            if (!hovered_asset->spawn_id.empty()) {
                allow_hover_group = std::any_of(selected_assets.begin(), selected_assets.end(),
                                                [&](Asset* a){ return a && a->spawn_id == hovered_asset->spawn_id; });
            } else {
                allow_hover_group = std::find(selected_assets.begin(), selected_assets.end(), hovered_asset) != selected_assets.end();
            }
        }
    }
    if (allow_hover_group) {
        for (Asset* a : active_assets) {
            if (!a) continue;
            if (!hovered_asset->spawn_id.empty() && a->spawn_id == hovered_asset->spawn_id) {
                if (std::find(highlighted_assets.begin(), highlighted_assets.end(), a) == highlighted_assets.end())
                    highlighted_assets.push_back(a);
            } else if (a == hovered_asset) {
                if (std::find(highlighted_assets.begin(), highlighted_assets.end(), a) == highlighted_assets.end())
                    highlighted_assets.push_back(a);
            }
        }
    }

    for (Asset* a : active_assets) {
        if (!a) continue;
        a->set_highlighted(false);
        a->set_selected(false);
    }

    for (Asset* a : highlighted_assets) {
        if (!a) continue;
        if (std::find(selected_assets.begin(), selected_assets.end(), a) != selected_assets.end()) {
            a->set_selected(true);
            a->set_highlighted(false);
        } else {
            a->set_highlighted(true);
            a->set_selected(false);
        }
    }
}

SDL_Point DevMouseControls::compute_mouse_world(int mx_screen, int my_screen) const {
    return assets_->getView().screen_to_map(SDL_Point{mx_screen, my_screen});
}

void DevMouseControls::purge_asset(Asset* a) {
    if (!a) return;
    if (hovered_asset == a) hovered_asset = nullptr;
    if (last_click_asset_ == a) {
        last_click_asset_ = nullptr;
        last_click_time_ms_ = 0;
    }
    selected_assets.erase(std::remove(selected_assets.begin(), selected_assets.end(), a), selected_assets.end());
    highlighted_assets.erase(std::remove(highlighted_assets.begin(), highlighted_assets.end(), a), highlighted_assets.end());
    if (drag_anchor_asset_ == a) {
        drag_anchor_asset_ = nullptr;
        dragging_ = false;
    }
}

