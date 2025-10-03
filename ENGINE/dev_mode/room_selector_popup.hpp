#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

class DMButton;
class DMTextBox;
class Input;

class RoomSelectorPopup {
public:
    using RoomCallback = std::function<void(const std::string&)>;
    using SuggestRoomFn = std::function<std::string()>;
    using CreateRoomFn = std::function<std::string(const std::string&)>;

    RoomSelectorPopup();
    ~RoomSelectorPopup();

    void set_anchor_rect(const SDL_Rect& rect);
    void set_screen_bounds(const SDL_Rect& bounds);
    void set_create_callbacks(SuggestRoomFn suggest_cb, CreateRoomFn create_cb);

    void open(const std::vector<std::string>& rooms, RoomCallback cb);
    void set_rooms(const std::vector<std::string>& rooms);
    void close();

    bool visible() const { return visible_; }

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    bool is_point_inside(int x, int y) const;

private:
    void rebuild_room_buttons();
    void ensure_geometry() const;
    void layout_widgets() const;
    void scroll_by(int delta);
    void position_from_anchor() const;

    SDL_Rect anchor_rect_{0, 0, 0, 0};
    mutable SDL_Rect rect_{0, 0, 280, 320};
    SDL_Rect screen_bounds_{0, 0, 0, 0};

    bool visible_ = false;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::string> rooms_;
    RoomCallback callback_{};

    mutable bool geometry_dirty_ = true;
    mutable int content_height_ = 0;
    mutable SDL_Rect content_clip_{0, 0, 0, 0};
    mutable int max_scroll_ = 0;
    mutable int scroll_offset_ = 0;

    SuggestRoomFn suggest_room_fn_{};
    CreateRoomFn create_room_fn_{};
};

