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
class FullScreenCollapsible;
class Room;

class AssetFilterBar {
public:
    using StateChangedCallback = std::function<void()>;

    AssetFilterBar();
    ~AssetFilterBar();

    void initialize();

    void set_state_changed_callback(StateChangedCallback cb);
    void set_enabled(bool enabled);
    void set_screen_dimensions(int width, int height);
    void set_footer_panel(FullScreenCollapsible* footer);
    void set_map_info(nlohmann::json* map_info);
    void set_current_room(Room* room);

    void refresh_layout();
    void ensure_layout();

    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& event);
    bool contains_point(int x, int y) const;

    void reset();

    bool passes(const Asset& asset) const;

private:
    enum class FilterKind { MapAssets, CurrentRoom, Type };

    struct FilterEntry {
        std::string id;
        FilterKind kind = FilterKind::Type;
        std::unique_ptr<DMCheckbox> checkbox;
    };

    struct FilterState {
        bool map_assets = false;
        bool current_room = true;
        std::unordered_map<std::string, bool> type_filters;
    };

    void rebuild_map_spawn_ids();
    void rebuild_room_spawn_ids();
    void rebuild_layout(const SDL_Rect& header_rect);
    void sync_state_from_ui();
    void notify_state_changed();
    bool type_filter_enabled(const std::string& type) const;
    std::string format_type_label(const std::string& type) const;
    void collect_spawn_ids(const nlohmann::json& node, std::unordered_set<std::string>& out) const;

    bool enabled_ = true;
    int screen_w_ = 0;
    int screen_h_ = 0;
    FullScreenCollapsible* footer_ = nullptr;
    nlohmann::json* map_info_json_ = nullptr;
    Room* current_room_ = nullptr;

    std::vector<FilterEntry> entries_;
    FilterState state_{};
    SDL_Rect layout_bounds_{0, 0, 0, 0};
    SDL_Rect cached_header_rect_{0, 0, 0, 0};
    bool layout_dirty_ = true;
    std::unordered_set<std::string> map_spawn_ids_;
    std::unordered_set<std::string> room_spawn_ids_;
    StateChangedCallback on_state_changed_{};
};

