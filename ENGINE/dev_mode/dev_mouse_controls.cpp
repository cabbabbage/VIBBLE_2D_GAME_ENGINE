
#include "dev_mouse_controls.hpp"
#include "asset/Asset.hpp"
#include "core/Assets.hpp"
#include "utils/input.hpp"
#include "utils/parallax.hpp"
#include <cmath>
#include <SDL.h>

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
      parallax_(screen_w_, screen_h_),   
      dragging_(false),
      drag_last_x_(0),
      drag_last_y_(0)
{
}

void DevMouseControls::handle_mouse_input(const Input& input) {
    // Keep parallax reference fresh for world<->screen conversions
    if (player) {
        parallax_.setReference(player->pos_X, player->pos_Y);
    }

    if (input.isKeyDown(SDLK_ESCAPE)) {
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
                    a->pos_X += dx;
                    a->pos_Y += dy;
                }
                drag_last_x_ = mx;
                drag_last_y_ = my;
            }
        }
    } else {
        dragging_ = false; 
    }



        // Right-click now opens the asset info editor for the hovered asset
        if (mouse->wasReleased(Input::RIGHT) && assets_) {
            if (hovered_asset && hovered_asset->info) {
                assets_->open_asset_info_editor(hovered_asset->info);
            }
        }

        // If waiting for selection, check if a selection was made
        if (waiting_spawn_selection_ && assets_) {
            if (!assets_->is_asset_library_open()) {
                auto chosen = assets_->consume_selected_asset_from_library();
                if (chosen) {
                    assets_->spawn_asset(chosen->name, spawn_world_x_, spawn_world_y_);
                }
                waiting_spawn_selection_ = false;
            }
        }

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

    
    parallax_.setReference(player->pos_X, player->pos_Y);

    Asset* nearest = nullptr;
    float nearest_d2 = std::numeric_limits<float>::max();

    for (Asset* a : active_assets) {
        if (!a || !a->info) continue;
        const std::string& t = a->info->type;
        if (t == "Texture") continue;

        
        SDL_Point scr = parallax_.apply(a->pos_X, a->pos_Y);

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

    // Use release edge for click/double-click to avoid multi-frame click buffer repeats
    if (!mouse->wasReleased(Input::LEFT)) return;

    Asset* nearest = hovered_asset; 
    if (nearest) {
        const bool ctrlHeld = input.isKeyDown(SDLK_LCTRL) || input.isKeyDown(SDLK_RCTRL);
        auto it = std::find(selected_assets.begin(), selected_assets.end(), nearest);

        if (ctrlHeld) {
            if (it == selected_assets.end()) {
                selected_assets.push_back(nearest);
            } else {
                selected_assets.erase(it);
            }
        } else {
            if (it != selected_assets.end() && selected_assets.size() == 1) {
                selected_assets.clear(); 
            } else {
                selected_assets.clear();
                selected_assets.push_back(nearest);
            }
        }

        // Double-click detection (left): within 300ms triggers spawn flow
        Uint32 now = SDL_GetTicks();
        if (now - last_left_click_time_ms_ <= 300) {
            if (assets_) {
                int mx = mouse->getX();
                int my = mouse->getY();
                spawn_click_screen_x_ = mx;
                spawn_click_screen_y_ = my;
                // Place exactly where visually clicked: convert screen->world using player center + screen offset
                int cx = screen_w / 2;
                int cy = screen_h / 2;
                int px = assets_->player ? assets_->player->pos_X : 0;
                int py = assets_->player ? assets_->player->pos_Y : 0;
                spawn_world_x_ = px + (mx - cx);
                spawn_world_y_ = py + (my - cy);
                waiting_spawn_selection_ = true;
                assets_->open_asset_library();
            }
            last_left_click_time_ms_ = 0; // reset after double-click
            return; // do not process further selection changes this frame
        } else {
            last_left_click_time_ms_ = now;
        }
    } else {
        const bool ctrlHeld = input.isKeyDown(SDLK_LCTRL) || input.isKeyDown(SDLK_RCTRL);
        if (!ctrlHeld) {
            selected_assets.clear();
        }
        last_left_click_time_ms_ = 0;
    }
}

void DevMouseControls::update_highlighted_assets() {
    highlighted_assets = selected_assets;
    if (hovered_asset &&
        std::find(highlighted_assets.begin(), highlighted_assets.end(), hovered_asset) == highlighted_assets.end()) {
        highlighted_assets.push_back(hovered_asset);
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

// compute_mouse_world removed: mouse is treated as screen space; spawn uses direct screen offset from player
