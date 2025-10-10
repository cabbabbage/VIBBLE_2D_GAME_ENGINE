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

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kStartAngle = -1.5707963267948966;

double clamp_positive(double value) {
    return value < 0.0 ? 0.0 : value;
}
}

CandidateEditorPieGraphWidget::CandidateEditorPieGraphWidget() {
    rect_ = SDL_Rect{0, 0, 280, 180};
}

void CandidateEditorPieGraphWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
}

const SDL_Rect& CandidateEditorPieGraphWidget::rect() const {
    return rect_;
}

int CandidateEditorPieGraphWidget::height_for_width(int w) const {
    int constrained = std::clamp(w, 160, 420);
    return std::max(constrained, rect_.h > 0 ? rect_.h : 180);
}

bool CandidateEditorPieGraphWidget::handle_event(const SDL_Event& e) {
    if (candidates_.empty()) {
        hovered_index_ = -1;
        if (scroll_capture_active_) {
            DMWidgetsSetSliderScrollCapture(this, false);
            scroll_capture_active_ = false;
        }
        return false;
    }

    if (e.type == SDL_MOUSEMOTION) {
        Layout layout = compute_layout();
        double total = total_weight();
        if (total <= 0.0) {
            total = 1.0;
        }

        const float dx = static_cast<float>(e.motion.x) - layout.center.x;
        const float dy = static_cast<float>(e.motion.y) - layout.center.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        int new_hover = -1;
        if (layout.radius > 0.0f && dist <= layout.radius + 10.0f) {
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
                    new_hover = static_cast<int>(i);
                    break;
                }
                used += sweep;
            }
        }

        if (new_hover != hovered_index_) {
            hovered_index_ = new_hover;
            return true;
        }
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (hovered_index_ >= 0) {
            if (active_index_ != hovered_index_) {
                active_index_ = hovered_index_;
                if (!scroll_capture_active_) {
                    DMWidgetsSetSliderScrollCapture(this, true);
                    scroll_capture_active_ = true;
                }
            } else {
                active_index_ = -1;
                if (scroll_capture_active_) {
                    DMWidgetsSetSliderScrollCapture(this, false);
                    scroll_capture_active_ = false;
                }
            }
            return true;
        } else if (active_index_ != -1) {
            active_index_ = -1;
            if (scroll_capture_active_) {
                DMWidgetsSetSliderScrollCapture(this, false);
                scroll_capture_active_ = false;
            }
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

    if (active_index_ == -1 && scroll_capture_active_) {
        DMWidgetsSetSliderScrollCapture(this, false);
        scroll_capture_active_ = false;
    }

    return false;
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
    if (active_index_ >= static_cast<int>(candidates_.size())) {
        active_index_ = -1;
        if (scroll_capture_active_) {
            DMWidgetsSetSliderScrollCapture(this, false);
            scroll_capture_active_ = false;
        }
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

    return layout;
}

double CandidateEditorPieGraphWidget::total_weight() const {
    return std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
                           [](double acc, const CandidateInfo& info) {
                               return acc + clamp_positive(info.weight);
                           });
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

        const bool highlight = static_cast<int>(i) == hovered_index_;
        SDL_Color color = color_for_index(i, highlight);
        float slice_radius = layout.radius + (highlight ? 6.0f : 0.0f);
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
        int y = layout.legend.y;
        for (size_t i = 0; i < candidates_.size(); ++i) {
            if (y + row_height > layout.legend.y + layout.legend.h) {
                break;
            }
            const bool highlight = static_cast<int>(i) == hovered_index_;
            SDL_Color swatch = color_for_index(i, highlight);
            SDL_Rect box{layout.legend.x, y, 16, 16};
            SDL_SetRenderDrawColor(renderer, swatch.r, swatch.g, swatch.b, 255);
            SDL_RenderFillRect(renderer, &box);
            SDL_Color outline_color = DMStyles::Border();
            outline_color.a = 255;
            SDL_SetRenderDrawColor(renderer, outline_color.r, outline_color.g, outline_color.b, outline_color.a);
            SDL_RenderDrawRect(renderer, &box);

            double percent = total > 0.0 ? (clamp_positive(candidates_[i].weight) / total) * 100.0 : 0.0;
            std::ostringstream label;
            label << candidates_[i].name << " - " << std::fixed << std::setprecision(1) << percent << "% (" << static_cast<int>(std::round(clamp_positive(candidates_[i].weight))) << ")";
            draw_text(renderer, font, label.str(), box.x + box.w + 8, y + (row_height - font_height) / 2, text_color, false);

            y += row_height;
        }
    } else {
        std::ostringstream summary;
        summary << "Total weight: " << static_cast<int>(std::round(total));
        draw_text(renderer, font, summary.str(), rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap(), DMStyles::Label().color, false);
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

SDL_Color CandidateEditorPieGraphWidget::color_for_index(size_t index, bool highlight) {
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

    SDL_Color color = kPalette[index % kPalette.size()];
    if (highlight) {
        color = lighten(color, 0.18f);
    }
    return color;
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
