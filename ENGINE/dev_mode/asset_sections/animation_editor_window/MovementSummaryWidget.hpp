#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

namespace animation_editor {

class AnimationDocument;
struct ResolvedMovement;

class MovementSummaryWidget {
  public:
    using EditCallback = std::function<void(const std::string&)>;
    using GoToSourceCallback = std::function<void(const std::string&)>;

    MovementSummaryWidget();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_edit_callback(EditCallback callback);
    void set_go_to_source_callback(GoToSourceCallback callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

  private:
    void refresh_totals();
    void apply_resolved_totals(const ResolvedMovement& resolved);

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::string animation_id_;
    EditCallback edit_callback_;
    GoToSourceCallback go_to_source_callback_;
    SDL_Rect bounds_{0, 0, 0, 0};
    SDL_Rect button_rect_{0, 0, 0, 0};
    bool button_hovered_ = false;
    bool button_pressed_ = false;
    float total_dx_ = 0.0f;
    float total_dy_ = 0.0f;
    std::string totals_signature_;
    bool show_button_ = true;
    bool button_is_go_to_ = false;
    bool derived_from_animation_ = false;
    std::string inherited_source_id_;
    std::vector<std::string> inherited_message_lines_;
};

}

