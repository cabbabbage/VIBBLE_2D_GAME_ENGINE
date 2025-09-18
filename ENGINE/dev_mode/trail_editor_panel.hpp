#pragma once

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"

class AssetsConfig;
class DMButton;
class ButtonWidget;
class DMTextBox;
class TextBoxWidget;
class DMRangeSlider;
class RangeSliderWidget;
class DMSlider;
class SliderWidget;
class DMCheckbox;
class CheckboxWidget;
class Widget;
class Input;
class Room;
union SDL_Event;
struct SDL_Renderer;

// Floating editor panel for configuring a single trail definition.
class TrailEditorPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    TrailEditorPanel(int x = 96, int y = 96);
    ~TrailEditorPanel() override;

    void set_on_save(SaveCallback cb);

    // Opens the editor for a given trail definition. The json pointer may be null
    // if editing should only affect the runtime room copy.
    void open(const std::string& trail_id, nlohmann::json* trail_json, Room* room);
    void close();

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    bool is_point_inside(int x, int y) const;

private:
    void rebuild_rows();
    void sync_room_to_entry();
    void ensure_spawn_groups();
    void mark_dirty();
    void mark_clean();
    bool perform_save();
    void refresh_cached_values();

private:
    SaveCallback on_save_{};

    Room* trail_room_ = nullptr;
    nlohmann::json* trail_entry_ = nullptr;
    nlohmann::json* trail_room_json_ = nullptr;

    std::string trail_id_;
    std::string name_;
    int min_width_ = 0;
    int max_width_ = 0;
    int curvyness_ = 0;
    bool inherits_map_assets_ = false;

    std::unique_ptr<DMTextBox> name_box_;
    std::unique_ptr<TextBoxWidget> name_widget_;
    std::unique_ptr<DMRangeSlider> width_slider_;
    std::unique_ptr<RangeSliderWidget> width_widget_;
    std::unique_ptr<DMSlider> curvyness_slider_;
    std::unique_ptr<SliderWidget> curvyness_widget_;
    std::unique_ptr<DMCheckbox> inherits_checkbox_;
    std::unique_ptr<CheckboxWidget> inherits_widget_;
    std::unique_ptr<AssetsConfig> assets_cfg_;
    std::unique_ptr<Widget> spawn_label_;
    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<ButtonWidget> save_button_widget_;
    std::unique_ptr<DMButton> close_button_;
    std::unique_ptr<ButtonWidget> close_button_widget_;

    bool dirty_ = false;
};

