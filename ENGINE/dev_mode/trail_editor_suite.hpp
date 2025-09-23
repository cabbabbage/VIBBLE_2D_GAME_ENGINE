#pragma once

#include <SDL.h>

#include <memory>
#include <string>

class Input;
class Room;
class SDL_Renderer;

class RoomConfigurator;
class SpawnGroupsConfig;

namespace nlohmann { class json; }

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

private:
    void ensure_ui();
    void update_bounds();
    void rebuild_spawn_groups_ui();
    void open_spawn_group_editor(const std::string& id);
    void duplicate_spawn_group(const std::string& id);
    void delete_spawn_group(const std::string& id);
    void add_spawn_group();
    nlohmann::json* find_spawn_entry(const std::string& id);

    int screen_w_ = 0;
    int screen_h_ = 0;
    SDL_Rect config_bounds_{0, 0, 0, 0};

    Room* active_trail_ = nullptr;
    std::unique_ptr<RoomConfigurator> configurator_;
    std::unique_ptr<SpawnGroupsConfig> spawn_groups_;
};

