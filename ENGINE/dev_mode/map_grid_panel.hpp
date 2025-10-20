#pragma once

#include <memory>
#include <vector>
#include <functional>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "utils/map_grid_settings.hpp"

class Input;

class MapGridPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;
    using RegenCallback = std::function<void()>;

    MapGridPanel(int x = 40, int y = 40);
    ~MapGridPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr, RegenCallback on_regen = nullptr);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w = 0, int screen_h = 0);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;
    bool is_point_inside(int x, int y) const;

private:
    void build_ui();
    void rebuild_rows();
    void sync_from_json();
    void apply_settings(bool trigger_save = true);
    void handle_resolution_changed();
    void handle_jitter_changed();
    void handle_chunk_changed();
    void trigger_regen();

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;
    RegenCallback on_regen_;
    MapGridSettings settings_{};

    std::unique_ptr<DMSlider> resolution_slider_;
    std::unique_ptr<DMSlider> chunk_slider_;
    std::unique_ptr<DMSlider> jitter_slider_;
    std::unique_ptr<DMButton> regen_button_;
    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    int last_resolution_value_ = 0;
    int last_jitter_value_ = 0;
    int last_chunk_value_ = 0;
};
