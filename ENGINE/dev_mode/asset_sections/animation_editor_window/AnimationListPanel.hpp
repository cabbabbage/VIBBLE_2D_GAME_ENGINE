#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

namespace animation_editor {

class AnimationDocument;
class PreviewProvider;

class AnimationListPanel {
  public:
    AnimationListPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_bounds(const SDL_Rect& bounds);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);
    void set_selected_animation_id(const std::optional<std::string>& animation_id);
    void set_on_selection_changed(std::function<void(const std::optional<std::string>&)> callback);
    void set_on_context_menu(std::function<void(const std::string&, const SDL_Point&)> callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void rebuild_rows();
    void layout_rows();
    void clamp_scroll();
    void scroll_selection_into_view();
    std::optional<size_t> row_index_at_point(const SDL_Point& p) const;

  private:
    struct DisplayRow {
        std::string id;
        int level = 0;
        bool missing_source = false;
    };

    std::shared_ptr<AnimationDocument> document_;
    std::vector<SDL_Rect> row_bounds_;
    std::vector<DisplayRow> display_rows_;
    std::optional<std::string> start_animation_id_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::function<void(const std::optional<std::string>&)> on_selection_changed_;
    std::function<void(const std::string&, const SDL_Point&)> on_context_menu_;
    std::optional<std::string> selected_animation_id_;
    std::optional<size_t> hovered_row_;
    SDL_Rect bounds_{0, 0, 0, 0};
    int scroll_offset_ = 0;
    int content_height_ = 0;
    bool layout_dirty_ = true;
};

}

