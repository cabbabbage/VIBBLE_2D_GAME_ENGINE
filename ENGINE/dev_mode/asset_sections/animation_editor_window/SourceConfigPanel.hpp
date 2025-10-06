#pragma once

#include <memory>
#include <string>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;
class CroppingService;
class AsyncTaskQueue;

class SourceConfigPanel {
  public:
    SourceConfigPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_services(std::shared_ptr<CroppingService> cropping, std::shared_ptr<AsyncTaskQueue> tasks);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void import_from_folder();
    void import_from_animation();
    void import_from_gif();
    void import_from_png_sequence();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<CroppingService> cropping_service_;
    std::shared_ptr<AsyncTaskQueue> task_queue_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
};

}  // namespace animation_editor

