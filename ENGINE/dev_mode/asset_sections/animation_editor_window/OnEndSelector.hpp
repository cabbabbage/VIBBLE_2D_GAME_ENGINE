#pragma once

#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "dev_mode/widgets.hpp"

namespace animation_editor {

class AnimationDocument;
class OnEndSelector {
  public:
    OnEndSelector();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

    bool allow_out_of_bounds_pointer_events() const;

  private:
    void rebuild_options();
    void sync_from_document();
    void layout_dropdown() const;
    void commit_selection();
    int find_option_index(const std::string& value) const;

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    std::vector<std::string> options_;
    std::unique_ptr<DMDropdown> dropdown_;
    mutable bool layout_dirty_ = true;
    std::string payload_signature_;
    std::string ids_signature_;
};

}

