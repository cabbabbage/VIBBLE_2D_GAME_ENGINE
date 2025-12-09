// Animation children control panel reusable widget.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "dev_mode/DockableCollapsible.hpp"
#include "dev_mode/widgets.hpp"

namespace animation_editor {

class AnimationDocument;

class AnimationChildrenPanel : public DockableCollapsible {
  public:
    AnimationChildrenPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_status_callback(std::function<void(const std::string&, int)> callback);
    void set_on_children_changed(std::function<void(const std::vector<std::string>&)> callback);
    void set_on_selection_changed(std::function<void(std::optional<std::string>)> callback);

    std::optional<std::string> selected_child() const;

    void refresh();
    void update();

    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override { DockableCollapsible::render(renderer); }

    void set_work_area_bounds(const SDL_Rect& bounds);

  private:
    void rebuild_rows();
    void sync_from_document();
    void update_button_styles();
    void select_child(int index);
    void add_child();
    void rename_child();
    void remove_child();
    void notify_selection_changed();
    std::vector<std::string> sanitized_children_from_document() const;
    std::string current_signature() const;
    static std::string trim_name(const std::string& raw);
    void apply_children(const std::vector<std::string>& next, const std::string& status_message);

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::function<void(const std::string&, int)> status_callback_;
    std::function<void(const std::vector<std::string>&)> on_children_changed_;
    std::function<void(std::optional<std::string>)> on_selection_changed_;

    std::unique_ptr<DMDropdown> child_dropdown_;
    std::unique_ptr<DropdownWidget> child_dropdown_widget_;
    std::unique_ptr<DMTextBox> name_box_;
    std::unique_ptr<TextBoxWidget> name_widget_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<ButtonWidget> add_widget_;
    std::unique_ptr<DMButton> rename_button_;
    std::unique_ptr<ButtonWidget> rename_widget_;
    std::unique_ptr<DMButton> remove_button_;
    std::unique_ptr<ButtonWidget> remove_widget_;

    std::vector<std::string> child_names_;
    int selected_index_ = -1;
    std::string last_signature_;
    std::string last_name_value_;
};

} // namespace animation_editor