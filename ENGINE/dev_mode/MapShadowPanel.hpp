#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <nlohmann/json.hpp>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

class Assets;
class Input;

class MapShadowPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;
    using ReactiveShadowSettings = render_pipeline::shading::ReactiveShadowSettings;

    MapShadowPanel(Assets* assets, int x = 0, int y = 0);
    ~MapShadowPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_reactive_settings(ReactiveShadowSettings* settings);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w = 0, int screen_h = 0) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    bool is_point_inside(int x, int y) const override;

protected:
    void render_content(SDL_Renderer* renderer) const override;
    void layout_custom_content(int, int) const override {}

private:
    using LutEntry = ReactiveShadowSettings::ShadowResponseLutEntry;

    void build_ui();
    void rebuild_ui();
    void sync_ui_from_settings(const ReactiveShadowSettings& settings);
    ReactiveShadowSettings settings_from_ui();
    ReactiveShadowSettings load_settings() const;
    void apply_settings(const ReactiveShadowSettings& settings, bool persist);
    void request_save();
    nlohmann::json* ensure_reactive_shadow_json();
    static int find_entry_index(const std::vector<LutEntry>& entries, const LutEntry& target);
    static float read_scaled_slider(const std::unique_ptr<DMSlider>& slider, int scale, float fallback);

    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_{};
    ReactiveShadowSettings current_settings_{};
    ReactiveShadowSettings last_applied_settings_{};
    ReactiveShadowSettings* reactive_settings_shared_ = nullptr;
    bool applying_ui_ = false;
    bool pending_save_ = false;
    bool initialized_ = false;
    int selected_entry_index_ = 0;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_{};

    std::unique_ptr<DMSlider> horizontal_falloff_{};
    std::unique_ptr<DMSlider> vertical_falloff_{};
    std::unique_ptr<DMSlider> max_offset_x_{};
    std::unique_ptr<DMSlider> max_offset_y_{};
    std::unique_ptr<DMSlider> shadow_scale_{};
    std::unique_ptr<DMSlider> size_scale_factor_{};
    std::unique_ptr<DMSlider> search_radius_{};

    std::unique_ptr<DMSlider> opacity_strength_{};
    std::unique_ptr<DMSlider> parallax_strength_{};
    std::unique_ptr<DMSlider> scale_strength_{};

    std::unique_ptr<DMSlider> static_weight_{};
    std::unique_ptr<DMSlider> dynamic_weight_{};

    std::unique_ptr<DMSlider> lut_index_slider_{};
    std::unique_ptr<DMSlider> lut_brightness_{};
    std::unique_ptr<DMSlider> lut_opacity_{};
    std::unique_ptr<DMSlider> lut_offset_{};
    std::unique_ptr<DMSlider> lut_scale_{};

};
