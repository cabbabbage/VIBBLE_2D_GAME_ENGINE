#include "Section_AnimationChildren.hpp"

#include "asset_info_ui.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include <SDL_log.h>

namespace {

class ChildrenPanelWidget : public Widget {
  public:
    explicit ChildrenPanelWidget(animation_editor::ChildrenPanel* panel) : panel_(panel) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (panel_) {
            panel_->set_bounds(rect_);
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        return panel_ ? panel_->preferred_height(w) : 0;
    }

    bool handle_event(const SDL_Event& e) override {
        if (!panel_) return false;
        return panel_->handle_event(e);
    }

    void render(SDL_Renderer* renderer) const override {
        if (!panel_) return;
        panel_->set_bounds(rect_);
        panel_->render(renderer);
        panel_->render_overlays(renderer);
    }

    bool wants_full_row() const override { return true; }

  private:
    animation_editor::ChildrenPanel* panel_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

}  // namespace

Section_AnimationChildren::Section_AnimationChildren() : DockableCollapsible("Animation Children", false) {
    set_visible_height(360);
}

void Section_AnimationChildren::build() {
    widgets_.clear();
    children_widget_ = nullptr;
    DockableCollapsible::Rows rows;

    if (!info_) {
        auto placeholder = std::make_unique<ReadOnlyTextBoxWidget>(
            "",
            "No asset selected. Select an asset to manage animation children.");
        rows.push_back({placeholder.get()});
        widgets_.push_back(std::move(placeholder));
        set_rows(rows);
        return;
    }

    if (!children_panel_) {
        children_panel_ = std::make_unique<animation_editor::ChildrenPanel>();
        children_panel_->set_layout_dirty_callback([this]() { this->rebuild_rows(); });
        children_panel_->set_on_children_changed([this]() {
            if (ui_) {
                ui_->sync_animation_children();
            }
        });
    }
    children_panel_->set_info(info_);
    children_panel_->set_manifest_store(manifest_store_);
    children_panel_->set_status_callback([](const std::string& msg) { SDL_Log("%s", msg.c_str()); });

    auto widget = std::make_unique<ChildrenPanelWidget>(children_panel_.get());
    widget->set_layout_dirty_callback([this]() { this->rebuild_rows(); });
    children_widget_ = widget.get();
    widgets_.push_back(std::move(widget));
    rows.push_back({children_widget_});

    set_rows(rows);
}

void Section_AnimationChildren::update(const Input& input, int screen_w, int screen_h) {
    if (children_panel_) {
        children_panel_->update();
    }
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool Section_AnimationChildren::handle_event(const SDL_Event& e) {
    if (children_panel_ && children_panel_->allow_out_of_bounds_pointer_events()) {
        // Allow overlay interactions to function even when the search panel is drawn outside the bounds.
        return children_panel_->handle_event(e) || DockableCollapsible::handle_event(e);
    }
    return DockableCollapsible::handle_event(e);
}

void Section_AnimationChildren::rebuild_rows() {
    if (!children_widget_) {
        return;
    }
    DockableCollapsible::Rows rows;
    rows.push_back({children_widget_});
    set_rows(rows);
}
