#include "CandidateEditorPieGraphWidget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <utility>
#include <vector>

#include <SDL_ttf.h>
#include <nlohmann/json.hpp>

#include "../../search_assets.hpp"
#include "../../dm_styles.hpp"
#include "../../draw_utils.hpp"
#include "../../../utils/input.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kStartAngle = -1.5707963267948966;
constexpr int kSearchPanelWidth = 280;
constexpr int kSearchPanelHeight = 320;

double clamp_positive(double value) {
    return value < 0.0 ? 0.0 : value;
}
}

CandidateEditorPieGraphWidget::CandidateEditorPieGraphWidget() {
    rect_ = SDL_Rect{0, 0, 280, 180};
}

void CandidateEditorPieGraphWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    position_search_within_bounds();
}

const SDL_Rect& CandidateEditorPieGraphWidget::rect() const {
    return rect_;
}

int CandidateEditorPieGraphWidget::height_for_width(int w) const {
    int constrained = std::clamp(w, 160, 420);
    return std::max(constrained, rect_.h > 0 ? rect_.h : 180);
}

bool CandidateEditorPieGraphWidget::handle_event(const SDL_Event& e) {
    if (search_visible()) {
        bool used = search_assets_->handle_event(e);
        bool should_close = false;

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point point{e.button.x, e.button.y};
            if (!search_assets_->is_point_inside(point.x, point.y)) {
                should_close = true;
            }
        } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            should_close = true;
        }

        if (should_close) {
            hide_search();
            return true;
        }

        if (used || e.type == SDL_TEXTINPUT || e.type == SDL_KEYDOWN || e.type == SDL_KEYUP ||
            e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION ||
            e.type == SDL_MOUSEWHEEL) {
            return true;
        }
    }

    auto release_scroll_capture = [this]() {
        if (scroll_capture_active_) {
            DMWidgetsSetSliderScrollCapture(this, false);
            scroll_capture_active_ = false;
        }
    };

    if (candidates_.empty()) {
        hovered_index_ = -1;
        release_scroll_capture();
        return false;
    }

    if (e.type == SDL_MOUSEMOTION) {
        Layout layout = compute_layout();
        double total = total_weight();
        if (total <= 0.0) {
            total = 1.0;
        }

        SDL_Point point{e.motion.x, e.motion.y};
        if (!SDL_PointInRect(&point, &rect_)) {
            if (hovered_index_ != -1) {
                hovered_index_ = -1;
                return true;
            }
            return false;
        }

        int new_hover = hit_test_candidate(layout, point, total);
        if (new_hover != hovered_index_) {
            hovered_index_ = new_hover;
            return true;
        }
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        Layout layout = compute_layout();
        double total = total_weight();
        if (total <= 0.0) {
            total = 1.0;
        }

        SDL_Point point{e.button.x, e.button.y};
        int target_index = -1;
        if (SDL_PointInRect(&point, &rect_)) {
            target_index = hit_test_candidate(layout, point, total);
        } else if (hovered_index_ != -1) {
            hovered_index_ = -1;
            return true;
        }

        if (target_index >= 0) {
            hovered_index_ = target_index;
            if (e.button.clicks >= 2) {
                if (on_delete_) {
                    on_delete_(target_index);
                }
                active_index_ = -1;
                hovered_index_ = -1;
                release_scroll_capture();
                return true;
            }

            if (active_index_ != target_index) {
                active_index_ = target_index;
                if (!scroll_capture_active_) {
                    DMWidgetsSetSliderScrollCapture(this, true);
                    scroll_capture_active_ = true;
                }
            } else {
                active_index_ = -1;
                release_scroll_capture();
            }
            return true;
        }

        if (active_index_ != -1) {
            active_index_ = -1;
            release_scroll_capture();
            return true;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        if (active_index_ >= 0 && on_adjust_) {
            int delta = e.wheel.y; // positive up, negative down
            if (delta != 0) {
                on_adjust_(active_index_, delta);
                return true; // prevent container scrolling while focused
            }
        }
    }

    if (active_index_ == -1) {
        release_scroll_capture();
    }

    return false;
}

int CandidateEditorPieGraphWidget::hit_test_candidate(const Layout& layout, SDL_Point point, double total) const {
    if (!SDL_PointInRect(&point, &rect_)) {
        return -1;
    }

    for (size_t i = 0; i < legend_row_rects_.size(); ++i) {
        const SDL_Rect& row = legend_row_rects_[i];
        if (row.w <= 0 || row.h <= 0) {
            continue;
        }
        if (SDL_PointInRect(&point, &row)) {
            return static_cast<int>(i);
        }
    }

    if (layout.radius <= 0.0f) {
        return -1;
    }

    const float dx = static_cast<float>(point.x) - layout.center.x;
    const float dy = static_cast<float>(point.y) - layout.center.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > layout.radius + 12.0f) {
        return -1;
    }

    double angle = std::atan2(dy, dx);
    double normalized = angle - kStartAngle;
    while (normalized < 0.0) normalized += kTwoPi;
    while (normalized >= kTwoPi) normalized -= kTwoPi;

    double used = 0.0;
    for (size_t i = 0; i < candidates_.size(); ++i) {
        const double weight = clamp_positive(candidates_[i].weight);
        double portion = total > 0.0 ? weight / total : 0.0;
        double sweep = portion * kTwoPi;
        if (i + 1 == candidates_.size()) {
            sweep = kTwoPi - used;
        }
        if (sweep <= 0.0) {
            used += sweep;
            continue;
        }
        if (normalized >= used && normalized <= used + sweep) {
            return static_cast<int>(i);
        }
        used += sweep;
    }

    return -1;
}

void CandidateEditorPieGraphWidget::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    draw_background(renderer);

    Layout layout = compute_layout();
    int font_size = std::max(11, DMStyles::Label().font_size - 1);
    TTF_Font* font = TTF_OpenFont(DMStyles::Label().font_path.c_str(), font_size);

    if (candidates_.empty() || layout.radius <= 0.0f) {
        render_empty(renderer, layout, font);
        if (font) {
            TTF_CloseFont(font);
        }
        return;
    }

    double total = total_weight();
    if (total <= 0.0) {
        total = std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
                                [](double acc, const CandidateInfo& info) {
                                    return acc + clamp_positive(info.weight);
                                });
        if (total <= 0.0) {
            total = 1.0;
        }
    }

    render_slices(renderer, layout, total);
    render_outline(renderer, layout);
    render_legend(renderer, layout, total, font);

    if (font) {
        TTF_CloseFont(font);
    }

    if (search_visible()) {
        SDL_Rect previous_clip;
        SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
        if (had_clip) {
            SDL_RenderGetClipRect(renderer, &previous_clip);
        }
        SDL_RenderSetClipRect(renderer, &rect_);
        search_assets_->render(renderer);
        if (had_clip) {
            SDL_RenderSetClipRect(renderer, &previous_clip);
        } else {
            SDL_RenderSetClipRect(renderer, nullptr);
        }
    }
}

void CandidateEditorPieGraphWidget::set_weights(std::vector<float> weights) {
    std::vector<CandidateInfo> info;
    info.reserve(weights.size());
    for (size_t i = 0; i < weights.size(); ++i) {
        CandidateInfo entry;
        entry.name = "Candidate " + std::to_string(i + 1);
        entry.weight = clamp_positive(static_cast<double>(weights[i]));
        info.push_back(std::move(entry));
    }
    candidates_ = std::move(info);
    hovered_index_ = -1;
    legend_row_rects_.clear();
    legend_row_height_ = 0;
}

void CandidateEditorPieGraphWidget::set_candidates_from_json(const nlohmann::json& entry) {
    std::vector<CandidateInfo> info;
    if (entry.is_object() && entry.contains("candidates") && entry["candidates"].is_array()) {
        const auto& candidates = entry["candidates"];
        info.reserve(candidates.size());
        size_t unnamed_index = 1;
        for (const auto& candidate : candidates) {
            CandidateInfo parsed;
            parsed.name = "Candidate " + std::to_string(unnamed_index++);
            parsed.weight = 1.0;
            if (candidate.is_object()) {
                if (candidate.contains("name") && candidate["name"].is_string()) {
                    parsed.name = candidate["name"].get<std::string>();
                }
                if (candidate.contains("weight")) {
                    const auto& value = candidate["weight"];
                    if (value.is_number_float()) {
                        parsed.weight = value.get<double>();
                    } else if (value.is_number_integer()) {
                        parsed.weight = static_cast<double>(value.get<int>());
                    }
                } else if (candidate.contains("chance")) {
                    const auto& value = candidate["chance"];
                    if (value.is_number_float()) {
                        parsed.weight = value.get<double>();
                    } else if (value.is_number_integer()) {
                        parsed.weight = static_cast<double>(value.get<int>());
                    }
                }
            } else if (candidate.is_number_float()) {
                parsed.weight = candidate.get<double>();
            } else if (candidate.is_number_integer()) {
                parsed.weight = static_cast<double>(candidate.get<int>());
            }
            parsed.weight = clamp_positive(parsed.weight);
            info.push_back(std::move(parsed));
        }
    }

    candidates_ = std::move(info);
    hovered_index_ = -1;
    legend_row_rects_.clear();
    legend_row_height_ = 0;
    if (active_index_ >= static_cast<int>(candidates_.size())) {
        active_index_ = -1;
        if (scroll_capture_active_) {
            DMWidgetsSetSliderScrollCapture(this, false);
            scroll_capture_active_ = false;
        }
    }
}

void CandidateEditorPieGraphWidget::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    if (search_assets_) {
        search_assets_->set_screen_dimensions(screen_w_, screen_h_);
        position_search_within_bounds();
    }
}

void CandidateEditorPieGraphWidget::show_search(const SDL_Rect& anchor_rect, std::function<void(const std::string&)> on_select) {
    ensure_search_created();
    last_search_anchor_ = anchor_rect;
    has_search_anchor_ = true;
    position_search_within_bounds();
    search_assets_->open([this, cb = std::move(on_select)](const std::string& value) {
        if (cb) {
            cb(value);
        }
        hide_search();
    });
    search_visible_previous_ = search_visible();
    notify_layout_change();
}

void CandidateEditorPieGraphWidget::hide_search() {
    if (!search_assets_) {
        return;
    }
    const bool was_visible = search_assets_->visible();
    search_assets_->close();
    if (was_visible) {
        search_visible_previous_ = false;
        notify_layout_change();
    }
}

void CandidateEditorPieGraphWidget::update_search(const Input& input) {
    if (!search_assets_) {
        return;
    }
    search_assets_->set_screen_dimensions(screen_w_, screen_h_);
    if (search_assets_->visible()) {
        position_search_within_bounds();
        search_assets_->update(input);
    }
    bool visible_now = search_assets_->visible();
    if (visible_now != search_visible_previous_) {
        search_visible_previous_ = visible_now;
        notify_layout_change();
    }
}

CandidateEditorPieGraphWidget::Layout CandidateEditorPieGraphWidget::compute_layout() const {
    Layout layout;
    layout.center = SDL_FPoint{static_cast<float>(rect_.x + rect_.w / 2),
                               static_cast<float>(rect_.y + rect_.h / 2)};
    layout.radius = 0.0f;
    layout.legend = SDL_Rect{0, 0, 0, 0};

    if (rect_.w <= 0 || rect_.h <= 0) {
        return layout;
    }

    const int margin = DMSpacing::item_gap();
    int legend_width = 0;
    if (rect_.w >= 320) {
        legend_width = std::max(120, rect_.w / 3);
    }

    const int pie_width = std::max(0, rect_.w - margin * 2 - (legend_width > 0 ? legend_width + margin : 0));
    const int pie_height = std::max(0, rect_.h - margin * 2);

    const int pie_x = rect_.x + margin;
    const int pie_y = rect_.y + margin;

    layout.center = SDL_FPoint{static_cast<float>(pie_x + pie_width / 2),
                               static_cast<float>(pie_y + pie_height / 2)};
    layout.radius = static_cast<float>(std::max(0, std::min(pie_width, pie_height))) * 0.5f - 6.0f;
    if (layout.radius < 0.0f) {
        layout.radius = 0.0f;
    }

    if (legend_width > 0) {
        layout.legend = SDL_Rect{rect_.x + rect_.w - legend_width - margin,
                                 rect_.y + margin,
                                 legend_width,
                                 std::max(0, rect_.h - margin * 2)};
    }

    cache_legend_rows(layout);

    return layout;
}

double CandidateEditorPieGraphWidget::total_weight() const {
    return std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
                           [](double acc, const CandidateInfo& info) {
                               return acc + clamp_positive(info.weight);
                           });
}

void CandidateEditorPieGraphWidget::cache_legend_rows(const Layout& layout, int row_height) const {
    legend_row_rects_.assign(candidates_.size(), SDL_Rect{0, 0, 0, 0});

    if (layout.legend.w <= 60 || layout.legend.h <= 0 || candidates_.empty()) {
        if (row_height > 0) {
            legend_row_height_ = row_height;
        } else if (legend_row_height_ <= 0) {
            legend_row_height_ = default_legend_row_height();
        }
        return;
    }

    int effective_height = row_height;
    if (effective_height <= 0) {
        effective_height = legend_row_height_ > 0 ? legend_row_height_ : default_legend_row_height();
    } else {
        legend_row_height_ = row_height;
    }

    int y = layout.legend.y;
    const int bottom = layout.legend.y + layout.legend.h;
    for (size_t i = 0; i < candidates_.size(); ++i) {
        if (y + effective_height > bottom) {
            break;
        }
        legend_row_rects_[i] = SDL_Rect{layout.legend.x, y, layout.legend.w, effective_height};
        y += effective_height;
    }
}

int CandidateEditorPieGraphWidget::default_legend_row_height() {
    return std::max(DMStyles::Label().font_size + 6, 20);
}

void CandidateEditorPieGraphWidget::draw_background(SDL_Renderer* renderer) const {
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 200);
    SDL_RenderFillRect(renderer, &rect_);
}

void CandidateEditorPieGraphWidget::render_empty(SDL_Renderer* renderer, const Layout& layout, TTF_Font* font) const {
    SDL_FPoint center = layout.center;
    float radius = layout.radius;
    if (radius <= 0.0f) {
        center = SDL_FPoint{static_cast<float>(rect_.x + rect_.w / 2),
                            static_cast<float>(rect_.y + rect_.h / 2)};
        radius = static_cast<float>(std::max(16, std::min(rect_.w, rect_.h) / 2 - 8));
    }

    const int segments = 64;
    std::vector<SDL_Point> outline;
    outline.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        double t = kStartAngle + kTwoPi * (static_cast<double>(i) / segments);
        outline.push_back(SDL_Point{static_cast<int>(std::round(center.x + radius * std::cos(t))),
                                    static_cast<int>(std::round(center.y + radius * std::sin(t)))});
    }

    SDL_Color border = DMStyles::Border();
    border.a = 220;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    if (!outline.empty()) {
        SDL_RenderDrawLines(renderer, outline.data(), static_cast<int>(outline.size()));
    }

    const SDL_Color text_color = DMStyles::Label().color;
    draw_text(renderer, font, "No candidates configured", static_cast<int>(std::round(center.x)), static_cast<int>(std::round(center.y)), text_color, true);
}

void CandidateEditorPieGraphWidget::render_slices(SDL_Renderer* renderer, const Layout& layout, double total) const {
    if (layout.radius <= 0.0f) {
        return;
    }

    double angle = kStartAngle;
    double used = 0.0;

    for (size_t i = 0; i < candidates_.size(); ++i) {
        const double weight = clamp_positive(candidates_[i].weight);
        double portion = total > 0.0 ? weight / total : 0.0;
        double sweep = portion * kTwoPi;
        if (i + 1 == candidates_.size()) {
            sweep = kTwoPi - used;
        }
        if (sweep <= 0.0) {
            used += sweep;
            angle += sweep;
            continue;
        }

        const bool is_hovered = static_cast<int>(i) == hovered_index_;
        const bool is_active = static_cast<int>(i) == active_index_;
        SDL_Color color = color_for_index(i);
        if (is_active) {
            color = lighten(color, 0.12f);
        }
        if (is_hovered) {
            color = lighten(color, 0.25f);
        }
        float slice_radius = layout.radius;
        if (is_active) {
            slice_radius += 4.0f;
        }
        if (is_hovered) {
            slice_radius += 6.0f;
        }
        int segments = std::max(6, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 32.0))));

#if SDL_VERSION_ATLEAST(2,0,18)
        std::vector<SDL_Vertex> verts;
        verts.reserve(segments + 2);
        SDL_Vertex center_vert{};
        center_vert.position = SDL_FPoint{layout.center.x, layout.center.y};
        center_vert.color = color;
        verts.push_back(center_vert);
        for (int s = 0; s <= segments; ++s) {
            double t = angle + sweep * (static_cast<double>(s) / segments);
            SDL_Vertex v{};
            v.position = SDL_FPoint{layout.center.x + slice_radius * static_cast<float>(std::cos(t)),
                                    layout.center.y + slice_radius * static_cast<float>(std::sin(t))};
            v.color = color;
            verts.push_back(v);
        }
        std::vector<int> idxs;
        idxs.reserve(segments * 3);
        for (int s = 1; s <= segments; ++s) {
            idxs.push_back(0);
            idxs.push_back(s);
            idxs.push_back(s + 1);
        }
        SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idxs.data(), static_cast<int>(idxs.size()));
#else
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        for (int s = 0; s <= segments; ++s) {
            double t = angle + sweep * (static_cast<double>(s) / segments);
            SDL_RenderDrawLine(renderer, static_cast<int>(std::round(layout.center.x)), static_cast<int>(std::round(layout.center.y)), static_cast<int>(std::round(layout.center.x + slice_radius * std::cos(t))), static_cast<int>(std::round(layout.center.y + slice_radius * std::sin(t))));
        }
#endif

        used += sweep;
        angle += sweep;
    }
}

void CandidateEditorPieGraphWidget::render_outline(SDL_Renderer* renderer, const Layout& layout) const {
    if (layout.radius <= 0.0f) {
        return;
    }

    const int outline_segments = 96;
    std::vector<SDL_Point> outline;
    outline.reserve(outline_segments + 1);
    float outline_radius = layout.radius + 6.0f;
    for (int i = 0; i <= outline_segments; ++i) {
        double t = kStartAngle + kTwoPi * (static_cast<double>(i) / outline_segments);
        outline.push_back(SDL_Point{static_cast<int>(std::round(layout.center.x + outline_radius * std::cos(t))),
                                    static_cast<int>(std::round(layout.center.y + outline_radius * std::sin(t)))});
    }

    SDL_Color border = DMStyles::Border();
    border.a = 220;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    if (!outline.empty()) {
        SDL_RenderDrawLines(renderer, outline.data(), static_cast<int>(outline.size()));
    }
}

void CandidateEditorPieGraphWidget::render_legend(SDL_Renderer* renderer, const Layout& layout, double total, TTF_Font* font) const {
    if (!font) {
        return;
    }

    if (layout.legend.w > 60) {
        SDL_Color text_color = DMStyles::Label().color;
        int font_height = TTF_FontHeight(font);
        int row_height = std::max(font_height + 6, 20);
        cache_legend_rows(layout, row_height);
        for (size_t i = 0; i < candidates_.size(); ++i) {
            const SDL_Rect& row_rect = i < legend_row_rects_.size() ? legend_row_rects_[i] : SDL_Rect{0, 0, 0, 0};
            if (row_rect.w <= 0 || row_rect.h <= 0) {
                break;
            }

            const bool is_hovered = static_cast<int>(i) == hovered_index_;
            const bool is_active = static_cast<int>(i) == active_index_;
            if (is_hovered || is_active) {
                SDL_Color row_bg = DMStyles::PanelHeader();
                Uint8 alpha = static_cast<Uint8>(is_active && is_hovered ? 220 : (is_active ? 200 : 170));
                SDL_SetRenderDrawColor(renderer, row_bg.r, row_bg.g, row_bg.b, alpha);
                SDL_RenderFillRect(renderer, &row_rect);
            }

            SDL_Color swatch = color_for_index(i);
            if (is_active) {
                swatch = lighten(swatch, 0.12f);
            }
            if (is_hovered) {
                swatch = lighten(swatch, 0.25f);
            }

            SDL_Rect box{row_rect.x, row_rect.y + std::max(0, (row_rect.h - 16) / 2), 16, 16};
            const int radius = std::min(DMStyles::CornerRadius(), std::min(box.w, box.h) / 2);
            const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(box.w, box.h) / 2));
            dm_draw::DrawBeveledRect(
                renderer,
                box,
                radius,
                bevel,
                swatch,
                swatch,
                swatch,
                false,
                0.0f,
                0.0f);
            SDL_Color outline_color = DMStyles::Border();
            outline_color.a = 255;
            dm_draw::DrawRoundedOutline(
                renderer,
                box,
                radius,
                1,
                outline_color);

            double percent = total > 0.0 ? (clamp_positive(candidates_[i].weight) / total) * 100.0 : 0.0;
            std::ostringstream label;
            label << candidates_[i].name << " - " << std::fixed << std::setprecision(1) << percent << "% (" << static_cast<int>(std::round(clamp_positive(candidates_[i].weight))) << ")";
            draw_text(renderer, font, label.str(), box.x + box.w + 8, row_rect.y + (row_rect.h - font_height) / 2, text_color, false);
        }
    } else {
        std::ostringstream summary;
        summary << "Total weight: " << static_cast<int>(std::round(total));
        draw_text(renderer, font, summary.str(), rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap(), DMStyles::Label().color, false);
        cache_legend_rows(layout, 0);
    }
}

SDL_Rect CandidateEditorPieGraphWidget::draw_text(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                                                  int x, int y, SDL_Color color, bool center) const {
    SDL_Rect dst{x, y, 0, 0};
    if (!renderer || !font) {
        return dst;
    }

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) {
        return dst;
    }

    dst.w = surf->w;
    dst.h = surf->h;
    if (center) {
        dst.x -= dst.w / 2;
        dst.y -= dst.h / 2;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    return dst;
}

SDL_Color CandidateEditorPieGraphWidget::color_for_index(size_t index) {
    static constexpr std::array<SDL_Color, 10> kPalette{{
        SDL_Color{0xED, 0x6A, 0x5A, 0xFF},
        SDL_Color{0x5A, 0xC8, 0xED, 0xFF},
        SDL_Color{0x9C, 0xED, 0x5A, 0xFF},
        SDL_Color{0xF2, 0xC9, 0x2C, 0xFF},
        SDL_Color{0xAE, 0x79, 0xED, 0xFF},
        SDL_Color{0xED, 0xA0, 0x5A, 0xFF},
        SDL_Color{0x4C, 0xAF, 0x50, 0xFF},
        SDL_Color{0xFF, 0x99, 0xCC, 0xFF},
        SDL_Color{0xFF, 0xB7, 0x4D, 0xFF},
        SDL_Color{0x64, 0x95, 0xED, 0xFF},
    }};

    return kPalette[index % kPalette.size()];
}

SDL_Color CandidateEditorPieGraphWidget::lighten(SDL_Color color, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    int r = static_cast<int>(std::round(color.r + (255.0f - color.r) * amount));
    int g = static_cast<int>(std::round(color.g + (255.0f - color.g) * amount));
    int b = static_cast<int>(std::round(color.b + (255.0f - color.b) * amount));
    return SDL_Color{clamp_color(r), clamp_color(g), clamp_color(b), color.a};
}

Uint8 CandidateEditorPieGraphWidget::clamp_color(int value) {
    return static_cast<Uint8>(std::clamp(value, 0, 255));
}

void CandidateEditorPieGraphWidget::ensure_search_created() {
    if (!search_assets_) {
        search_assets_ = std::make_unique<SearchAssets>();
        search_assets_->set_screen_dimensions(screen_w_, screen_h_);
    }
}

void CandidateEditorPieGraphWidget::position_search_within_bounds() {
    if (!search_assets_) {
        return;
    }
    SDL_Rect anchor = has_search_anchor_ ? last_search_anchor_ : rect_;
    int desired_x = anchor.x;
    int desired_y = anchor.y + anchor.h + 4;

    const int min_x = rect_.x + 4;
    int max_x = rect_.x + rect_.w - kSearchPanelWidth - 4;
    if (max_x < min_x) {
        max_x = min_x;
    }
    desired_x = std::clamp(desired_x, min_x, max_x);

    const int min_y = rect_.y + 4;
    int max_y = rect_.y + rect_.h - kSearchPanelHeight - 4;
    if (max_y < min_y) {
        max_y = min_y;
    }
    desired_y = std::clamp(desired_y, min_y, max_y);

    search_assets_->set_position(desired_x, desired_y);
}

void CandidateEditorPieGraphWidget::notify_layout_change() const {
    if (on_request_layout_) {
        on_request_layout_();
    }
}

bool CandidateEditorPieGraphWidget::search_visible() const {
    return search_assets_ && search_assets_->visible();
}
