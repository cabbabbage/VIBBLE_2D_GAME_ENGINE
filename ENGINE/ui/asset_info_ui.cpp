#include "asset_info_ui.hpp"

#include <algorithm>
#include <sstream>
#include <cmath>
#include <vector>
#include <climits>

#include "utils/input.hpp"
#include "asset/asset_info.hpp"
#include "ui/slider.hpp"
#include "ui/checkbox.hpp"
#include "ui/text_box.hpp"
#include "ui/button.hpp"
#include "asset/animation.hpp"
#include "utils/text_style.hpp"
#include "ui/styles.hpp"

namespace {
    
    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\n\r");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\n\r");
        return s.substr(b, e-b+1);
    }

    std::string blend_mode_to_string(SDL_BlendMode mode) {
        switch (mode) {
            case SDL_BLENDMODE_NONE: return "SDL_BLENDMODE_NONE";
            case SDL_BLENDMODE_BLEND: return "SDL_BLENDMODE_BLEND";
            case SDL_BLENDMODE_ADD: return "SDL_BLENDMODE_ADD";
            case SDL_BLENDMODE_MOD: return "SDL_BLENDMODE_MOD";
            case SDL_BLENDMODE_MUL: return "SDL_BLENDMODE_MUL";
            default: return "SDL_BLENDMODE_BLEND";
        }
    }
}

AssetInfoUI::AssetInfoUI() {}
AssetInfoUI::~AssetInfoUI() = default;

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    build_widgets();
}

void AssetInfoUI::clear_info() {
    info_.reset();
    s_z_threshold_.reset();
    s_min_same_type_.reset();
    s_min_all_.reset();
    s_scale_pct_.reset();
    c_passable_.reset();
    c_flipable_.reset();
    t_type_.reset();
    t_tags_.reset();
    // New collapsible header widgets and extras
    h_basic_.reset();
    h_animations_.reset();
    h_rendering_.reset();
    h_spawning_.reset();
    h_passability_.reset();
    h_lighting_.reset();
    h_children_.reset();
    s_scale_var_pct_.reset();
}

void AssetInfoUI::open()  { visible_ = true; }
void AssetInfoUI::close() { visible_ = false; }
void AssetInfoUI::toggle(){ visible_ = !visible_; }

void AssetInfoUI::build_widgets() {
    if (!info_) return;

    // Section headers
    h_basic_       = std::make_unique<Button>("Basic",        &Styles::MainDecoButton(), Button::width(), Button::height());
    h_animations_  = std::make_unique<Button>("Animations",   &Styles::MainDecoButton(), Button::width(), Button::height());
    h_rendering_   = std::make_unique<Button>("Rendering",    &Styles::MainDecoButton(), Button::width(), Button::height());
    h_spawning_    = std::make_unique<Button>("Spawning",     &Styles::MainDecoButton(), Button::width(), Button::height());
    h_passability_ = std::make_unique<Button>("Passability",  &Styles::MainDecoButton(), Button::width(), Button::height());
    h_lighting_    = std::make_unique<Button>("Lighting",     &Styles::MainDecoButton(), Button::width(), Button::height());
    h_children_    = std::make_unique<Button>("Child Assets", &Styles::MainDecoButton(), Button::width(), Button::height());

    // Sliders (use broad ranges)
    s_z_threshold_   = std::make_unique<Slider>("Z Threshold", -1024, 1024, info_->z_threshold);
    s_min_same_type_ = std::make_unique<Slider>("Min Same-Type Distance", 0, 2048, info_->min_same_type_distance);
    s_min_all_       = std::make_unique<Slider>("Min Distance (All)", 0, 2048, info_->min_distance_all);

    int pct = std::max(0, (int)std::round(info_->scale_factor * 100.0f));
    s_scale_pct_     = std::make_unique<Slider>("Scale (%)", 10, 400, pct);
    s_scale_var_pct_ = std::make_unique<Slider>("Scale Variability (%)", 0, 100, 0);

    // Checkboxes (passable derived from area presence; not directly editable)
    c_passable_.reset();
    c_flipable_ = std::make_unique<Checkbox>("Flipable (can invert)", info_->flipable);

    // Text boxes
    t_type_  = std::make_unique<TextBox>("Type", info_->type);
    // Tags joined by comma
    std::ostringstream oss;
    for (size_t i=0;i<info_->tags.size();++i) { oss << info_->tags[i]; if (i+1<info_->tags.size()) oss << ", "; }
    t_tags_  = std::make_unique<TextBox>("Tags (comma)", oss.str());

    scroll_ = 0;

    rebuild_area_buttons();
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    // Cache screen size for event-time refresh
    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;
    // Right third
    int panel_x = (screen_w * 2) / 3;
    int panel_w = screen_w - panel_x;
    panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };

    int x = panel_.x + 16;
    // Base cursor; widgets subtract scroll_ when setting their rects.
    int y = panel_.y + 16;

    const int gap_after_text = 32;    // text box label sits above by ~18px
    const int gap_after_slider = 18;  // breathing room under sliders
    const int gap_after_checkbox = 16;

    // Basic
    if (h_basic_) { h_basic_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }
    if (open_basic_) {
        if (t_type_)  { t_type_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, TextBox::height() }); y += TextBox::height() + gap_after_text; }
        if (t_tags_)  { t_tags_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, TextBox::height() }); y += TextBox::height() + gap_after_text + 4; }
    }

    // Animations (empty)
    if (h_animations_) { h_animations_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }

    // Rendering
    if (h_rendering_) { h_rendering_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }
    if (open_rendering_) {
        if (s_scale_pct_)     { s_scale_pct_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() }); y += Slider::height() + gap_after_slider; }
        if (s_scale_var_pct_) { s_scale_var_pct_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() }); y += Slider::height() + gap_after_slider; }
        if (s_z_threshold_)   { s_z_threshold_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() }); y += Slider::height() + gap_after_slider; }
        if (c_flipable_)      { c_flipable_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Checkbox::height() }); y += Checkbox::height() + gap_after_checkbox; }
    }

    // Spawning
    if (h_spawning_) { h_spawning_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }
    if (open_spawning_) {
        if (s_min_same_type_) { s_min_same_type_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() }); y += Slider::height() + gap_after_slider; }
        if (s_min_all_)       { s_min_all_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() }); y += Slider::height() + gap_after_slider; }
    }

    // Passability
    if (h_passability_) { h_passability_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }
    if (open_passability_) {
        if (!area_buttons_.empty() && area_buttons_.front().second) {
            area_buttons_.front().second->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() });
            y += Button::height() + 12;
        }
    }

    // Lighting (empty)
    if (h_lighting_) { h_lighting_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }

    // Child assets (empty)
    if (h_children_) { h_children_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Button::height() }); y += Button::height() + 8; }

    // Compute scroll max
    max_scroll_ = std::max(0, (y + 20) - panel_.h);
}

void AssetInfoUI::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;
    if (!info_) return;

    layout_widgets(screen_w, screen_h);

    // Scroll only when mouse within panel
    int mx = input.getX();
    int my = input.getY();
    if (mx >= panel_.x && mx < panel_.x + panel_.w && my >= panel_.y && my < panel_.y + panel_.h) {
        int dy = input.getScrollY();
        if (dy != 0) {
            scroll_ -= dy * 40;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_));
        }

        // Synthesize basic click interactions if SDL events are not routed
        if (input.wasClicked(Input::LEFT) && !synthesizing_) {
            synthesizing_ = true;
            SDL_Event down{}; down.type = SDL_MOUSEBUTTONDOWN; down.button.button = SDL_BUTTON_LEFT; down.button.x = mx; down.button.y = my;
            SDL_Event up{};   up.type   = SDL_MOUSEBUTTONUP;   up.button.button   = SDL_BUTTON_LEFT; up.button.x   = mx; up.button.y   = my;
            handle_event(down);
            handle_event(up);
            synthesizing_ = false;
        }
    }
}

void AssetInfoUI::handle_event(const SDL_Event& e) {
    if (!visible_ || !info_) return;
    // Ensure layout is current for this event
    int lw = last_screen_w_;
    int lh = last_screen_h_;
    if (lw <= 0 || lh <= 0) {
        Uint32 wid = 0;
        switch (e.type) {
            case SDL_MOUSEMOTION:      wid = e.motion.windowID; break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:    wid = e.button.windowID; break;
            case SDL_MOUSEWHEEL:       wid = e.wheel.windowID; break;
            default: break;
        }
        if (wid != 0) {
            if (SDL_Window* win = SDL_GetWindowFromID(wid)) {
                SDL_GetWindowSize(win, &lw, &lh);
            }
        }
    }
    if (lw > 0 && lh > 0) {
        layout_widgets(lw, lh);
    }

    bool changed = false;


    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const SDL_Point p{ e.button.x, e.button.y };
        auto inside = [&](const std::unique_ptr<TextBox>& tb){ return tb && SDL_PointInRect(&p, &tb->rect()); };
        bool any = (t_type_ && inside(t_type_)) || (t_tags_ && inside(t_tags_));
        if (any) {
            if (t_type_  && !inside(t_type_))  t_type_->set_editing(false);
            if (t_tags_  && !inside(t_tags_))  t_tags_->set_editing(false);
        } else {
            if (t_type_)  t_type_->set_editing(false);
            if (t_tags_)  t_tags_->set_editing(false);
        }
    }

    // Toggle collapsible sections via header buttons
    if (h_basic_       && h_basic_->handle_event(e))       open_basic_       = !open_basic_;
    if (h_animations_  && h_animations_->handle_event(e))  open_animations_  = !open_animations_;
    if (h_rendering_   && h_rendering_->handle_event(e))   open_rendering_   = !open_rendering_;
    if (h_spawning_    && h_spawning_->handle_event(e))    open_spawning_    = !open_spawning_;
    if (h_passability_ && h_passability_->handle_event(e)) open_passability_ = !open_passability_;
    if (h_lighting_    && h_lighting_->handle_event(e))    open_lighting_    = !open_lighting_;
    if (h_children_    && h_children_->handle_event(e))    open_children_    = !open_children_;

    // Sliders
    if (open_rendering_ && s_z_threshold_ && s_z_threshold_->handle_event(e)) {
        info_->set_z_threshold(s_z_threshold_->value());
        changed = true;
    }
    if (open_spawning_ && s_min_same_type_ && s_min_same_type_->handle_event(e)) {
        info_->set_min_same_type_distance(s_min_same_type_->value());
        changed = true;
    }
    if (open_spawning_ && s_min_all_ && s_min_all_->handle_event(e)) {
        info_->set_min_distance_all(s_min_all_->value());
        changed = true;
    }
    if (open_rendering_ && s_scale_pct_ && s_scale_pct_->handle_event(e)) {
        info_->set_scale_percentage((float)s_scale_pct_->value());
        reload_pending_ = true; // rebuild animations with new scale
        changed = true;
    }

    // Checkboxes
    if (c_flipable_ && c_flipable_->handle_event(e)) {
        info_->set_flipable(c_flipable_->value());
        changed = true;
    }

    // Text boxes (live updates)
    if (open_basic_ && t_type_ && t_type_->handle_event(e)) {
        info_->set_asset_type(t_type_->value());
        changed = true;
    }
    if (open_basic_ && t_tags_ && t_tags_->handle_event(e)) {
        // Parse CSV into tags vector
        std::vector<std::string> tags;
        std::string s = t_tags_->value();
        size_t pos = 0;
        while (pos != std::string::npos) {
            size_t comma = s.find(',', pos);
            std::string tok = (comma == std::string::npos) ? s.substr(pos) : s.substr(pos, comma - pos);
            tok = trim(tok);
            if (!tok.empty()) tags.push_back(tok);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        info_->set_tags(tags);
        // Keep 'passable' derived from presence of passability area, not tags
        bool has_pass = info_->has_passability_area && (bool)info_->passability_area;
        info_->set_passable(has_pass);
        changed = true;
    }
    

    if (changed) save_now();

    // Passability button only: handle click and defer heavy actions to render()
    if (!area_buttons_.empty()) {
        auto& kv = area_buttons_.front();
        if (kv.second && kv.second->handle_event(e)) {
            const std::string key = kv.first;
            const bool exists = has_area_key(key);
            pending_area_key_ = key;
            pending_create_ = !exists;
            pending_area_action_ = true;
        }
    }
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_ || !info_) return;
    layout_widgets(screen_w, screen_h);

    // If any setting requires a rebuild of animations (e.g., scale), do it now.
    if (reload_pending_) {
        info_->loadAnimations(r);
        reload_pending_ = false;
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Panel background on right third
    SDL_Color kInfoPanelBG = Styles::Night(); kInfoPanelBG.a = 160;
    SDL_SetRenderDrawColor(r, kInfoPanelBG.r, kInfoPanelBG.g, kInfoPanelBG.b, kInfoPanelBG.a);
    SDL_RenderFillRect(r, &panel_);

    // Section headers
    if (h_basic_)       h_basic_->render(r);
    if (h_animations_)  h_animations_->render(r);
    if (h_rendering_)   h_rendering_->render(r);
    if (h_spawning_)    h_spawning_->render(r);
    if (h_passability_) h_passability_->render(r);
    if (h_lighting_)    h_lighting_->render(r);
    if (h_children_)    h_children_->render(r);

    // Sections
    if (open_basic_) {
        if (t_type_)  t_type_->render(r);
        if (t_tags_)  t_tags_->render(r);
    }
    if (open_rendering_) {
        if (s_scale_pct_)     s_scale_pct_->render(r);
        if (s_scale_var_pct_) s_scale_var_pct_->render(r);
        if (s_z_threshold_)   s_z_threshold_->render(r);
        if (c_flipable_)      c_flipable_->render(r);
    }
    if (open_spawning_) {
        if (s_min_same_type_) s_min_same_type_->render(r);
        if (s_min_all_)       s_min_all_->render(r);
    }
    if (open_passability_) {
        if (!area_buttons_.empty() && area_buttons_.front().second) {
            area_buttons_.front().second->render(r);
        }
    }

    if (pending_area_action_) {
        pending_area_action_ = false;
        try {
            const std::string key = pending_area_key_;
            pending_area_key_.clear();
            std::string display = key;
            if (key == "spacing_area") display = "Spacing Area";
            else if (key == "impassable_area") display = "Impassable Area";
            else if (key == "collision_area") display = "Collision Area";
            else if (key == "interaction_area") display = "Interaction Area";
            else if (key == "hit_area") display = "Hit Area";


            SDL_Texture* bg = ensure_default_frame_texture(r);
            if (!bg) {
                if (info_->collision_area) bg = info_->collision_area->get_texture();
                else if (info_->interaction_area) bg = info_->interaction_area->get_texture();
                else if (info_->attack_area) bg = info_->attack_area->get_texture();
                else if (info_->passability_area) bg = info_->passability_area->get_texture();
            }
            if (!pending_create_) {

                if (key == "impassable_area" && info_->passability_area && bg) {
                    info_->passability_area->edit_with_ui(bg, r);
                    apply_area_from_points(key, info_->passability_area->get_points());
                    info_->has_passability_area = true;
                } else if (key == "collision_area" && info_->collision_area && bg) {
                    info_->collision_area->edit_with_ui(bg, r);
                    apply_area_from_points(key, info_->collision_area->get_points());
                    info_->has_collision_area = true;
                } else if (key == "interaction_area" && info_->interaction_area && bg) {
                    info_->interaction_area->edit_with_ui(bg, r);
                    apply_area_from_points(key, info_->interaction_area->get_points());
                    info_->has_interaction_area = true;
                } else if (key == "hit_area" && info_->attack_area && bg) {
                    info_->attack_area->edit_with_ui(bg, r);
                    apply_area_from_points(key, info_->attack_area->get_points());
                    info_->has_attack_area = true;
                }
            } else {

                if (bg) {
                    if (key == "impassable_area") {
                        info_->passability_area = std::make_unique<Area>(display, bg, r);
                        apply_area_from_points(key, info_->passability_area->get_points());
                        info_->has_passability_area = true;
                    } else if (key == "collision_area") {
                        info_->collision_area = std::make_unique<Area>(display, bg, r);
                        apply_area_from_points(key, info_->collision_area->get_points());
                        info_->has_collision_area = true;
                    } else if (key == "interaction_area") {
                        info_->interaction_area = std::make_unique<Area>(display, bg, r);
                        apply_area_from_points(key, info_->interaction_area->get_points());
                        info_->has_interaction_area = true;
                    } else if (key == "hit_area") {
                        info_->attack_area = std::make_unique<Area>(display, bg, r);
                        apply_area_from_points(key, info_->attack_area->get_points());
                        info_->has_attack_area = true;
                    }
                }
            }

            // Update derived passable from presence of passability area
            bool has_pass = info_->has_passability_area && (bool)info_->passability_area;
            info_->set_passable(has_pass);

            // Persist and refresh button labels
            (void)info_->update_info_json();
            const_cast<AssetInfoUI*>(this)->rebuild_area_buttons();
        } catch (const std::exception& ex) {
            SDL_Log("[AssetInfoUI] Area edit/create failed: %s", ex.what());
        }
    }
}

void AssetInfoUI::save_now() const {
    if (info_) (void)info_->update_info_json();
}

// -------------------- Areas helpers --------------------

void AssetInfoUI::rebuild_area_buttons() {
    area_buttons_.clear();
    area_labels_.clear();
    if (!info_) return;

    bool present = info_->has_passability_area && info_->passability_area;
    std::string btn_text = std::string(present ? "Edit " : "Create ") + std::string("Passability Area");
    auto btn = std::make_unique<Button>(btn_text, &Styles::MainDecoButton(), Button::width(), Button::height());
    area_buttons_.emplace_back("impassable_area", std::move(btn));
    area_labels_.emplace_back("Passability Area");

    // Keep passable derived from presence
    info_->set_passable(present);
}

SDL_Texture* AssetInfoUI::get_default_frame_texture() const {
    if (!info_) return nullptr;
    auto it = info_->animations.find("default");
    if (it == info_->animations.end()) it = info_->animations.find("start");
    if (it == info_->animations.end() && !info_->animations.empty()) it = info_->animations.begin();
    if (it == info_->animations.end()) return nullptr;
    const Animation& anim = it->second;
    if (anim.frames.empty()) return nullptr;
    return anim.frames.front();
}

SDL_Texture* AssetInfoUI::ensure_default_frame_texture(SDL_Renderer* r) const {
    if (!info_) return nullptr;
    SDL_Texture* tex = get_default_frame_texture();
    if (!tex) {
        // Attempt to load animations to populate frames
        info_->loadAnimations(r);
        tex = get_default_frame_texture();
    }
    return tex;
}

bool AssetInfoUI::has_area_key(const std::string& key) const {
    if (!info_) return false;
    if (key == "impassable_area")   return info_->has_passability_area && info_->passability_area;
    if (key == "collision_area")    return info_->has_collision_area    && info_->collision_area;
    if (key == "interaction_area")  return info_->has_interaction_area  && info_->interaction_area;
    if (key == "hit_area")          return info_->has_attack_area       && info_->attack_area;
    return false;
}

void AssetInfoUI::apply_area_from_points(const std::string& key,
                                         const std::vector<std::pair<int,int>>& pts) const {
    if (!info_) return;
    // Convert absolute points to coordinates relative to the sprite pivot
    // pivot = (scaled_canvas_w/2, scaled_canvas_h)
    const int pivot_x = static_cast<int>(std::lround(info_->original_canvas_width  * info_->scale_factor / 2.0f));
    const int pivot_y = static_cast<int>(std::lround(info_->original_canvas_height * info_->scale_factor        ));

    std::vector<std::pair<int,int>> rel;
    rel.reserve(pts.size());
    for (auto [x,y] : pts) {
        rel.emplace_back(x - pivot_x, y - pivot_y);
    }
    info_->set_area_points(key, rel);
}
