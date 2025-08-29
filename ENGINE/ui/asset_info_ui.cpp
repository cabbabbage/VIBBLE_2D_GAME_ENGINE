#include "asset_info_ui.hpp"

#include <algorithm>
#include <sstream>
#include <cmath>
#include <vector>

#include "utils/input.hpp"
#include "asset/asset_info.hpp"
#include "ui/slider.hpp"
#include "ui/checkbox.hpp"
#include "ui/text_box.hpp"
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
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    (void)screen_h;
    // Right third
    int panel_x = (screen_w * 2) / 3;
    int panel_w = screen_w - panel_x;
    panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };

    int x = panel_.x + 16;
    int y = 16 - scroll_;

    // Group: Identity
    if (t_type_) {
        t_type_->set_rect(SDL_Rect{ x, y + 20, std::min(440, panel_w - 32), TextBox::height() });
        y += 20 + TextBox::height() + 14;
    }
    if (t_tags_) {
        t_tags_->set_rect(SDL_Rect{ x, y + 20, std::min(480, panel_w - 32), TextBox::height() });
        y += 20 + TextBox::height() + 30;
    }

    // Group: Appearance
    if (t_blend_) {
        t_blend_->set_rect(SDL_Rect{ x, y + 20, std::min(360, panel_w - 32), TextBox::height() });
        y += 20 + TextBox::height() + 10;
    }
    if (s_scale_pct_) {
        s_scale_pct_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
        y += 10 + Slider::height() + 8;
    }
    if (c_shading_) {
        c_shading_->set_rect(SDL_Rect{ x, y + 8, panel_w - 32, Checkbox::height() });
        y += 8 + Checkbox::height() + 6;
    }
    if (c_flipable_) {
        c_flipable_->set_rect(SDL_Rect{ x, y + 4, panel_w - 32, Checkbox::height() });
        y += 4 + Checkbox::height() + 18;
    }

    // Group: Distances / Z
    if (s_z_threshold_) {
        s_z_threshold_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
        y += 10 + Slider::height() + 8;
    }
    if (s_min_same_type_) {
        s_min_same_type_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
        y += 10 + Slider::height() + 8;
    }
    if (s_min_all_) {
        s_min_all_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
        y += 10 + Slider::height() + 8;
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
    max_scroll_ = std::max(0, y + 20 - panel_.h);
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
    }
}

void AssetInfoUI::handle_event(const SDL_Event& e) {
    if (!visible_ || !info_) return;
    if (b_close_ && b_close_->handle_event(e)) {
        return; // close() already called in button callback
    }
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
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_ || !info_) return;
    layout_widgets(screen_w, screen_h);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Panel background on right third
    SDL_Color kInfoPanelBG = Styles::Night(); kInfoPanelBG.a = 160;
    SDL_SetRenderDrawColor(r, kInfoPanelBG.r, kInfoPanelBG.g, kInfoPanelBG.b, kInfoPanelBG.a);
    SDL_RenderFillRect(r, &panel_);

    // Draw group headers and widgets
    auto draw_header = [&](const char* title, int& y) {
        const TextStyle& h = TextStyles::MediumSecondary();
        TTF_Font* f = h.open_font();
        if (!f) return;
        SDL_Surface* surf = TTF_RenderText_Blended(f, title, Styles::Gold());
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ panel_.x + 16, y - scroll_, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(f);
        y += 22;
    };

    int y = 16;
    draw_header("Identity", y);
    if (t_type_)  t_type_->render(r);
    if (t_tags_)  t_tags_->render(r);

    y = t_tags_ ? (t_tags_->rect().y + t_tags_->rect().h + 18 + scroll_) : y + 60;
    draw_header("Appearance", y);
    if (t_blend_)     t_blend_->render(r);
    if (s_scale_pct_) s_scale_pct_->render(r);
    if (c_shading_)   c_shading_->render(r);
    if (c_flipable_)  c_flipable_->render(r);

    int headerDZ = (s_z_threshold_ ? s_z_threshold_->rect().y - 24 : (panel_.y + 280)) + scroll_;
    headerDZ = std::max(headerDZ, (c_flipable_ ? c_flipable_->rect().y + 16 + scroll_ : headerDZ));
    draw_header("Distances & Z", headerDZ);
    if (s_z_threshold_)   s_z_threshold_->render(r);
    if (s_min_same_type_) s_min_same_type_->render(r);
    if (s_min_all_)       s_min_all_->render(r);

    // Flags header placed above passable checkbox
    int flagsHeaderY = (c_passable_ ? c_passable_->rect().y - 24 + scroll_ : panel_.y + 520);
    draw_header("Flags", flagsHeaderY);
    if (c_passable_) c_passable_->render(r);
}

void AssetInfoUI::save_now() const {
    if (info_) (void)info_->update_info_json();
}
