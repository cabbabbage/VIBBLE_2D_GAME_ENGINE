#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json_fwd.hpp>

class Asset;
class DMCheckbox;
class Room;

class AssetFilterBar {
public:
    using StateChangedCallback = std::function<void()>;
    using ExtraRenderer = std::function<void(SDL_Renderer*, const SDL_Rect&)>;
    using ExtraEventHandler = std::function<bool(const SDL_Event&, const SDL_Rect&)>;
    struct ModeButtonConfig {
        std::string id;
        std::string label;
        bool active = false;
};

    AssetFilterBar();
    ~AssetFilterBar();

    void initialize();

    void set_state_changed_callback(StateChangedCallback cb);
    void set_enabled(bool enabled);
    void set_screen_dimensions(int width, int height);
    void set_map_info(nlohmann::json* map_info);
    void set_current_room(Room* room);
    void set_mode_buttons(std::vector<ModeButtonConfig> buttons);
    void set_mode_changed_callback(std::function<void(const std::string&)> cb);
    void set_active_mode(const std::string& id, bool trigger_callback = false);
    void set_filters_expanded(bool expanded);
    bool filters_expanded() const { return filters_expanded_; }

    void set_header_suppressed(bool suppressed) { header_suppressed_ = suppressed; }
    bool header_suppressed() const { return header_suppressed_; }

    void refresh_layout();
    void ensure_layout();

    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& event);
    bool contains_point(int x, int y) const;

    const SDL_Rect& header_rect() const { return header_rect_; }
    const SDL_Rect& layout_bounds() const { return layout_bounds_; }

    void set_right_accessory_width(int width) { right_accessory_width_ = std::max(0, width); layout_dirty_ = true; }
    int right_accessory_width() const { return right_accessory_width_; }

    void set_extra_panel_height(int height) { extra_panel_height_ = std::max(0, height); layout_dirty_ = true; }
    void set_extra_panel_renderer(ExtraRenderer renderer) { extra_renderer_ = std::move(renderer); }
    void set_extra_panel_event_handler(ExtraEventHandler handler) { extra_event_handler_ = std::move(handler); }

    void reset();

    bool passes(const Asset& asset) const;
    bool render_dark_mask_enabled() const;

private:
    enum class FilterKind { MapAssets, CurrentRoom, RenderDarkMask, Type, SpawnMethod };

    struct FilterEntry {
        std::string id;
        FilterKind kind = FilterKind::Type;
        std::unique_ptr<DMCheckbox> checkbox;
};

    struct FilterState {
        bool map_assets = false;
        bool current_room = true;
        bool render_dark_mask = true;
        std::unordered_map<std::string, bool> type_filters;
        std::unordered_map<std::string, bool> method_filters;
};

    void rebuild_map_spawn_ids();
    void rebuild_room_spawn_ids();
    void rebuild_layout();
    void sync_state_from_ui();
    void load_persisted_state();
    void persist_state();
    void persist_filters_expanded() const;
    FilterState& mutable_state();
    const FilterState& state() const;
    void notify_state_changed();
    bool type_filter_enabled(const std::string& type) const;
    bool method_filter_enabled(const std::string& method) const;
    bool default_type_enabled(const std::string& type) const;
    bool default_method_enabled(const std::string& method) const;
    bool load_type_filter_value(const std::string& type, bool default_value) const;
    bool load_method_filter_value(const std::string& method, bool default_value) const;
    std::string format_type_label(const std::string& type) const;
    std::string format_method_label(const std::string& method) const;
    static std::string canonicalize_method(const std::string& method);
    void collect_spawn_ids(const nlohmann::json& node, std::unordered_set<std::string>& out) const;
    void update_filter_toggle_label();
    void clear_checkbox_rects();
    void layout_mode_buttons();
    void layout_filter_checkboxes();

    static FilterState& persistent_state();
    static bool& persistent_state_initialized_flag();
    static bool& persistent_state_loaded_flag();
    static bool& persistent_filters_expanded_flag();
    static void ensure_persistent_state_loaded();

    bool enabled_ = true;
    int screen_w_ = 0;
    int screen_h_ = 0;
    nlohmann::json* map_info_json_ = nullptr;
    Room* current_room_ = nullptr;

    std::vector<FilterEntry> entries_;
    FilterState* state_ = nullptr;
    bool has_saved_state_ = false;
    SDL_Rect layout_bounds_{0, 0, 0, 0};
    SDL_Rect mode_bar_rect_{0, 0, 0, 0};
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect filters_rect_{0, 0, 0, 0};
    bool layout_dirty_ = true;
    std::unordered_set<std::string> map_spawn_ids_;
    std::unordered_set<std::string> room_spawn_ids_;
    StateChangedCallback on_state_changed_{};
    struct ModeButtonEntry {
        ModeButtonConfig config;
        std::unique_ptr<class DMButton> button;
};
    std::vector<ModeButtonEntry> mode_buttons_;
    std::function<void(const std::string&)> on_mode_selected_{};
    std::unique_ptr<class DMButton> filter_toggle_button_;
    bool filters_expanded_ = false;
    bool header_suppressed_ = false;
    int right_accessory_width_ = 0;
    int extra_panel_height_ = 0;
    SDL_Rect extra_panel_rect_{0,0,0,0};
    ExtraRenderer extra_renderer_{};
    ExtraEventHandler extra_event_handler_{};
};
