
#include "dev_mouse_controls.hpp"
#include "asset/Asset.hpp"
#include "utils/mouse_input.hpp"
#include "utils/parallax.hpp"
#include <cmath>
#include <SDL.h>

DevMouseControls::DevMouseControls(MouseInput* m,
                                   std::vector<Asset*>& actives,
                                   Asset* player_,
                                   int screen_w_,
                                   int screen_h_)
    : mouse(m),
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

void DevMouseControls::handle_mouse_input(const std::unordered_set<SDL_Keycode>& keys) {
    
    if (keys.count(SDLK_ESCAPE)) {
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

    
    if (mouse->isDown(MouseInput::LEFT) && !selected_assets.empty()) {
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



        handle_hover();
        handle_click(keys);
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
        if (t == "Boundary" || t == "boundary" || t == "Texture") continue;

        
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

void DevMouseControls::handle_click(const std::unordered_set<SDL_Keycode>& keys) {
    if (!mouse || !player || !mouse->wasClicked(MouseInput::LEFT)) return;

    Asset* nearest = hovered_asset; 
    if (nearest) {
        const bool ctrlHeld = keys.count(SDLK_LCTRL) || keys.count(SDLK_RCTRL);
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
    } else {
        const bool ctrlHeld = keys.count(SDLK_LCTRL) || keys.count(SDLK_RCTRL);
        if (!ctrlHeld) {
            selected_assets.clear();
        }
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
