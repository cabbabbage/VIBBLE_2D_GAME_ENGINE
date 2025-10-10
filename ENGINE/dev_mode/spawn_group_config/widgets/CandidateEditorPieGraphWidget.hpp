#pragma once

#include <SDL.h>

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "widgets.hpp"

class CandidateEditorPieGraphWidget : public Widget {
public:
    CandidateEditorPieGraphWidget();

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override;
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;
    bool wants_full_row() const override { return true; }

    void set_weights(std::vector<float> weights);
    void set_candidates_from_json(const nlohmann::json& entry);
    void set_on_adjust(std::function<void(int index, int delta)> cb) { on_adjust_ = std::move(cb); }
    void set_on_delete(std::function<void(int index)> cb) { on_delete_ = std::move(cb); }

private:
    struct CandidateInfo {
        std::string name;
        double weight = 0.0;
};

    struct Layout {
        SDL_FPoint center{0.f, 0.f};
        float radius = 0.f;
        SDL_Rect legend{0, 0, 0, 0};
};

    Layout compute_layout() const;
    double total_weight() const;
    void draw_background(SDL_Renderer* renderer) const;
    void render_empty(SDL_Renderer* renderer, const Layout& layout, TTF_Font* font) const;
    void render_slices(SDL_Renderer* renderer, const Layout& layout, double total) const;
    void render_outline(SDL_Renderer* renderer, const Layout& layout) const;
    void render_legend(SDL_Renderer* renderer, const Layout& layout, double total, TTF_Font* font) const;
    SDL_Rect draw_text(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center) const;
    static SDL_Color color_for_index(size_t index, bool highlight);
    static SDL_Color lighten(SDL_Color color, float amount);
    static Uint8 clamp_color(int value);

    SDL_Rect rect_{};
    std::vector<CandidateInfo> candidates_{};
    int hovered_index_ = -1;
    int active_index_ = -1;
    std::function<void(int index, int delta)> on_adjust_{};
    std::function<void(int index)> on_delete_{};
    bool scroll_capture_active_ = false;
};

