#include "CustomControllerService.hpp"

namespace animation_editor {

CustomControllerService::CustomControllerService() = default;

void CustomControllerService::set_asset_root(const std::filesystem::path& asset_root) {
    asset_root_ = asset_root;
    // TODO: Track base directory for controller templates and scripts.
}

void CustomControllerService::create_new_controller(const std::string& controller_name) {
    (void)controller_name;
    // TODO: Generate a new controller file and open it in the developer's editor.
}

void CustomControllerService::open_existing_controller(const std::string& controller_name) {
    (void)controller_name;
    // TODO: Launch IDE pointing to the selected controller file.
}

void CustomControllerService::register_controller_with_animation(const std::string& controller_name, const std::string& animation_id) {
    (void)controller_name;
    (void)animation_id;
    // TODO: Update animation metadata to reference the chosen custom controller.
}

}  // namespace animation_editor

