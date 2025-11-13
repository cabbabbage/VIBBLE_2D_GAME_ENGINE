#pragma once

#include <array>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include <nlohmann/json.hpp>
#include "utils/ranged_color.hpp"

class Assets;

struct OrbitSettings;
struct ScreenLightSettings;
class DMColorRangeWidget;

class MapLightPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    MapLightPanel(int x = 40, int y = 40);
    ~MapLightPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_assets(Assets* assets);
    void set_update_map_light_callback(std::function<void(bool)> cb);

    using ColorSampleRequestCallback = std::function<void( const utils::color::RangedColor&, std::function<void(SDL_Color)>, std::function<void()>)>;
    void set_map_color_sample_callback(ColorSampleRequestCallback cb);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w = 0, int screen_h = 0);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    bool is_point_inside(int x, int y) const;

    const std::string& persistence_warning() const { return persistence_warning_text_; }

    nlohmann::json& mutable_light();
    bool commit_light_changes_external();

protected:

    void render_content(SDL_Renderer* r) const override;

private:

    void update_save_status(bool success) const;

    void build_ui();
    void rebuild_rows();
    void update_section_header_labels();
    void sync_ui_from_json();
    void sync_json_from_ui();
    void sync_chunk_slider_from_json();
    void persist_chunk_resolution();
    void load_update_map_light_setting();
    nlohmann::json& ensure_light();
    struct OrbitSettings {
        int update_interval = 10;
        int orbit_x = 0;
        int orbit_y = 0;
        bool operator==(const OrbitSettings& other) const {
            return update_interval == other.update_interval &&
                   orbit_x == other.orbit_x &&
                   orbit_y == other.orbit_y;
        }
};

    OrbitSettings sanitize_orbit_settings(const OrbitSettings& raw) const;
    OrbitSettings current_orbit_settings_from_ui() const;
    void set_orbit_sliders(const OrbitSettings& orbit);
    void write_orbit_settings_to_json(const OrbitSettings& orbit);
    void apply_immediate_settings();
    bool commit_light_changes();

    void ensure_keys_array();
    void rebuild_orbit_key_pairs_from_json();
    void write_orbit_pairs_to_json();
    void refresh_orbit_widget();
    void add_orbit_pair(double angle_degrees);
    void delete_orbit_pair(int index);
    void adjust_orbit_pair_angle(int index, int delta_degrees);
    void set_focused_pair(int index);
    void set_focused_pair_by_id(int id);
    void handle_pair_color_changed(int index, const utils::color::RangedColor& color);
    void handle_map_color_changed(const utils::color::RangedColor& color);
    void write_map_color_to_json();
    void set_map_color_widget_value(const utils::color::RangedColor& color);
    int find_pair_containing_angle(double angle_degrees) const;
    utils::color::RangedColor default_pair_color();

    static int clamp_int(int v, int lo, int hi);
    static float clamp_float(float v, float lo, float hi);
    static float wrap_angle(float a);

private:

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;
    nlohmann::json editing_light_{};

    Assets* assets_ = nullptr;

    std::unique_ptr<DMCheckbox> update_map_light_checkbox_;
    std::unique_ptr<DMButton> orbit_section_btn_;
    std::unique_ptr<DMButton> texture_section_btn_;
    bool orbit_section_collapsed_ = false;
    bool texture_section_collapsed_ = false;
    std::unique_ptr<DMSlider> orbit_x_;
    std::unique_ptr<DMSlider> orbit_y_;
    std::unique_ptr<DMSlider> update_interval_;
    std::unique_ptr<DMSlider> chunk_resolution_;
    int chunk_resolution_value_ = 0;

    class OrbitKeyWidget;
    class SectionToggleWidget;
    struct OrbitKeyPair {
        int id = 0;
        double angle = 0.0;
        utils::color::RangedColor color{};
};

    int next_pair_id_ = 1;
    std::vector<OrbitKeyPair> orbit_key_pairs_;
    int focused_pair_index_ = -1;
    OrbitKeyWidget* orbit_key_widget_ = nullptr;
    DMColorRangeWidget* map_color_widget_ = nullptr;
    utils::color::RangedColor map_color_{{0, 0}, {0, 0}, {0, 0}, {255, 255}};
    bool suppress_map_color_callback_ = false;
    ColorSampleRequestCallback map_color_sample_callback_{};
    mutable std::string persistence_warning_text_;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    class WarningLabel;
    WarningLabel* warning_label_ = nullptr;

    void toggle_orbit_section();
    void toggle_texture_section();
    bool needs_sync_to_json_ = false;

    bool update_map_light_enabled_ = false;
    std::function<void(bool)> update_map_light_callback_{};

    OrbitSettings last_applied_orbit_{};

    friend class OrbitKeyWidget;

    void sort_orbit_pairs();
    static double normalize_angle(double angle);

protected:
    std::string_view lock_settings_namespace() const override { return "lighting"; }
    std::string_view lock_settings_id() const override { return "map_panel"; }
};
