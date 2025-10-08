#pragma once

#include "DockableCollapsible.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nlohmann {
class json;
}

class DMCheckbox;
class DMSlider;
class Widget;
class Input;

struct SDL_Rect;
struct SDL_Color;
struct SDL_Renderer;
union SDL_Event;

class LightRaysUIPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    explicit LightRaysUIPanel(int x = 96, int y = 96);
    ~LightRaysUIPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    bool is_point_inside(int x, int y) const;

private:
    void build_ui();
    void sync_ui_from_json();
    void sync_json_from_ui();
    nlohmann::json& ensure_params();

    static int clamp_int(int v, int lo, int hi);
    static double slider_units_to_double(int units, int scale);
    static int double_to_slider_units(double value, int scale, int lo, int hi);

    void configure_float_slider(DMSlider* slider, int scale, int precision);
    void update_save_status(bool success) const;

private:
    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_{};

    bool needs_sync_ = false;

    std::unique_ptr<DMCheckbox> enabled_checkbox_;
    std::unique_ptr<DMSlider> min_luma_slider_;
    std::unique_ptr<DMSlider> bright_percentile_slider_;
    std::unique_ptr<DMSlider> density_slider_;
    std::unique_ptr<DMSlider> decay_slider_;
    std::unique_ptr<DMSlider> weight_slider_;
    std::unique_ptr<DMSlider> exposure_slider_;
    std::unique_ptr<DMSlider> samples_slider_;
    std::unique_ptr<DMSlider> downsample_slider_;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    class StatusLabel;
    StatusLabel* status_label_ = nullptr;
    mutable std::string status_text_{};
};

