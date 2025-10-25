#include "AnimationListPanel.hpp"

#include <SDL.h>

#include <algorithm>
#include <optional>
#include <utility>

#include "AnimationDocument.hpp"
#include "PreviewProvider.hpp"
#include "dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/widgets.hpp"

namespace {

constexpr int kRowHeight = 72;

SDL_Point event_point(const SDL_Event& e) {
    SDL_Point p{0, 0};
    if (e.type == SDL_MOUSEMOTION) {
        p.x = e.motion.x;
        p.y = e.motion.y;
    } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        p.x = e.button.x;
        p.y = e.button.y;
    }
    return p;
}

bool rects_intersect(const SDL_Rect& a, const SDL_Rect& b) {
    SDL_Rect result{};
    return SDL_IntersectRect(&a, &b, &result) == SDL_TRUE;
}

void draw_text(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color) {
    if (!renderer || text.empty()) {
        return;
    }

    DMLabelStyle style = DMStyles::Label();
    style.color = color;
    DMFontCache::instance().draw_text(renderer, style, text, x, y);
}

}

namespace animation_editor {

AnimationListPanel::AnimationListPanel() = default;

void AnimationListPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    rebuild_children();
}

void AnimationListPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    clamp_scroll();
    layout_dirty_ = true;
}

void AnimationListPanel::set_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_provider_ = std::move(provider);
}

void AnimationListPanel::set_selected_animation_id(const std::optional<std::string>& animation_id) {
    selected_animation_id_ = animation_id;
    if (layout_dirty_) {
        layout_rows();
    }
    scroll_selection_into_view();
}

void AnimationListPanel::set_on_selection_changed(
    std::function<void(const std::optional<std::string>&)> callback) {
    on_selection_changed_ = std::move(callback);
}

void AnimationListPanel::set_on_context_menu(
    std::function<void(const std::string&, const SDL_Point&)> callback) {
    on_context_menu_ = std::move(callback);
}

void AnimationListPanel::update() {
    if (document_) {
        auto ids = document_->animation_ids();
        if (ids != cached_animation_ids_) {
            cached_animation_ids_ = ids;
            layout_dirty_ = true;
            if (selected_animation_id_) {
                auto it = std::find(cached_animation_ids_.begin(), cached_animation_ids_.end(), *selected_animation_id_);
                if (it == cached_animation_ids_.end()) {
                    selected_animation_id_.reset();
                    if (on_selection_changed_) {
                        on_selection_changed_(std::nullopt);
                    }
                }
            }
        }
        start_animation_id_ = document_->start_animation();
    } else {
        if (!cached_animation_ids_.empty()) {
            cached_animation_ids_.clear();
            selected_animation_id_.reset();
            row_bounds_.clear();
            scroll_offset_ = 0;
            content_height_ = 0;
            layout_dirty_ = true;
            if (on_selection_changed_) {
                on_selection_changed_(std::nullopt);
            }
        }
        start_animation_id_.reset();
    }

    if (layout_dirty_) {
        layout_rows();
    }
}

void AnimationListPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect(renderer,
                             bounds_,
                             DMStyles::CornerRadius(),
                             DMStyles::BevelDepth(),
                             DMStyles::PanelBG(),
                             DMStyles::HighlightColor(),
                             DMStyles::ShadowColor(),
                             false,
                             DMStyles::HighlightIntensity(),
                             DMStyles::ShadowIntensity());

    SDL_Rect clip = bounds_;
    const int inset = DMStyles::BevelDepth();
    clip.x += inset;
    clip.y += inset;
    clip.w = std::max(0, clip.w - inset * 2);
    clip.h = std::max(0, clip.h - inset * 2);
    if (clip.w > 0 && clip.h > 0) {
        SDL_RenderSetClipRect(renderer, &clip);
    }

    const DMButtonStyle& style = DMStyles::ListButton();
    const SDL_Color& border = style.border;
    const SDL_Color text_color = style.label.color;
    const SDL_Color hover_bg = style.hover_bg;
    const SDL_Color selected_bg = style.press_bg;
    const SDL_Color idle_bg = style.bg;

    const int row_padding = DMSpacing::small_gap();

    for (size_t i = 0; i < cached_animation_ids_.size(); ++i) {
        const SDL_Rect& rect = row_bounds_.at(i);
        if (!rects_intersect(rect, bounds_)) {
            continue;
        }

        const bool selected = selected_animation_id_ && *selected_animation_id_ == cached_animation_ids_[i];
        const bool hovered = hovered_row_ && *hovered_row_ == i;
        const SDL_Color fill = selected ? selected_bg : (hovered ? hover_bg : idle_bg);

        dm_draw::DrawBeveledRect(renderer,
                                 rect,
                                 DMStyles::CornerRadius(),
                                 DMStyles::BevelDepth(),
                                 fill,
                                 DMStyles::HighlightColor(),
                                 DMStyles::ShadowColor(),
                                 false,
                                 DMStyles::HighlightIntensity(),
                                 DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline(renderer, rect, DMStyles::CornerRadius(), 1, border);

        int content_x = rect.x + row_padding;
        int content_y = rect.y + row_padding;
        const int content_h = rect.h - row_padding * 2;

        // Thumbnail
        if (preview_provider_) {
            SDL_Texture* texture = preview_provider_->get_preview_texture(renderer, cached_animation_ids_[i]);
            if (texture) {
                const int thumb_size = content_h;
                SDL_Rect thumb_rect{content_x, rect.y + (rect.h - thumb_size) / 2, thumb_size, thumb_size};
                int tex_w = 0;
                int tex_h = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
                if (tex_w > 0 && tex_h > 0) {
                    float scale = std::min(static_cast<float>(thumb_rect.w) / static_cast<float>(tex_w),
                                            static_cast<float>(thumb_rect.h) / static_cast<float>(tex_h));
                    int draw_w = std::max(1, static_cast<int>(tex_w * scale));
                    int draw_h = std::max(1, static_cast<int>(tex_h * scale));
                    SDL_Rect dst{thumb_rect.x + (thumb_rect.w - draw_w) / 2,
                                 thumb_rect.y + (thumb_rect.h - draw_h) / 2,
                                 draw_w,
                                 draw_h};
                    SDL_RenderCopy(renderer, texture, nullptr, &dst);
                }
                content_x += thumb_rect.w + row_padding;
            }
        }

        const std::string& name = cached_animation_ids_[i];
        draw_text(renderer, name, content_x, content_y, text_color);

        if (start_animation_id_ && *start_animation_id_ == name) {
            const DMButtonStyle& badge_style = DMStyles::AccentButton();
            DMLabelStyle badge_label = badge_style.label;
            badge_label.font_size = std::max(12, badge_label.font_size - 2);
            const std::string badge_text = "START";
            SDL_Point badge_size = DMFontCache::instance().measure_text(badge_label, badge_text);
            const int badge_padding = DMSpacing::small_gap();
            SDL_Rect badge_rect{rect.x + rect.w - badge_size.x - badge_padding * 2,
                                rect.y + badge_padding,
                                badge_size.x + badge_padding * 2,
                                badge_size.y + badge_padding};
            badge_rect.x = std::max(badge_rect.x, rect.x + content_x - row_padding);
            dm_draw::DrawBeveledRect(renderer,
                                     badge_rect,
                                     DMStyles::CornerRadius(),
                                     DMStyles::BevelDepth(),
                                     badge_style.bg,
                                     DMStyles::HighlightColor(),
                                     DMStyles::ShadowColor(),
                                     false,
                                     DMStyles::HighlightIntensity(),
                                     DMStyles::ShadowIntensity());
            dm_draw::DrawRoundedOutline(renderer, badge_rect, DMStyles::CornerRadius(), 1, badge_style.border);
            DMFontCache::instance().draw_text(renderer,
                                              badge_label,
                                              badge_text,
                                              badge_rect.x + badge_padding,
                                              badge_rect.y + (badge_rect.h - badge_size.y) / 2);
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);
}

bool AnimationListPanel::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point mouse{mx, my};
        if (!SDL_PointInRect(&mouse, &bounds_) && !DMWidgetsSliderScrollCaptured()) {
            return false;
        }
        const int step = DMButton::height() + DMSpacing::section_gap();
        scroll_offset_ -= e.wheel.y * step;
        clamp_scroll();
        layout_dirty_ = true;
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &bounds_)) {
            hovered_row_.reset();
            return false;
        }
        hovered_row_ = row_index_at_point(p);
        return hovered_row_.has_value();
    }

    if (e.type == SDL_MOUSEBUTTONDOWN) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &bounds_)) {
            return false;
        }

        auto index = row_index_at_point(p);
        if (!index) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (selected_animation_id_) {
                    selected_animation_id_.reset();
                    if (on_selection_changed_) {
                        on_selection_changed_(std::nullopt);
                    }
                }
            }
            return true;
        }

        const std::string& animation_id = cached_animation_ids_.at(*index);
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (!selected_animation_id_ || *selected_animation_id_ != animation_id) {
                selected_animation_id_ = animation_id;
                scroll_selection_into_view();
                if (on_selection_changed_) {
                    on_selection_changed_(selected_animation_id_);
                }
            }
            return true;
        }

        if (e.button.button == SDL_BUTTON_RIGHT) {
            if (on_context_menu_) {
                on_context_menu_(animation_id, p);
            }
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &bounds_)) {
            return false;
        }
        auto index = row_index_at_point(p);
        return index.has_value();
    }

    return false;
}

void AnimationListPanel::rebuild_children() {
    cached_animation_ids_.clear();
    row_bounds_.clear();
    scroll_offset_ = 0;
    content_height_ = 0;
    layout_dirty_ = true;
    hovered_row_.reset();

    if (!document_) {
        start_animation_id_.reset();
        return;
    }

    cached_animation_ids_ = document_->animation_ids();
    start_animation_id_ = document_->start_animation();
    row_bounds_.resize(cached_animation_ids_.size());
}

void AnimationListPanel::layout_rows() {
    layout_dirty_ = false;

    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::small_gap();
    const int width = std::max(0, bounds_.w - padding * 2);

    row_bounds_.assign(cached_animation_ids_.size(), SDL_Rect{});

    int y = bounds_.y + padding - scroll_offset_;
    for (size_t i = 0; i < cached_animation_ids_.size(); ++i) {
        SDL_Rect rect{bounds_.x + padding, y, width, kRowHeight};
        row_bounds_[i] = rect;
        y += kRowHeight + gap;
    }

    content_height_ = padding * 2;
    if (!cached_animation_ids_.empty()) {
        content_height_ += static_cast<int>(cached_animation_ids_.size()) * kRowHeight;
        content_height_ += gap * (static_cast<int>(cached_animation_ids_.size()) - 1);
    }

    clamp_scroll();
}

void AnimationListPanel::clamp_scroll() {
    int viewport = std::max(0, bounds_.h);
    int max_offset = std::max(0, content_height_ - viewport);
    if (scroll_offset_ < 0) {
        scroll_offset_ = 0;
    } else if (scroll_offset_ > max_offset) {
        scroll_offset_ = max_offset;
    }
}

void AnimationListPanel::scroll_selection_into_view() {
    if (!selected_animation_id_) {
        return;
    }
    if (layout_dirty_) {
        layout_rows();
    }
    auto it = std::find(cached_animation_ids_.begin(), cached_animation_ids_.end(), *selected_animation_id_);
    if (it == cached_animation_ids_.end()) {
        return;
    }
    size_t index = static_cast<size_t>(std::distance(cached_animation_ids_.begin(), it));
    if (index >= row_bounds_.size()) {
        return;
    }
    SDL_Rect rect = row_bounds_[index];
    const int top = rect.y;
    const int bottom = rect.y + rect.h;
    const int viewport_top = bounds_.y;
    const int viewport_bottom = bounds_.y + bounds_.h;

    if (top < viewport_top) {
        scroll_offset_ -= (viewport_top - top);
        clamp_scroll();
        layout_rows();
    } else if (bottom > viewport_bottom) {
        scroll_offset_ += (bottom - viewport_bottom);
        clamp_scroll();
        layout_rows();
    }
}

std::optional<size_t> AnimationListPanel::row_index_at_point(const SDL_Point& p) const {
    for (size_t i = 0; i < row_bounds_.size(); ++i) {
        if (SDL_PointInRect(&p, &row_bounds_[i])) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace animation_editor
