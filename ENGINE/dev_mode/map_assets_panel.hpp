#pragma once

#include <functional>
#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"

class AssetsConfig;
class DMButton;
class ButtonWidget;
class DMCheckbox;
class CheckboxWidget;
class Widget;
class Input;
union SDL_Event;
struct SDL_Renderer;

// Floating dockable panel for editing map-wide and boundary spawn groups.
class MapAssetsPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    MapAssetsPanel(int x = 80, int y = 80);
    ~MapAssetsPanel() override;

    void set_map_info(nlohmann::json* map_info, const std::string& map_path);
    void set_on_save(SaveCallback cb);

    void open();
    void close();

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    bool is_point_inside(int x, int y) const;

private:
    void rebuild_rows();
    void ensure_configs_loaded();
    nlohmann::json& ensure_map_assets();
    nlohmann::json& ensure_map_boundary();
    static nlohmann::json& ensure_spawn_groups(nlohmann::json& root);
    // Ensures the spawn_groups array exists and has at least one entry.
    // Returns true if a default entry was created.
    static bool ensure_at_least_one_spawn_group(nlohmann::json& root);
    void refresh_checkbox_from_json();
    void handle_inherits_checkbox_change();
    void mark_dirty();
    void mark_clean();
    bool perform_save();
    bool save_to_disk() const;
    bool reload_from_disk();

private:
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    SaveCallback on_save_;

    std::unique_ptr<AssetsConfig> map_assets_cfg_;
    std::unique_ptr<AssetsConfig> boundary_cfg_;

    std::unique_ptr<Widget> map_label_;
    std::unique_ptr<Widget> boundary_label_;

    std::unique_ptr<DMCheckbox> inherits_checkbox_;
    std::unique_ptr<CheckboxWidget> inherits_widget_;

    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<ButtonWidget> save_button_widget_;
    std::unique_ptr<DMButton> reload_button_;
    std::unique_ptr<ButtonWidget> reload_button_widget_;
    std::unique_ptr<DMButton> close_button_;
    std::unique_ptr<ButtonWidget> close_button_widget_;

    std::unique_ptr<Widget> map_divider_;
    std::unique_ptr<Widget> footer_divider_;

    bool dirty_ = false;
};

