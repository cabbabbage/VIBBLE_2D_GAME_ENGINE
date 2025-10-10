#pragma once

#include <SDL.h>

#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "widgets.hpp"

// CandidateEditorPieGraphWidget renders a lightweight pie chart that visualizes
// the weight distribution of the spawn candidates in a spawn group. The widget
// purposefully keeps the rendering simple so it can be embedded within existing
// dev-mode panels without introducing additional rendering dependencies.
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

private:
    void normalize_weights();
    void draw_background(SDL_Renderer* renderer) const;
    void draw_slices(SDL_Renderer* renderer) const;
    void draw_empty_state(SDL_Renderer* renderer) const;
    static SDL_Color color_for_index(size_t index);

    SDL_Rect rect_{};
    std::vector<float> weights_{};
};

