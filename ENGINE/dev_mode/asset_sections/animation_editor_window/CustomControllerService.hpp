#pragma once

#include <filesystem>
#include <string>

namespace animation_editor {

class CustomControllerService {
  public:
    CustomControllerService();

    void set_asset_root(const std::filesystem::path& asset_root);

    void create_new_controller(const std::string& controller_name);
    void open_existing_controller(const std::string& controller_name);
    void register_controller_with_animation(const std::string& controller_name, const std::string& animation_id);

  private:
    std::filesystem::path asset_root_;
};

}  // namespace animation_editor

