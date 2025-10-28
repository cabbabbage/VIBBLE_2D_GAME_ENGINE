#include "AnimationListPanel.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "AnimationDocument.hpp"
#include "PreviewProvider.hpp"
#include "string_utils.hpp"
#include "dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/widgets.hpp"
#include <nlohmann/json.hpp>

namespace {

constexpr int kRowHeight = 72;
constexpr int kIndentPerLevel = 12;
constexpr float kMinSizeFactor = 0.45f;

float size_factor_for_level(int level) {
    if (level <= 0) {
        return 1.0f;
    }
    switch (level) {
        case 1:
            return 0.8f;
        case 2:
            return 0.65f;
        case 3:
            return 0.55f;
        default:
            return kMinSizeFactor;
    }
}

int row_height_for_level(int level) {
    float factor = size_factor_for_level(level);
    int height = static_cast<int>(std::round(kRowHeight * factor));
    return std::max(1, height);
}

int indent_for_level(int level) {
    if (level <= 0) {
        return 0;
    }
    return level * kIndentPerLevel;
}

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

}

namespace animation_editor {

AnimationListPanel::AnimationListPanel() = default;

void AnimationListPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    rebuild_rows();
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
    rebuild_rows();
    if (layout_dirty_) {
        layout_rows();
    }
}

void AnimationListPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect(renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

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
    const SDL_Color hover_bg = style.hover_bg;
    const SDL_Color selected_bg = style.press_bg;
    const SDL_Color idle_bg = style.bg;

    const int row_padding = DMSpacing::small_gap();

    for (size_t i = 0; i < display_rows_.size(); ++i) {
        const SDL_Rect& rect = row_bounds_.at(i);
        if (!rects_intersect(rect, bounds_)) {
            continue;
        }

        const DisplayRow& row = display_rows_[i];
        const float size_factor = size_factor_for_level(row.level);
        const bool selected = selected_animation_id_ && *selected_animation_id_ == row.id;
        const bool hovered = hovered_row_ && *hovered_row_ == i;
        const SDL_Color fill = selected ? selected_bg : (hovered ? hover_bg : idle_bg);

        dm_draw::DrawBeveledRect(renderer, rect, DMStyles::CornerRadius(), DMStyles::BevelDepth(), fill, DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline(renderer, rect, DMStyles::CornerRadius(), 1, border);

        int content_x = rect.x + row_padding + indent_for_level(row.level);
        int content_y = rect.y + row_padding;
        const int content_h = rect.h - row_padding * 2;

        if (preview_provider_) {
            SDL_Texture* texture = preview_provider_->get_preview_texture(renderer, row.id);
            if (texture) {
                const int thumb_size = content_h;
                SDL_Rect thumb_rect{content_x, rect.y + (rect.h - thumb_size) / 2, thumb_size, thumb_size};
                int tex_w = 0;
                int tex_h = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
                if (tex_w > 0 && tex_h > 0) {
                    float scale = std::min(static_cast<float>(thumb_rect.w) / static_cast<float>(tex_w), static_cast<float>(thumb_rect.h) / static_cast<float>(tex_h));
                    int draw_w = std::max(1, static_cast<int>(tex_w * scale));
                    int draw_h = std::max(1, static_cast<int>(tex_h * scale));
                    SDL_Rect dst{thumb_rect.x + (thumb_rect.w - draw_w) / 2,
                                 thumb_rect.y + (thumb_rect.h - draw_h) / 2, draw_w, draw_h};
                    SDL_RenderCopy(renderer, texture, nullptr, &dst);
                }
                content_x += thumb_rect.w + row_padding;
            }
        }

        DMLabelStyle label_style = DMStyles::Label();
        label_style.color = style.label.color;
        label_style.font_size = std::max(1, static_cast<int>(std::round(label_style.font_size * size_factor)));
        DMFontCache::instance().draw_text(renderer, label_style, row.id, content_x, content_y);

        std::vector<std::pair<const DMButtonStyle*, std::string>> badges;
        if (row.missing_source) {
            badges.emplace_back(&DMStyles::DeleteButton(), std::string{"(missing source)"});
        }
        if (start_animation_id_ && *start_animation_id_ == row.id) {
            badges.emplace_back(&DMStyles::AccentButton(), std::string{"START"});
        }

        int badge_x = rect.x + rect.w - row_padding;
        const int badge_padding = std::max(1, static_cast<int>(std::round(DMSpacing::small_gap() * size_factor)));
        for (auto it = badges.rbegin(); it != badges.rend(); ++it) {
            const DMButtonStyle* badge_style = it->first;
            DMLabelStyle badge_label = badge_style->label;
            badge_label.font_size = std::max(1, static_cast<int>(std::round(std::max(1, badge_label.font_size - 2) * size_factor)));
            SDL_Point badge_size = DMFontCache::instance().measure_text(badge_label, it->second);
            int badge_width = badge_size.x + badge_padding * 2;
            int badge_height = badge_size.y + badge_padding * 2;
            badge_x -= badge_width;
            int min_badge_x = content_x + badge_padding;
            if (badge_x < min_badge_x) {
                badge_x = min_badge_x;
            }
            SDL_Rect badge_rect{badge_x, rect.y + std::max(0, (rect.h - badge_height) / 2), badge_width, badge_height};
            dm_draw::DrawBeveledRect(renderer, badge_rect, DMStyles::CornerRadius(), DMStyles::BevelDepth(), badge_style->bg, DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
            dm_draw::DrawRoundedOutline(renderer, badge_rect, DMStyles::CornerRadius(), 1, badge_style->border);
            DMFontCache::instance().draw_text(renderer, badge_label, it->second, badge_rect.x + badge_padding, badge_rect.y + (badge_rect.h - badge_size.y) / 2);
            badge_x -= badge_padding;
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

        const std::string& animation_id = display_rows_.at(*index).id;
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

void AnimationListPanel::rebuild_rows() {
    if (!document_) {
        if (!display_rows_.empty()) {
            display_rows_.clear();
            row_bounds_.clear();
            scroll_offset_ = 0;
            content_height_ = 0;
            hovered_row_.reset();
            layout_dirty_ = true;
        }
        start_animation_id_.reset();
        return;
    }

    start_animation_id_ = document_->start_animation();

    auto ids = document_->animation_ids();
    std::unordered_set<std::string> id_set(ids.begin(), ids.end());

    struct NodeInfo {
        std::string id;
        std::optional<std::string> parent;
        bool missing_source = false;
        std::vector<std::string> children;
    };

    std::unordered_map<std::string, NodeInfo> nodes;
    nodes.reserve(ids.size());

    for (const auto& id : ids) {
        NodeInfo node;
        node.id = id;

        bool missing_parent = false;
        std::optional<std::string> parent;

        if (auto payload_text = document_->animation_payload(id)) {
            nlohmann::json payload = nlohmann::json::parse(*payload_text, nullptr, false);
            if (payload.is_object() && payload.contains("source") && payload["source"].is_object()) {
                const nlohmann::json& source = payload["source"];
                std::string kind = source.value("kind", std::string{});
                if (kind == std::string{"animation"}) {
                    std::string candidate;
                    if (source.contains("name") && source["name"].is_string()) {
                        candidate = strings::trim_copy(source["name"].get<std::string>());
                    }
                    if (candidate.empty()) {
                        candidate = strings::trim_copy(source.value("path", std::string{}));
                    }
                    if (!candidate.empty()) {
                        if (candidate == id) {
                            missing_parent = true;
                        } else if (id_set.count(candidate) > 0) {
                            parent = candidate;
                        } else {
                            missing_parent = true;
                        }
                    }
                }
            }
        }

        node.parent = parent;
        node.missing_source = missing_parent;
        nodes.emplace(id, std::move(node));
    }

    for (auto& entry : nodes) {
        if (entry.second.parent) {
            auto it = nodes.find(*entry.second.parent);
            if (it != nodes.end()) {
                it->second.children.push_back(entry.first);
            }
        }
    }

    for (auto& entry : nodes) {
        std::sort(entry.second.children.begin(), entry.second.children.end());
    }

    std::vector<std::string> roots;
    roots.reserve(nodes.size());
    for (const auto& entry : nodes) {
        const NodeInfo& node = entry.second;
        if (!node.parent || nodes.find(*node.parent) == nodes.end()) {
            roots.push_back(entry.first);
        }
    }
    std::sort(roots.begin(), roots.end());

    std::vector<DisplayRow> flattened;
    flattened.reserve(nodes.size());

    std::unordered_set<std::string> visited;
    visited.reserve(nodes.size());

    std::function<void(const std::string&, int)> visit = [&](const std::string& id, int level) {
        if (visited.count(id) != 0) {
            return;
        }
        auto it = nodes.find(id);
        if (it == nodes.end()) {
            visited.insert(id);
            return;
        }
        visited.insert(id);
        const NodeInfo& info = it->second;
        DisplayRow row;
        row.id = id;
        row.level = level;
        row.missing_source = (!info.parent.has_value() && info.missing_source);
        flattened.push_back(row);
        for (const auto& child : info.children) {
            visit(child, level + 1);
        }
    };

    for (const auto& root : roots) {
        visit(root, 0);
    }

    for (const auto& entry : nodes) {
        if (visited.count(entry.first) == 0) {
            visit(entry.first, 0);
        }
    }

    bool changed = flattened.size() != display_rows_.size();
    if (!changed) {
        for (size_t i = 0; i < flattened.size(); ++i) {
            if (flattened[i].id != display_rows_[i].id ||
                flattened[i].level != display_rows_[i].level ||
                flattened[i].missing_source != display_rows_[i].missing_source) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        display_rows_ = std::move(flattened);
        row_bounds_.assign(display_rows_.size(), SDL_Rect{});
        layout_dirty_ = true;
        hovered_row_.reset();
    }

    if (selected_animation_id_) {
        auto it = std::find_if(display_rows_.begin(), display_rows_.end(), [&](const DisplayRow& row) {
            return row.id == *selected_animation_id_;
        });
        if (it == display_rows_.end()) {
            selected_animation_id_.reset();
            if (on_selection_changed_) {
                on_selection_changed_(std::nullopt);
            }
        }
    }
}

void AnimationListPanel::layout_rows() {
    layout_dirty_ = false;

    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::small_gap();
    const int width = std::max(0, bounds_.w - padding * 2);

    row_bounds_.assign(display_rows_.size(), SDL_Rect{});

    int y = bounds_.y + padding - scroll_offset_;
    for (size_t i = 0; i < display_rows_.size(); ++i) {
        int row_height = row_height_for_level(display_rows_[i].level);
        SDL_Rect rect{bounds_.x + padding, y, width, row_height};
        row_bounds_[i] = rect;
        y += row_height + gap;
    }

    content_height_ = padding * 2;
    if (!display_rows_.empty()) {
        int total_height = 0;
        for (const auto& row : display_rows_) {
            total_height += row_height_for_level(row.level);
        }
        content_height_ += total_height;
        content_height_ += gap * static_cast<int>(display_rows_.size() - 1);
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
    auto it = std::find_if(display_rows_.begin(), display_rows_.end(), [&](const DisplayRow& row) {
        return row.id == *selected_animation_id_;
    });
    if (it == display_rows_.end()) {
        return;
    }
    size_t index = static_cast<size_t>(std::distance(display_rows_.begin(), it));
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

}
