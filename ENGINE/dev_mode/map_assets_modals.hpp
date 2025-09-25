 #pragma once

 #include <memory>
 #include <functional>
 #include <string>
 #include <nlohmann/json.hpp>
 #include <SDL.h>

 class Input;
 struct SDL_Renderer;
 class SpawnGroupConfigUI;

 // Simple wrapper around a single SpawnGroupConfigUI used as a floating modal
 // to edit exactly one spawn group entry under a specific map_info section.
 class SingleSpawnGroupModal {
 public:
     using SaveCallback = std::function<void()>;

     SingleSpawnGroupModal();
     ~SingleSpawnGroupModal();

     // Open for a given section key inside map_info (e.g., "map_assets_data" or "map_boundary_data").
     // Ensures a single spawn group exists and configures UI restrictions.
     void open(nlohmann::json& map_info,
               const std::string& section_key,
               const std::string& default_display_name,
               const std::string& ownership_label,
               SDL_Color ownership_color,
               SaveCallback on_save);

     void close();
     bool visible() const;

     void update(const Input& input);
     bool handle_event(const SDL_Event& e);
     void render(SDL_Renderer* r) const;
     bool is_point_inside(int x, int y) const;

 private:
     void ensure_single_group(nlohmann::json& section,
                              const std::string& default_display_name);

     // References while open
     nlohmann::json* map_info_ = nullptr;
     nlohmann::json* section_ = nullptr;
     SaveCallback on_save_{};

     std::unique_ptr<SpawnGroupConfigUI> cfg_;
 };
