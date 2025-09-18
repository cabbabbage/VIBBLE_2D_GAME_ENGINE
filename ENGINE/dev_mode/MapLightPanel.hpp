#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"

#include <nlohmann/json.hpp>

// Floatable dockable panel that edits map_info["map_light_data"].
// NOTE: All light data now lives under the single merged map_info.json.
// This panel DOES NOT read a separate map_light.json.
class MapLightPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<void()>;

    MapLightPanel(int x = 40, int y = 40);
    ~MapLightPanel() override;

    // Attach the merged map_info object and an optional save callback.
    // map_info must outlive this panel.
    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);

    // Show/hide helpers
    void open();
    void close();
    void toggle();
    bool is_visible() const;

    // Standard dev-mode panel API
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    // Hit-test for input routing
    bool is_point_inside(int x, int y) const;

protected:
    // DockableCollapsible hook
    void render_content(SDL_Renderer* r) const override;

private:
    // UI creation & binding
    void build_ui();
    void sync_ui_from_json();  // map_info -> widgets
    void sync_json_from_ui();  // widgets -> map_info["map_light_data"]
    nlohmann::json& ensure_light(); // ensures map_light_data exists and returns ref

    // Keys handling (angle + RGBA), simple pager-based editor
    void ensure_keys_array();
    void clamp_key_index();
    void select_prev_key();
    void select_next_key();
    void add_key_pair_at_current_angle(); // add angle and angle+180 (mod 360) with current color
    void delete_current_key();

    // Helpers
    static int clamp_int(int v, int lo, int hi);
    static float clamp_float(float v, float lo, float hi);
    static float wrap_angle(float a); // 0..360

private:
    // External data
    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;

    // Cached state (for editor)
    int current_key_index_ = 0;

    // Widgets
    // Top sliders
    std::unique_ptr<DMSlider> radius_;
    std::unique_ptr<DMSlider> intensity_;
    std::unique_ptr<DMSlider> orbit_radius_;
    std::unique_ptr<DMSlider> update_interval_;
    std::unique_ptr<DMSlider> mult_x100_;     // 0..100 -> 0.0..1.0
    std::unique_ptr<DMSlider> falloff_;       // 0..100
    std::unique_ptr<DMSlider> min_opacity_;   // 0..255
    std::unique_ptr<DMSlider> max_opacity_;   // 0..255

    // Base color RGBA
    std::unique_ptr<DMSlider> base_r_;
    std::unique_ptr<DMSlider> base_g_;
    std::unique_ptr<DMSlider> base_b_;
    std::unique_ptr<DMSlider> base_a_;

    // Keys controls (pager + angle + color)
    std::unique_ptr<DMButton> prev_key_btn_;
    std::unique_ptr<DMButton> next_key_btn_;
    std::unique_ptr<DMButton> add_pair_btn_;
    std::unique_ptr<DMButton> delete_btn_;

    std::unique_ptr<DMSlider> key_angle_; // 0..360
    std::unique_ptr<DMSlider> key_r_;
    std::unique_ptr<DMSlider> key_g_;
    std::unique_ptr<DMSlider> key_b_;
    std::unique_ptr<DMSlider> key_a_;

    // For showing the current key label in content render
    mutable std::string current_key_label_;

    // Non-owning wrappers registered with DockableCollapsible rows
    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    // Internal flag to debounce bulk syncs
    bool needs_sync_to_json_ = false;
};
