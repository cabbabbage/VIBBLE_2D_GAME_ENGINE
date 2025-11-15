#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

#include "SlidingWindowContainer.hpp"

class Input;
struct SDL_Renderer;
class DMButton;

class MapRoomsDisplay {
public:
    using SelectRoomCallback = std::function<void(const std::string&)>;

    MapRoomsDisplay();
    ~MapRoomsDisplay();

    void attach_container(SlidingWindowContainer* container);
    void detach_container();

    void set_map_info(nlohmann::json* map_info);
    void set_on_select_room(SelectRoomCallback cb);
    void set_header_text(const std::string& text);
    void refresh();
    void set_on_rooms_changed(std::function<void()> cb);
    void set_on_create_room(std::function<void()> cb);

private:
    struct RoomRow {
        std::string key;
        std::string name;
        SDL_Rect rect{0, 0, 0, 0};
        SDL_Rect delete_rect{0, 0, 0, 0};
        SDL_Color display_color{180, 188, 202, 255};
};

    void configure_container(SlidingWindowContainer& container);
    void clear_container_callbacks(SlidingWindowContainer& container);

    int layout_content(const SlidingWindowContainer::LayoutContext& ctx);
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    void update(const Input& input, int screen_w, int screen_h);

    void rebuild_rows();
    void set_hovered_room(const std::string& key);
    void clear_hover();
    void create_room_entry();
    void delete_room_entry(const std::string& key);

private:
    SlidingWindowContainer* container_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    std::vector<RoomRow> rooms_;
    std::string hovered_room_;
    std::string hovered_delete_room_;
    SelectRoomCallback on_select_room_{};
    std::string header_text_ = "Map Rooms";
    std::unique_ptr<DMButton> create_room_button_;
    std::function<void()> on_rooms_changed_{};
    std::function<void()> on_create_room_{};
};

