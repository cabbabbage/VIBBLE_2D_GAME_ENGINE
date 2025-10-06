#pragma once

#include <SDL.h>
#include <filesystem>
#include <memory>
#include <string>

class AssetInfo;
class Input;

namespace animation_editor {

class AnimationEditorWindow {
  public:
    AnimationEditorWindow();

    void set_visible(bool visible);
    bool is_visible() const { return visible_; }
    void toggle_visible();

    void set_bounds(const SDL_Rect& bounds);
    const SDL_Rect& bounds() const { return bounds_; }

    void set_info(const std::shared_ptr<AssetInfo>& info);
    void clear_info();

    void update(const Input& input, int screen_w, int screen_h);
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void render_background(SDL_Renderer* renderer) const;
    void render_placeholder(SDL_Renderer* renderer) const;

  private:
    bool visible_ = false;
    SDL_Rect bounds_{0, 0, 0, 0};
    std::weak_ptr<AssetInfo> info_;
    std::filesystem::path info_path_;
};

}  // namespace animation_editor

