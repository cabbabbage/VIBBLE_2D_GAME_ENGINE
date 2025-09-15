#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "animation_utils.hpp"

class Input;
class DockableCollapsible;
class Widget;
class DMButton;
class DMTextBox;
class DMCheckbox;
class DMSlider;
class DMDropdown;
// Fresh C++ version of the Animations editor, modelled after the Python UI.
// This panel only reads/writes SRC/<asset>/info.json and manipulates image
// files inside SRC/<asset> folders. It does NOT use AssetInfo.
class AnimationsEditorPanel {
public:
    AnimationsEditorPanel();
    ~AnimationsEditorPanel();

    // Provide paths for the selected asset. The panel will only use these.
    void set_asset_paths(const std::string& asset_dir_path,
                         const std::string& info_json_path);
    void open();
    void close();
    bool is_open() const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;

private:
    void rebuild_all_rows();
    void rebuild_header_row();
    void rebuild_animation_rows();
    std::vector<std::string> current_names_sorted() const;
    int compute_frames_from_source(const nlohmann::json& source) const;
    static nlohmann::json default_payload(const std::string& name);
    bool save_info_json() const;
    bool load_info_json();
    std::vector<std::string> animation_names() const;
    nlohmann::json animation_payload(const std::string& name) const;
    bool upsert_animation(const std::string& name, const nlohmann::json& payload);
    bool remove_animation(const std::string& name);
    bool rename_animation(const std::string& old_name, const std::string& new_name);
    std::string get_start_animation_name() const;
    void set_start_animation_name(const std::string& name);
    std::string first_frame_path(const nlohmann::json& source) const;
    bool creates_cycle(const std::string& current, const std::string& ref) const;

    struct AnimUI {
        std::string name;
        nlohmann::json last_payload;
        std::unique_ptr<DMTextBox>   id_box;
        std::unique_ptr<DMDropdown>  kind_dd;
        std::unique_ptr<DMTextBox>   path_box;
        std::unique_ptr<DMDropdown>  ref_dd;
        std::unique_ptr<DMCheckbox>  flipped_cb;
        std::unique_ptr<DMCheckbox>  reversed_cb;
        std::unique_ptr<DMCheckbox>  locked_cb;
        std::unique_ptr<DMCheckbox>  loop_cb;
        std::unique_ptr<DMCheckbox>  rnd_start_cb;
        std::unique_ptr<DMSlider>    speed_sl;
        std::unique_ptr<DMButton>    del_btn;
        std::unique_ptr<DMButton>    movement_btn;
        std::unique_ptr<DMTextBox>   frames_label;
        std::unique_ptr<DMDropdown>  on_end_dd;
        std::unique_ptr<DMButton>    dup_btn;
        // Crop helpers (folder-kind only)
        std::unique_ptr<DMSlider>    alpha_sl;     // 0..255 threshold
        std::unique_ptr<DMButton>    compute_btn;  // compute bounds
        std::unique_ptr<DMButton>    crop_btn;     // apply crop
        std::unique_ptr<DMTextBox>   crop_summary; // shows margins
        animation::Bounds             last_bounds{};
        bool                          has_bounds{false};
        // Folder scaffolding
        std::unique_ptr<DMButton>    create_folder_btn;
        std::vector<std::unique_ptr<Widget>> row_widgets;
    };

private:
    // Header controls
    std::unique_ptr<DMDropdown> start_dd_;
    std::unique_ptr<DMButton>   new_btn_;
    std::unique_ptr<DMButton>   new_folder_btn_;
    mutable std::vector<std::unique_ptr<Widget>> header_widgets_;

    std::unique_ptr<DockableCollapsible> box_;
    std::vector<std::unique_ptr<AnimUI>> items_;
    std::vector<std::vector<Widget*>> rows_;
    // Context (paths and JSON cache)
    std::string asset_dir_path_;
    std::string info_json_path_;
    nlohmann::json info_json_;

    // Modal editor for per-frame movement (ported from Python).
    animation::MovementModal movement_modal_;
    bool movement_was_open_ = false;
    std::string movement_anim_name_;
    bool rebuild_requested_ = false;
    void request_rebuild() { rebuild_requested_ = true; }
};
