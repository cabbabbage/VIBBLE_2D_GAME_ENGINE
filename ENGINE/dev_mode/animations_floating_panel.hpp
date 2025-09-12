#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

class Input;
class FloatingCollapsible;
class Widget;
class DMButton;
class DMTextBox;
class DMCheckbox;
class DMSlider;
class DMDropdown;
class AssetInfo;

// Floating, collapsible Animations editor panel (C++ port of Python scripts)
// - Lists animations from AssetInfo info.json
// - Edit: id (rename), source (folder|animation), path/ref, flags, speed, on_end, start selector
// - Computes frame count from folder sources (PNG sequence)
// - Auto-saves to info.json on change via AssetInfo helpers
class AnimationsFloatingPanel {
public:
    AnimationsFloatingPanel();
    ~AnimationsFloatingPanel();

    void set_info(const std::shared_ptr<AssetInfo>& info);
    void open();
    void close();
    bool is_open() const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;

private:
    void rebuild_rows();
    void rebuild_header_row();
    void rebuild_animation_rows();
    std::vector<std::string> current_names_sorted() const;
    static int compute_frames_from_source(const AssetInfo& info, const nlohmann::json& source);
    static nlohmann::json default_payload(const std::string& name);

    // A single animation UI bundle
    struct AnimUI {
        std::string name;                        // current key
        nlohmann::json last_payload;             // cache to detect changes
        // Controls
        std::unique_ptr<DMTextBox>   id_box;
        std::unique_ptr<DMDropdown>  kind_dd;    // folder|animation
        std::unique_ptr<DMTextBox>   path_box;   // folder path (relative under SRC/<asset>)
        std::unique_ptr<DMDropdown>  ref_dd;     // referenced animation name
        std::unique_ptr<DMCheckbox>  flipped_cb;
        std::unique_ptr<DMCheckbox>  reversed_cb;
        std::unique_ptr<DMCheckbox>  locked_cb;
        std::unique_ptr<DMCheckbox>  loop_cb;
        std::unique_ptr<DMCheckbox>  rnd_start_cb;
        std::unique_ptr<DMSlider>    speed_sl;   // -20..20 (0 coerces to 1)
        std::unique_ptr<DMButton>    del_btn;
        std::unique_ptr<DMButton>    movement_btn; // TODO: hook modal editor
        // Readonly frames label (in a textbox for simplicity)
        std::unique_ptr<DMTextBox>   frames_label;

        // Row adapters (non-owning wrappers)
        std::vector<std::unique_ptr<Widget>> row_widgets; // flattened in creation order
    };

    // Header controls
    std::unique_ptr<DMDropdown> start_dd_;
    std::unique_ptr<DMButton>   new_btn_;
    mutable std::vector<std::unique_ptr<Widget>> header_widgets_;

    std::unique_ptr<FloatingCollapsible> box_;
    std::vector<std::unique_ptr<AnimUI>> items_;
    std::vector<std::vector<Widget*>> rows_;
    std::shared_ptr<AssetInfo> info_;
};

