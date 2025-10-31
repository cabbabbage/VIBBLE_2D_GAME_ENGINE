#include "AnimationListPanel.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "AnimationDocument.hpp"
#include "PreviewProvider.hpp"
#include "string_utils.hpp"
#include "dm_icons.hpp"
#include "dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/widgets.hpp"
#include <nlohmann/json.hpp>

namespace {

constexpr int kRowHeight = 72;
// Slightly increase indent so children read as grouped under parents
constexpr int kIndentPerLevel = 16;
// Keep a sensible floor for very deep chains
constexpr float kMinSizeFactor = 0.60f;

float size_factor_for_level(int level) {
    if (level <= 0) {
        return 1.0f;
    }
    switch (level) {
        case 1:
            // SFA (derived) items render slightly smaller than SFF
            return 0.85f;
        case 2:
            return 0.75f;
        case 3:
            return 0.65f;
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

// --- Color helpers for SFF/SFA coloring ---

SDL_Color hsv_to_rgb(float hue, float saturation, float value) {
    hue = std::fmod(hue, 360.0f);
    if (hue < 0.0f) hue += 360.0f;
    saturation = std::clamp(saturation, 0.0f, 1.0f);
    value = std::clamp(value, 0.0f, 1.0f);

    const float chroma = value * saturation;
    const float h_prime = hue / 60.0f;
    const float x = chroma * (1.0f - std::fabs(std::fmod(h_prime, 2.0f) - 1.0f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (0.0f <= h_prime && h_prime < 1.0f) { r = chroma; g = x; }
    else if (1.0f <= h_prime && h_prime < 2.0f) { r = x; g = chroma; }
    else if (2.0f <= h_prime && h_prime < 3.0f) { g = chroma; b = x; }
    else if (3.0f <= h_prime && h_prime < 4.0f) { g = x; b = chroma; }
    else if (4.0f <= h_prime && h_prime < 5.0f) { r = x; b = chroma; }
    else { r = chroma; b = x; }

    const float m = value - chroma;
    auto to_channel = [m](float c) {
        c = std::clamp(c + m, 0.0f, 1.0f);
        return static_cast<Uint8>(std::lround(c * 255.0f));
    };
    return SDL_Color{to_channel(r), to_channel(g), to_channel(b), 230};
}

SDL_Color color_for_root_key(const std::string& key) {
    // Generate a vivid, diverse color for each root id, avoiding the orange range
    // so that orange can be reserved exclusively for selection state.
    // We derive a stable pseudo-random value from the key using a simple xorshift-like mix,
    // then map it to HSV while skipping the orange wedge (~20..45 degrees).

    auto mix64 = [](uint64_t x) {
        x += 0x9e3779b97f4a7c15ull; // golden ratio seed
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        x = x ^ (x >> 31);
        return x;
    };

    // Compute a 64-bit hash from the std::hash seed
    uint64_t h = static_cast<uint64_t>(std::hash<std::string>{}(key));
    h = mix64(h);

    auto u01 = [&](uint64_t bits, int shift) {
        // Produce a float in [0,1) from 24 bits
        uint32_t v = static_cast<uint32_t>((bits >> shift) & 0xFFFFFFull);
        return static_cast<float>(v) / static_cast<float>(0x1000000ull);
    };

    float r1 = u01(h, 0);
    float r2 = u01(h, 24);
    float r3 = u01(h, 48);

    // Base hue from r1 across [0,360)
    float hue = r1 * 360.0f;
    // If hue falls within orange wedge [20,45], remap it out of that range by shifting forward
    const float kOrangeMin = 20.0f;
    const float kOrangeMax = 45.0f;
    if (hue >= kOrangeMin && hue <= kOrangeMax) {
        // Push into a non-orange band while keeping distribution stable
        float span = kOrangeMax - kOrangeMin; // 25 deg
        hue = std::fmod(kOrangeMax + (hue - kOrangeMin) + 60.0f, 360.0f); // jump past orange by +60°
    }

    // Prefer vivid saturation/value ranges for stronger differentiation
    float saturation = 0.72f + 0.24f * r2; // 0.72..0.96
    saturation = std::clamp(saturation, 0.70f, 0.96f);
    float value = 0.78f + 0.18f * r3;      // 0.78..0.96
    value = std::clamp(value, 0.78f, 0.96f);

    return hsv_to_rgb(hue, saturation, value);
}

SDL_Color greyscale_of(SDL_Color c) {
    // luminance approximation
    int lum = static_cast<int>(std::lround(0.299f * c.r + 0.587f * c.g + 0.114f * c.b));
    lum = std::clamp(lum, 0, 255);
    return SDL_Color{static_cast<Uint8>(lum), static_cast<Uint8>(lum), static_cast<Uint8>(lum), c.a};
}

SDL_Color mix_color(SDL_Color a, SDL_Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](Uint8 x, Uint8 y) { return static_cast<Uint8>(std::lround((1.0f - t) * x + t * y)); };
    return SDL_Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

SDL_Color grey_variant_for_level(SDL_Color root, int level) {
    if (level <= 0) return root;
    // Increase greying with depth, clamped
    float t = 0.35f + 0.10f * static_cast<float>(level - 1); // 0.35 at level 1
    t = std::clamp(t, 0.0f, 0.6f);
    return mix_color(root, greyscale_of(root), t);
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

void AnimationListPanel::set_on_delete_animation(std::function<void(const std::string&)> callback) {
    on_delete_animation_ = std::move(callback);
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

        // Compute per-row base fill color from its root SFF id
        SDL_Color base = DMStyles::ListButton().bg;
        auto it_root = root_for_id_.find(row.id);
        if (it_root != root_for_id_.end()) {
            SDL_Color root_col = color_for_root_key(it_root->second);
            base = grey_variant_for_level(root_col, row.level);
        }
        SDL_Color fill = base;
        if (hovered) {
            fill = dm_draw::LightenColor(base, 0.08f);
        }
        // Keep the unique background color for selected items and use an orange outline to highlight.
        dm_draw::DrawBeveledRect(renderer, rect, DMStyles::CornerRadius(), DMStyles::BevelDepth(), fill, DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        SDL_Color border_col = dm_draw::DarkenColor(base, 0.45f);
        int border_thickness = 1;
        if (selected) {
            border_col = DMStyles::AccentButton().bg; // orange highlight
            border_thickness = 2;
        }
        dm_draw::DrawRoundedOutline(renderer, rect, DMStyles::CornerRadius(), border_thickness, border_col);

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

        // Draw delete button (X) in top-right corner
        const int delete_button_size = 16;
        const int delete_x = rect.x + rect.w - row_padding - delete_button_size;
        const int delete_y = rect.y + row_padding;
        SDL_Rect delete_rect{delete_x, delete_y, delete_button_size, delete_button_size};

        // Style the delete button - use DeleteButton style but smaller
        const DMButtonStyle& delete_style = DMStyles::DeleteButton();
        SDL_Color delete_bg = hovered_delete_row_ && *hovered_delete_row_ == i ? delete_style.hover_bg : delete_style.bg;
        dm_draw::DrawBeveledRect(renderer, delete_rect, DMStyles::CornerRadius(), 1, delete_bg,
                                DMStyles::HighlightColor(), DMStyles::ShadowColor(),
                                false, DMStyles::HighlightIntensity() * 0.5f, DMStyles::ShadowIntensity() * 0.5f);

        DMLabelStyle delete_label_style{delete_style.label.font_path, 12, delete_style.text};
        std::string delete_text{DMIcons::Close()};
        SDL_Point delete_size = DMFontCache::instance().measure_text(delete_label_style, delete_text);
        int delete_text_x = delete_rect.x + (delete_rect.w - delete_size.x) / 2;
        int delete_text_y = delete_rect.y + (delete_rect.h - delete_size.y) / 2;
        DMFontCache::instance().draw_text(renderer, delete_label_style, delete_text, delete_text_x, delete_text_y);

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
            hovered_delete_row_.reset();
            return false;
        }
        hovered_row_ = row_index_at_point(p);

        // Check if hovering over delete button
        hovered_delete_row_.reset();
        if (hovered_row_) {
            const SDL_Rect& rect = row_bounds_[*hovered_row_];
            const int delete_button_size = 16;
            const int delete_x = rect.x + rect.w - DMSpacing::small_gap() - delete_button_size;
            const int delete_y = rect.y + DMSpacing::small_gap();
            SDL_Rect delete_rect{delete_x, delete_y, delete_button_size, delete_button_size};
            if (SDL_PointInRect(&p, &delete_rect)) {
                hovered_delete_row_ = *hovered_row_;
            }
        }

        return hovered_row_.has_value() || hovered_delete_row_.has_value();
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

        // Check if clicking on delete button (left click only)
        if (e.button.button == SDL_BUTTON_LEFT) {
            const SDL_Rect& rect = row_bounds_[*index];
            const int delete_button_size = 16;
            const int delete_x = rect.x + rect.w - DMSpacing::small_gap() - delete_button_size;
            const int delete_y = rect.y + DMSpacing::small_gap();
            SDL_Rect delete_rect{delete_x, delete_y, delete_button_size, delete_button_size};
            if (SDL_PointInRect(&p, &delete_rect)) {
                // Clicked on delete button - call delete callback
                if (on_delete_animation_) {
                    on_delete_animation_(animation_id);
                }
                return true;
            }

            // Clicked on row - select it
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

    // Rebuild mapping from id -> root (top-level SFF)
    root_for_id_.clear();

    std::function<void(const std::string&, int, const std::string&)> visit = [&](const std::string& id, int level, const std::string& root_id) {
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
        root_for_id_[id] = root_id;
        DisplayRow row;
        row.id = id;
        row.level = level;
        row.missing_source = (!info.parent.has_value() && info.missing_source);
        flattened.push_back(row);
        for (const auto& child : info.children) {
            visit(child, level + 1, root_id);
        }
    };

    for (const auto& root : roots) {
        visit(root, 0, root);
    }

    for (const auto& entry : nodes) {
        if (visited.count(entry.first) == 0) {
            visit(entry.first, 0, entry.first);
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
    const int base_width = std::max(0, bounds_.w - padding * 2);

    row_bounds_.assign(display_rows_.size(), SDL_Rect{});

    int y = bounds_.y + padding - scroll_offset_;
    for (size_t i = 0; i < display_rows_.size(); ++i) {
        int level = display_rows_[i].level;
        int row_height = row_height_for_level(level);
        // Scale width for derived (smaller) animations so they shrink horizontally as well.
        const float width_factor = size_factor_for_level(level);
        int row_width = std::max(1, static_cast<int>(std::round(base_width * width_factor)));
        SDL_Rect rect{bounds_.x + padding, y, row_width, row_height};
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
