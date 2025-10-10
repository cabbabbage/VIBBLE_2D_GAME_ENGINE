#include "CandidateEditorPieGraphWidget.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include <nlohmann/json.hpp>

namespace {
constexpr float kPi = 3.1415926535f;

float clamp_positive(float value) {
    return value < 0.0f ? 0.0f : value;
}
}  // namespace

CandidateEditorPieGraphWidget::CandidateEditorPieGraphWidget() {
    rect_ = SDL_Rect{0, 0, 120, 120};
}

void CandidateEditorPieGraphWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
}

const SDL_Rect& CandidateEditorPieGraphWidget::rect() const {
    return rect_;
}

int CandidateEditorPieGraphWidget::height_for_width(int w) const {
    int target = std::clamp(w, 96, 200);
    return std::max(target, rect_.h > 0 ? rect_.h : 120);
}

bool CandidateEditorPieGraphWidget::handle_event(const SDL_Event&) {
    return false;
}

void CandidateEditorPieGraphWidget::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    draw_background(renderer);

    if (weights_.empty()) {
        draw_empty_state(renderer);
        return;
    }

    draw_slices(renderer);
}

void CandidateEditorPieGraphWidget::set_weights(std::vector<float> weights) {
    weights_ = std::move(weights);
    normalize_weights();
}

void CandidateEditorPieGraphWidget::set_candidates_from_json(const nlohmann::json& entry) {
    std::vector<float> weights;
    if (entry.is_object() && entry.contains("candidates") && entry["candidates"].is_array()) {
        const auto& candidates = entry["candidates"];
        weights.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            float weight = 1.0f;
            if (candidate.is_object()) {
                if (candidate.contains("weight")) {
                    const auto& value = candidate["weight"];
                    if (value.is_number_float()) {
                        weight = static_cast<float>(value.get<double>());
                    } else if (value.is_number_integer()) {
                        weight = static_cast<float>(value.get<int>());
                    }
                }
            }
            weights.push_back(clamp_positive(weight));
        }
    }
    set_weights(std::move(weights));
}

void CandidateEditorPieGraphWidget::normalize_weights() {
    weights_.erase(std::remove_if(weights_.begin(), weights_.end(), [](float value) {
        return value <= 0.0f;
    }), weights_.end());

    if (weights_.empty()) {
        return;
    }

    const float total = std::accumulate(weights_.begin(), weights_.end(), 0.0f);
    if (total <= 0.0f) {
        const float uniform = 1.0f;
        std::fill(weights_.begin(), weights_.end(), uniform);
        return;
    }

    for (auto& weight : weights_) {
        weight = weight / total;
    }
}

void CandidateEditorPieGraphWidget::draw_background(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 20, 24, 180);
    SDL_RenderFillRect(renderer, &rect_);
}

void CandidateEditorPieGraphWidget::draw_slices(SDL_Renderer* renderer) const {
    if (weights_.empty()) {
        return;
    }

    SDL_Point center{rect_.x + rect_.w / 2, rect_.y + rect_.h / 2};
    const int radius = std::max(12, std::min(rect_.w, rect_.h) / 2 - 6);

    float start_angle = -90.0f;
    for (size_t i = 0; i < weights_.size(); ++i) {
        const float weight = weights_[i];
        if (weight <= 0.0f) {
            continue;
        }
        const float sweep = weight * 360.0f;
        const SDL_Color color = color_for_index(i);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int segments = std::max(1, static_cast<int>(sweep / 2.0f));
        for (int step = 0; step <= segments; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(segments);
            const float angle = start_angle + sweep * t;
            const float radians = angle * (kPi / 180.0f);
            const int x = center.x + static_cast<int>(std::round(std::cos(radians) * radius));
            const int y = center.y + static_cast<int>(std::round(std::sin(radians) * radius));
            SDL_RenderDrawLine(renderer, center.x, center.y, x, y);
        }
        start_angle += sweep;
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
    const int outline_segments = 72;
    int prev_x = center.x + radius;
    int prev_y = center.y;
    for (int i = 1; i <= outline_segments; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(outline_segments) * 2.0f * kPi;
        const int x = center.x + static_cast<int>(std::round(std::cos(angle) * radius));
        const int y = center.y + static_cast<int>(std::round(std::sin(angle) * radius));
        SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }
}

void CandidateEditorPieGraphWidget::draw_empty_state(SDL_Renderer* renderer) const {
    SDL_Point center{rect_.x + rect_.w / 2, rect_.y + rect_.h / 2};
    const int radius = std::max(12, std::min(rect_.w, rect_.h) / 2 - 6);

    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    const int outline_segments = 64;
    int prev_x = center.x + radius;
    int prev_y = center.y;
    for (int i = 1; i <= outline_segments; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(outline_segments) * 2.0f * kPi;
        const int x = center.x + static_cast<int>(std::round(std::cos(angle) * radius));
        const int y = center.y + static_cast<int>(std::round(std::sin(angle) * radius));
        SDL_RenderDrawLine(renderer, prev_x, prev_y, x, y);
        prev_x = x;
        prev_y = y;
    }

    SDL_RenderDrawLine(renderer, center.x - radius / 2, center.y, center.x + radius / 2, center.y);
    SDL_RenderDrawLine(renderer, center.x, center.y - radius / 2, center.x, center.y + radius / 2);
}

SDL_Color CandidateEditorPieGraphWidget::color_for_index(size_t index) {
    static constexpr std::array<SDL_Color, 6> kPalette{{
        SDL_Color{0xED, 0x6A, 0x5A, 0xFF},
        SDL_Color{0x5A, 0xC8, 0xED, 0xFF},
        SDL_Color{0x9C, 0xED, 0x5A, 0xFF},
        SDL_Color{0xF2, 0xC9, 0x2C, 0xFF},
        SDL_Color{0xAE, 0x79, 0xED, 0xFF},
        SDL_Color{0xED, 0xA0, 0x5A, 0xFF},
    }};
    return kPalette[index % kPalette.size()];
}

