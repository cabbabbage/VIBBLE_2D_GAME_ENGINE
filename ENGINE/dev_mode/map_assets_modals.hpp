 #pragma once

#include <functional>
#include <memory>
#include <string>

#include <SDL.h>
#include <nlohmann/json.hpp>

class Input;
 struct SDL_Renderer;
class CandidateListPanel;

class SingleSpawnGroupModal {
 public:
    using SaveCallback = std::function<bool()>;
    using RegenCallback = std::function<void(const nlohmann::json&)>;

     SingleSpawnGroupModal();
     ~SingleSpawnGroupModal();

    void open(nlohmann::json& map_info, const std::string& section_key, const std::string& default_display_name, const std::string& ownership_label, SDL_Color ownership_color, SaveCallback on_save, RegenCallback on_regen);

    void close();
    bool visible() const;

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_point_inside(int x, int y) const;

    void set_screen_dimensions(int width, int height);
    void set_floating_stack_key(std::string key);
    void set_on_open_area(std::function<void(const std::string&, const std::string&)> cb);
    void set_on_close(std::function<void()> cb);

 private:
    void ensure_single_group(nlohmann::json& section, const std::string& default_display_name);
    void ensure_visible_position();

    nlohmann::json* map_info_ = nullptr;
    nlohmann::json* section_ = nullptr;
    nlohmann::json* entry_ = nullptr;
    SaveCallback on_save_{};
    RegenCallback on_regen_{};

    std::unique_ptr<CandidateListPanel> panel_;

    int screen_w_ = 1920;
    int screen_h_ = 1080;
    bool position_initialized_ = false;
    std::string stack_key_;
    std::function<void(const std::string&, const std::string&)> on_open_area_{};
    std::function<void()> on_close_{};
};
