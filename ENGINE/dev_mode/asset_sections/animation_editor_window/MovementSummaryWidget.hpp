#pragma once

#include <functional>
#include <memory>
#include <string>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;

class MovementSummaryWidget {
  public:
    using EditCallback = std::function<void(const std::string&)>;

    MovementSummaryWidget();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_edit_callback(EditCallback callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void refresh_totals();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::string animation_id_;
    EditCallback edit_callback_;
    SDL_Rect bounds_{0, 0, 0, 0};
    float total_dx_ = 0.0f;
    float total_dy_ = 0.0f;
};

}  // namespace animation_editor

