#include "AnimationEditorWindow.hpp"

#include <SDL_ttf.h>

#include "asset/asset_info.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

namespace {

void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer || text.empty()) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

}  // namespace

namespace animation_editor {

AnimationEditorWindow::AnimationEditorWindow() = default;

void AnimationEditorWindow::set_visible(bool visible) { visible_ = visible; }

void AnimationEditorWindow::toggle_visible() { visible_ = !visible_; }

void AnimationEditorWindow::set_bounds(const SDL_Rect& bounds) { bounds_ = bounds; }

void AnimationEditorWindow::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    if (info) {
        info_path_ = info->info_json_path();
    } else {
        info_path_.clear();
    }
}

void AnimationEditorWindow::clear_info() {
    info_.reset();
    info_path_.clear();
}

void AnimationEditorWindow::update(const Input&, int, int) {
    // Placeholder implementation for future logic.
}

void AnimationEditorWindow::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    render_background(renderer);
    render_placeholder(renderer);
}

bool AnimationEditorWindow::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    if (pointer_event) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) {
            p.x = e.motion.x;
            p.y = e.motion.y;
        } else {
            p.x = e.button.x;
            p.y = e.button.y;
        }
        if (!SDL_PointInRect(&p, &bounds_)) {
            return false;
        }
        // Consume pointer interactions within the editor bounds for now.
        return true;
    }

    return false;
}

void AnimationEditorWindow::render_background(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bounds_);

    const SDL_Color border = DMStyles::AccentButton().hover_bg;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(renderer, &bounds_);
}

void AnimationEditorWindow::render_placeholder(SDL_Renderer* renderer) const {
    const int padding = DMSpacing::panel_padding();
    std::string header = "Animation Editor";
    render_label(renderer, header, bounds_.x + padding, bounds_.y + padding);

    if (!info_path_.empty()) {
        std::string details = "Editing: " + info_path_.filename().string();
        render_label(renderer, details, bounds_.x + padding, bounds_.y + padding * 2 + DMButton::height());
    } else {
        std::string details = "Select an asset to begin.";
        render_label(renderer, details, bounds_.x + padding, bounds_.y + padding * 2 + DMButton::height());
    }
}

}  // namespace animation_editor

