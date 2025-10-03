#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"

#include <nlohmann/json.hpp>

class MapLightPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<void()>;

    MapLightPanel(int x = 40, int y = 40);
    ~MapLightPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w = 0, int screen_h = 0);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    bool is_point_inside(int x, int y) const;

protected:

    void render_content(SDL_Renderer* r) const override;

private:

    void build_ui();
    void sync_ui_from_json();
    void sync_json_from_ui();
    nlohmann::json& ensure_light();

    void ensure_keys_array();
    void clamp_key_index();
    void select_prev_key();
    void select_next_key();
    void add_key_pair_at_current_angle();
    void delete_current_key();

    static int clamp_int(int v, int lo, int hi);
    static float clamp_float(float v, float lo, float hi);
    static float wrap_angle(float a);

private:

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;

    int current_key_index_ = 0;

    std::unique_ptr<DMSlider> radius_;
    std::unique_ptr<DMSlider> intensity_;
    std::unique_ptr<DMSlider> orbit_radius_;
    std::unique_ptr<DMSlider> update_interval_;
    std::unique_ptr<DMSlider> mult_x100_;
    std::unique_ptr<DMSlider> falloff_;
    std::unique_ptr<DMSlider> min_opacity_;
    std::unique_ptr<DMSlider> max_opacity_;

    std::unique_ptr<DMSlider> base_r_;
    std::unique_ptr<DMSlider> base_g_;
    std::unique_ptr<DMSlider> base_b_;
    std::unique_ptr<DMSlider> base_a_;

    std::unique_ptr<DMButton> prev_key_btn_;
    std::unique_ptr<DMButton> next_key_btn_;
    std::unique_ptr<DMButton> add_pair_btn_;
    std::unique_ptr<DMButton> delete_btn_;

    std::unique_ptr<DMSlider> key_angle_;
    std::unique_ptr<DMSlider> key_r_;
    std::unique_ptr<DMSlider> key_g_;
    std::unique_ptr<DMSlider> key_b_;
    std::unique_ptr<DMSlider> key_a_;

    mutable std::string current_key_label_;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    bool needs_sync_to_json_ = false;
};
