#include "AnimationListPanel.hpp"

#include <SDL.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "AnimationDocument.hpp"
#include "AnimationInspectorPanel.hpp"
#include "PreviewProvider.hpp"
#include "dm_styles.hpp"
#include "dev_mode/widgets.hpp"

namespace {

bool is_pointer_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION:
            return true;
        default:
            return false;
    }
}

bool is_keyboard_event(const SDL_Event& e) {
    return e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT;
}

SDL_Point event_point(const SDL_Event& e) {
    SDL_Point p{0, 0};
    if (e.type == SDL_MOUSEMOTION) {
        p.x = e.motion.x;
        p.y = e.motion.y;
    } else {
        p.x = e.button.x;
        p.y = e.button.y;
    }
    return p;
}

bool rects_intersect(const SDL_Rect& a, const SDL_Rect& b) {
    SDL_Rect result{};
    return SDL_IntersectRect(&a, &b, &result) == SDL_TRUE;
}

}  // namespace

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
    for (auto& inspector : inspectors_) {
        if (inspector) {
            inspector->set_preview_provider(preview_provider_);
        }
    }
    layout_dirty_ = true;
}

void AnimationListPanel::set_inspector_configurator(std::function<void(AnimationInspectorPanel&)> configurator) {
    inspector_configurator_ = std::move(configurator);
    for (auto& inspector : inspectors_) {
        if (inspector && inspector_configurator_) {
            inspector_configurator_(*inspector);
        }
    }
    layout_dirty_ = true;
}

void AnimationListPanel::update() {
    if (document_) {
        auto ids = document_->animation_ids();
        if (ids != cached_animation_ids_) {
            cached_animation_ids_ = ids;
            rebuild_children();
        }
    } else if (!inspectors_.empty()) {
        inspectors_.clear();
        inspector_bounds_.clear();
        cached_animation_ids_.clear();
        scroll_offset_ = 0;
        content_height_ = 0;
    }

    if (layout_dirty_) {
        layout_inspectors();
    }

    for (auto& inspector : inspectors_) {
        if (inspector) {
            inspector->update();
        }
    }
}

void AnimationListPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color& bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bounds_);

    const SDL_Color& border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bounds_);

    SDL_RenderSetClipRect(renderer, &bounds_);
    for (size_t i = 0; i < inspectors_.size(); ++i) {
        const SDL_Rect& rect = inspector_bounds_[i];
        if (!rects_intersect(rect, bounds_)) {
            continue;
        }
        if (inspectors_[i]) {
            inspectors_[i]->render(renderer);
        }
    }
    SDL_RenderSetClipRect(renderer, nullptr);
}

bool AnimationListPanel::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL) {
        const int step = DMSpacing::section_gap() + DMButton::height();
        scroll_offset_ -= e.wheel.y * step;
        clamp_scroll();
        layout_dirty_ = true;
        return true;
    }

    const bool pointer = is_pointer_event(e);
    if (pointer) {
        SDL_Point p = event_point(e);
        if (!SDL_PointInRect(&p, &bounds_)) {
            return false;
        }
    }

    const bool keyboard = is_keyboard_event(e);
    bool handled = false;

    if (pointer) {
        SDL_Point p = event_point(e);
        for (size_t i = 0; i < inspectors_.size(); ++i) {
            const SDL_Rect& rect = inspector_bounds_[i];
            if (!SDL_PointInRect(&p, &rect)) {
                continue;
            }
            if (inspectors_[i] && inspectors_[i]->handle_event(e)) {
                handled = true;
                break;
            }
        }
    } else if (keyboard) {
        for (auto& inspector : inspectors_) {
            if (inspector && inspector->handle_event(e)) {
                handled = true;
                break;
            }
        }
    }

    return handled;
}

void AnimationListPanel::rebuild_children() {
    inspectors_.clear();
    inspector_bounds_.clear();

    if (!document_) {
        cached_animation_ids_.clear();
        scroll_offset_ = 0;
        content_height_ = 0;
        layout_dirty_ = true;
        return;
    }

    cached_animation_ids_ = document_->animation_ids();
    inspectors_.reserve(cached_animation_ids_.size());
    inspector_bounds_.resize(cached_animation_ids_.size());

    for (const auto& id : cached_animation_ids_) {
        auto inspector = std::make_unique<AnimationInspectorPanel>();
        inspector->set_document(document_);
        inspector->set_animation_id(id);
        if (preview_provider_) {
            inspector->set_preview_provider(preview_provider_);
        }
        if (inspector_configurator_) {
            inspector_configurator_(*inspector);
        }
        inspectors_.push_back(std::move(inspector));
    }

    scroll_offset_ = 0;
    layout_dirty_ = true;
}

void AnimationListPanel::layout_inspectors() {
    layout_dirty_ = false;

    if (inspectors_.empty()) {
        inspector_bounds_.clear();
        content_height_ = 0;
        return;
    }

    inspector_bounds_.resize(inspectors_.size());

    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::section_gap();
    const int width = std::max(0, bounds_.w - padding * 2);
    int column_count = width > 0 ? 2 : 1;
    int column_gap = column_count > 1 ? DMSpacing::item_gap() : 0;
    int column_width = width;
    if (column_count > 1) {
        column_width = (width - column_gap) / column_count;
        if (column_width <= 0) {
            column_count = 1;
            column_gap = 0;
            column_width = width;
        }
    }

    std::vector<int> heights(inspectors_.size(), 0);
    for (size_t i = 0; i < inspectors_.size(); ++i) {
        if (inspectors_[i]) {
            heights[i] = inspectors_[i]->height_for_width(column_width);
        }
    }

    size_t row_count = (inspectors_.size() + static_cast<size_t>(column_count) - 1) / static_cast<size_t>(column_count);
    std::vector<int> row_heights(row_count, 0);
    int content_y = padding;
    for (size_t row = 0; row < row_count; ++row) {
        int row_height = 0;
        for (int col = 0; col < column_count; ++col) {
            size_t index = row * static_cast<size_t>(column_count) + static_cast<size_t>(col);
            if (index >= inspectors_.size()) break;
            row_height = std::max(row_height, heights[index]);
        }
        row_heights[row] = row_height;
        content_y += row_height;
        if (row + 1 < row_count) {
            content_y += gap;
        }
    }

    content_height_ = content_y + padding;
    clamp_scroll();

    int y = padding;
    for (size_t row = 0; row < row_count; ++row) {
        for (int col = 0; col < column_count; ++col) {
            size_t index = row * static_cast<size_t>(column_count) + static_cast<size_t>(col);
            if (index >= inspectors_.size()) break;
            int x = bounds_.x + padding + col * (column_width + column_gap);
            SDL_Rect rect{x, bounds_.y + y - scroll_offset_, column_width, heights[index]};
            inspector_bounds_[index] = rect;
            if (inspectors_[index]) {
                inspectors_[index]->set_bounds(rect);
            }
        }
        y += row_heights[row];
        if (row + 1 < row_count) {
            y += gap;
        }
    }
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

}  // namespace animation_editor

