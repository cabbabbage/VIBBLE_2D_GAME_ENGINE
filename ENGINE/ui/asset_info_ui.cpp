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
    c_shading_.reset();
    c_flipable_.reset();
    t_type_.reset();
    t_tags_.reset();
    t_blend_.reset();
}

void AssetInfoUI::open()  { visible_ = true; }
void AssetInfoUI::close() { visible_ = false; }
void AssetInfoUI::toggle(){ visible_ = !visible_; }

void AssetInfoUI::build_widgets() {
    if (!info_) return;

    // Sliders (use broad ranges)
    s_z_threshold_   = std::make_unique<Slider>("Z Threshold", -1024, 1024, info_->z_threshold);
    s_min_same_type_ = std::make_unique<Slider>("Min Same-Type Distance", 0, 2048, info_->min_same_type_distance);
    s_min_all_       = std::make_unique<Slider>("Min Distance (All)", 0, 2048, info_->min_distance_all);

    int pct = std::max(0, (int)std::round(info_->scale_factor * 100.0f));
    s_scale_pct_     = std::make_unique<Slider>("Scale (%)", 10, 400, pct);

    // Checkboxes
    c_passable_ = std::make_unique<Checkbox>("Passable", info_->has_tag("passable"));
    c_shading_  = std::make_unique<Checkbox>("Has Shading", info_->has_shading);
    c_flipable_ = std::make_unique<Checkbox>("Flipable (can invert)", info_->flipable);

    // Text boxes
    t_type_  = std::make_unique<TextBox>("Type", info_->type);
    // Tags joined by comma
    std::ostringstream oss;
    for (size_t i=0;i<info_->tags.size();++i) { oss << info_->tags[i]; if (i+1<info_->tags.size()) oss << ", "; }
    t_tags_  = std::make_unique<TextBox>("Tags (comma)", oss.str());
    t_blend_ = std::make_unique<TextBox>("Blend Mode", blend_mode_to_string(info_->blendmode));

    b_close_ = std::make_unique<Button>(
        "Close",
        &Styles::ExitDecoButton(),
        Button::width(),
        Button::height()
    );

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
    // Leave generous space at top for the first group header.
    int y = panel_.y + 56;

    const int gap_after_text = 32;    // text box label sits above by ~18px
    const int gap_after_slider = 18;  // breathing room under sliders
    const int gap_after_checkbox = 16;

    // Group: Identity
    if (t_type_) {
        t_type_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, TextBox::height() });
        y += TextBox::height() + gap_after_text;
    }
    if (t_tags_) {
        t_tags_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, TextBox::height() });
        y += TextBox::height() + gap_after_text + 4;
    }

    // Group: Appearance
    if (t_blend_) {
        t_blend_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, TextBox::height() });
        y += TextBox::height() + gap_after_text - 4;
    }
    if (s_scale_pct_) {
        s_scale_pct_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() });
        y += Slider::height() + gap_after_slider;
    }
    if (c_shading_) {
        c_shading_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Checkbox::height() });
        y += Checkbox::height() + gap_after_checkbox;
    }
    if (c_flipable_) {
        c_flipable_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Checkbox::height() });
        y += Checkbox::height() + gap_after_checkbox + 6;
    }

    // Group: Distances / Z
    if (s_z_threshold_) {
        s_z_threshold_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() });
        y += Slider::height() + gap_after_slider;
    }
    if (s_min_same_type_) {
        s_min_same_type_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() });
        y += Slider::height() + gap_after_slider;
    }
    if (s_min_all_) {
        s_min_all_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Slider::height() });
        y += Slider::height() + gap_after_slider;
    }

    // Group: Flags (passable)
    if (c_passable_) {
        c_passable_->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, Checkbox::height() });
        y += Checkbox::height() + gap_after_checkbox;
    }

    // Group: Areas (buttons)
    if (!area_buttons_.empty()) {
        // Layout buttons full width under this group
        const int btn_h = Button::height();
        for (auto& kv : area_buttons_) {
            if (kv.second) {
                kv.second->set_rect(SDL_Rect{ x, y - scroll_, panel_w - 32, btn_h });
                y += btn_h + 14;
            }
        }
    }

        if (b_close_) {
        int btn_w = 100;
        int btn_h = Button::height();
        b_close_->set_rect(SDL_Rect{
            panel_.x + panel_.w - btn_w - 16,
            panel_.y + panel_.h - btn_h - 16,
            btn_w, btn_h
        });
    }

    // Compute scroll max
    // y currently holds the unscrolled content bottom; derive max scroll independent of current scroll
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
    if (b_close_ && b_close_->handle_event(e)) { close(); return; }
    bool changed = false;

    // Ensure layout is up-to-date before hit-testing
    // (Caller should have called update() earlier this frame.)

    // Manage exclusive focus for text boxes on mouse down
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const SDL_Point p{ e.button.x, e.button.y };
        auto inside = [&](const std::unique_ptr<TextBox>& tb){ return tb && SDL_PointInRect(&p, &tb->rect()); };
        bool any = (t_type_ && inside(t_type_)) || (t_tags_ && inside(t_tags_)) || (t_blend_ && inside(t_blend_));
        if (any) {
            if (t_type_  && !inside(t_type_))  t_type_->set_editing(false);
            if (t_tags_  && !inside(t_tags_))  t_tags_->set_editing(false);
            if (t_blend_ && !inside(t_blend_)) t_blend_->set_editing(false);
        } else {
            if (t_type_)  t_type_->set_editing(false);
            if (t_tags_)  t_tags_->set_editing(false);
            if (t_blend_) t_blend_->set_editing(false);
        }
    }

    // Sliders
    if (s_z_threshold_ && s_z_threshold_->handle_event(e)) {
        info_->set_z_threshold(s_z_threshold_->value());
        changed = true;
    }
    if (s_min_same_type_ && s_min_same_type_->handle_event(e)) {
        info_->set_min_same_type_distance(s_min_same_type_->value());
        changed = true;
    }
    if (s_min_all_ && s_min_all_->handle_event(e)) {
        info_->set_min_distance_all(s_min_all_->value());
        changed = true;
    }
    if (s_scale_pct_ && s_scale_pct_->handle_event(e)) {
        info_->set_scale_percentage((float)s_scale_pct_->value());
        reload_pending_ = true; // rebuild animations with new scale
        changed = true;
    }

    // Checkboxes
    if (c_passable_ && c_passable_->handle_event(e)) {
        info_->set_passable(c_passable_->value());
        // Also reflect in tags textbox
        if (t_tags_) {
            // rebuild tags string
            std::ostringstream oss;
            for (size_t i=0;i<info_->tags.size();++i) { oss << info_->tags[i]; if (i+1<info_->tags.size()) oss << ", "; }
            t_tags_->set_value(oss.str());
        }
        changed = true;
    }
    if (c_shading_ && c_shading_->handle_event(e)) {
        info_->set_has_shading(c_shading_->value());
        changed = true;
    }
    if (c_flipable_ && c_flipable_->handle_event(e)) {
        info_->set_flipable(c_flipable_->value());
        changed = true;
    }

    // Text boxes (live updates)
    if (t_type_ && t_type_->handle_event(e)) {
        info_->set_asset_type(t_type_->value());
        changed = true;
    }
    if (t_tags_ && t_tags_->handle_event(e)) {
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
        // Keep passable checkbox in sync
        if (c_passable_) c_passable_->set_value(info_->has_tag("passable"));
        changed = true;
    }
    if (t_blend_ && t_blend_->handle_event(e)) {
        info_->set_blend_mode_string(t_blend_->value());
        changed = true;
    }

    if (changed) save_now();

    // Area buttons: handle clicks and defer heavy actions to render()
    for (size_t i = 0; i < area_buttons_.size(); ++i) {
        auto& kv = area_buttons_[i];
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

    // Section headers disabled per request
    auto draw_header = [&](const char* /*title*/, int& /*y*/) { /* no-op */ };

    // Headers removed: directly render widgets
    int y_hdr = (t_type_ ? t_type_->rect().y : panel_.y + 40);
    int tmp = y_hdr - 26 + scroll_;
    if (t_type_)  t_type_->render(r);
    if (t_tags_)  t_tags_->render(r);

    // Appearance widgets (no header)
    int min_app_y = INT_MAX;
    if (t_blend_)     min_app_y = std::min(min_app_y, t_blend_->rect().y);
    if (s_scale_pct_) min_app_y = std::min(min_app_y, s_scale_pct_->rect().y);
    if (c_shading_)   min_app_y = std::min(min_app_y, c_shading_->rect().y);
    if (c_flipable_)  min_app_y = std::min(min_app_y, c_flipable_->rect().y);
    if (min_app_y != INT_MAX) {
        tmp = min_app_y - 26 + scroll_;
        // no header
    }
    if (t_blend_)     t_blend_->render(r);
    if (s_scale_pct_) s_scale_pct_->render(r);
    if (c_shading_)   c_shading_->render(r);
    if (c_flipable_)  c_flipable_->render(r);

    // Distances & Z widgets (no header)
    int min_dz_y = INT_MAX;
    if (s_z_threshold_)   min_dz_y = std::min(min_dz_y, s_z_threshold_->rect().y);
    if (s_min_same_type_) min_dz_y = std::min(min_dz_y, s_min_same_type_->rect().y);
    if (s_min_all_)       min_dz_y = std::min(min_dz_y, s_min_all_->rect().y);
    if (min_dz_y != INT_MAX) {
        tmp = min_dz_y - 26 + scroll_;
        // no header
    }
    if (s_z_threshold_)   s_z_threshold_->render(r);
    if (s_min_same_type_) s_min_same_type_->render(r);
    if (s_min_all_)       s_min_all_->render(r);

    // Flags (no header)
    if (c_passable_) {
        tmp = c_passable_->rect().y - 26 + scroll_;
        // no header
        c_passable_->render(r);
    }

    // Areas (no header)
    if (!area_buttons_.empty()) {
        // Find first button y
        int first_y = INT_MAX;
        for (const auto& kv : area_buttons_) {
            if (kv.second) first_y = std::min(first_y, kv.second->rect().y);
        }
        if (first_y != INT_MAX) {
            tmp = first_y - 26 + scroll_;
            // no header
        }
        for (const auto& kv : area_buttons_) {
            if (kv.second) kv.second->render(r);
        }
    }

    // Close button (anchored to panel corner; not scrolled)
    if (b_close_) b_close_->render(r);

    // Execute pending area edit/create action (requires renderer)
    if (pending_area_action_) {
        pending_area_action_ = false;
        try {
            const std::string key = pending_area_key_;
            pending_area_key_.clear();

            // Determine label for name seed
            std::string display = key;
            if (key == "spacing_area") display = "Spacing Area";
            else if (key == "impassable_area") display = "Impassable Area";
            else if (key == "collision_area") display = "Collision Area";
            else if (key == "interaction_area") display = "Interaction Area";
            else if (key == "hit_area") display = "Hit Area";

            // Edit existing or create new via interactive editor
            // Always use a meaningful background: default frame if available,
            // otherwise fall back to any area texture we already have.
            SDL_Texture* bg = ensure_default_frame_texture(r);
            if (!bg) {
                if (info_->collision_area) bg = info_->collision_area->get_texture();
                else if (info_->interaction_area) bg = info_->interaction_area->get_texture();
                else if (info_->attack_area) bg = info_->attack_area->get_texture();
                else if (info_->passability_area) bg = info_->passability_area->get_texture();
            }
            if (!pending_create_) {
                // Edit in place using chosen background
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
                // Create new using chosen background
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

    struct Row { const char* key; const char* label; bool present; } rows[] = {
        { "impassable_area",   "Impassable Area",   info_->has_passability_area && info_->passability_area },
        { "collision_area",    "Collision Area",    info_->has_collision_area    && info_->collision_area },
        { "interaction_area",  "Interaction Area",  info_->has_interaction_area  && info_->interaction_area },
        { "hit_area",          "Hit Area",          info_->has_attack_area       && info_->attack_area }
    };

    for (const auto& r : rows) {
        std::string btn_text = std::string(r.present ? "Edit " : "Create ") + r.label;
        auto btn = std::make_unique<Button>(btn_text, &Styles::MainDecoButton(), Button::width(), Button::height());
        area_buttons_.emplace_back(r.key, std::move(btn));
        area_labels_.emplace_back(r.label);
    }
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
