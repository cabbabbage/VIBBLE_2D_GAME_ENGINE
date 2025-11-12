#pragma once

#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

namespace animation_editor {

class AnimationDocument;
class PreviewProvider;
class FrameToolsPanel;
class MovementCanvas;

class FrameChildrenEditor {
  public:
    FrameChildrenEditor();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);
    void set_tools_panel(FrameToolsPanel* panel);
    void set_canvas(MovementCanvas* canvas);
    void set_selected_frame(int index);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    bool handle_key_event(const SDL_Event& e);

  private:
    struct ChildFrame {
        int child_index = -1;
        float dx = 0.0f;
        float dy = 0.0f;
        float rotation = 0.0f;
        bool visible = true;
};

    struct MovementFrame {
        float dx = 0.0f;
        float dy = 0.0f;
        bool resort_z = false;
        std::vector<ChildFrame> children;
};

  private:
    void reload_from_document();
    void ensure_child_vectors();
    void refresh_tools_panel() const;
    void select_child(int index);
    void apply_current_to_next();
    void set_child_visible(bool visible);
    void persist_changes();
    MovementFrame* current_frame();
    const MovementFrame* current_frame() const;
    ChildFrame* current_child();
    const ChildFrame* current_child() const;
    bool point_in_canvas(int x, int y) const;
    SDL_FPoint screen_to_world(SDL_Point screen) const;
    SDL_FPoint world_to_screen(const SDL_FPoint& world) const;
    int hit_test_child(int x, int y) const;

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_;
    FrameToolsPanel* tools_panel_ = nullptr;
    MovementCanvas* canvas_ = nullptr;
    std::string animation_id_;
    std::vector<std::string> child_ids_;
    std::vector<MovementFrame> frames_;
    int selected_frame_index_ = 0;
    int selected_child_index_ = 0;
    bool dragging_child_ = false;
    SDL_Point drag_start_screen_{0, 0};
    SDL_FPoint drag_start_world_{0.0f, 0.0f};
    ChildFrame drag_snapshot_;
    std::string payload_signature_;
};

}
