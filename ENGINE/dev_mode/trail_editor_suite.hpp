#pragma once

#include <SDL.h>

#include <cstddef>
#include <memory>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>

class Input;
class Room;
class SDL_Renderer;

class RoomConfigurator;
class TrailEditorSuite {
public:
    TrailEditorSuite();
    ~TrailEditorSuite();

    void set_screen_dimensions(int width, int height);

    void open(Room* trail);
    void close();
    bool is_open() const;

    void update(const Input& input);
    bool handle_event(const SDL_Event& event);
    void render(SDL_Renderer* renderer) const;
    bool contains_point(int x, int y) const;

    Room* active_trail() const { return active_trail_; }

    void set_on_open_area(std::function<void(const std::string&, const std::string&)> cb,
                          std::string stack_key = {});

private:
    void ensure_ui();
    void update_bounds();
    void delete_spawn_group(const std::string& id);
    void add_spawn_group();
    void reorder_spawn_group(const std::string& id, size_t new_index);
    nlohmann::json* find_spawn_entry(const std::string& id);

    int screen_w_ = 0;
    int screen_h_ = 0;
    SDL_Rect config_bounds_{0, 0, 0, 0};

    Room* active_trail_ = nullptr;
    std::unique_ptr<RoomConfigurator> configurator_;
    std::function<void(const std::string&, const std::string&)> on_open_area_{};
    std::string open_area_stack_key_{};
};

