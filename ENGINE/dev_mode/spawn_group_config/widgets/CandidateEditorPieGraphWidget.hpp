#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "widgets.hpp"
#include "dev_mode/search_assets.hpp"

class Input;
class CandidateEditorPieGraphWidget : public Widget {
public:
    CandidateEditorPieGraphWidget();

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override;
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;
    bool wants_full_row() const override { return true; }

    void set_screen_dimensions(int width, int height);
    void set_on_request_layout(std::function<void()> cb) { on_request_layout_ = std::move(cb); }
    void set_weights(std::vector<float> weights);
    void set_candidates_from_json(const nlohmann::json& entry);
    void set_on_adjust(std::function<void(int index, int delta)> cb) { on_adjust_ = std::move(cb); }
    void set_on_delete(std::function<void(int index)> cb) { on_delete_ = std::move(cb); }
    void set_on_regenerate(std::function<void()> cb);
    void set_on_add_candidate(std::function<void(const std::string&)> cb);
    void hide_search();
    void update_search(const Input& input);
    void set_search_extra_results_provider(SearchAssets::ExtraResultsProvider provider);

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
    void update_internal_layout();
    void open_add_candidate_search();
    bool should_show_regen_button() const;
    bool should_show_add_button() const;
    void show_search(const SDL_Rect& anchor_rect, std::function<void(const std::string&)> on_select);
    void draw_background(SDL_Renderer* renderer) const;
    void render_empty(SDL_Renderer* renderer, const Layout& layout, TTF_Font* font) const;
    void render_slices(SDL_Renderer* renderer, const Layout& layout, double total) const;
    void render_outline(SDL_Renderer* renderer, const Layout& layout) const;
    void render_legend(SDL_Renderer* renderer, const Layout& layout, double total, TTF_Font* font) const;
    void update_collapse_button();
    SDL_Rect draw_text(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center) const;
    int hit_test_candidate(const Layout& layout, SDL_Point point, double total) const;
    void cache_legend_rows(const Layout& layout, int row_height = -1) const;
    static int default_legend_row_height();
    static SDL_Color color_for_index(size_t index);
    static SDL_Color lighten(SDL_Color color, float amount);
    static Uint8 clamp_color(int value);
    int desired_search_panel_height() const;
    void release_scroll_capture();
    void ensure_search_created();
    void position_search_within_bounds();
    void notify_layout_change() const;
    bool search_visible() const;

    SDL_Rect rect_{};
    std::vector<CandidateInfo> candidates_{};
    int hovered_index_ = -1;
    int active_index_ = -1;
    std::function<void(int index, int delta)> on_adjust_{};
    std::function<void(int index)> on_delete_{};
    std::function<void()> on_regenerate_{};
    std::function<void(const std::string&)> on_add_candidate_{};
    std::function<void()> on_request_layout_{};
    bool scroll_capture_active_ = false;
    double wheel_scroll_accumulator_ = 0.0;
    mutable std::vector<SDL_Rect> legend_row_rects_{};
    mutable int legend_row_height_ = 0;
    std::unique_ptr<DMButton> regen_button_{};
    std::unique_ptr<DMButton> add_button_{};
    std::unique_ptr<DMButton> collapse_button_{};
    bool collapsed_ = false;
    SDL_Rect content_rect_{0, 0, 0, 0};
    std::unique_ptr<SearchAssets> search_assets_{};
    SearchAssets::ExtraResultsProvider search_extra_results_provider_{};
    SDL_Rect search_rect_{0, 0, 0, 0};
    int screen_w_ = 0;
    int screen_h_ = 0;
    mutable bool search_visible_previous_ = false;
    int last_search_height_ = 0;
};

