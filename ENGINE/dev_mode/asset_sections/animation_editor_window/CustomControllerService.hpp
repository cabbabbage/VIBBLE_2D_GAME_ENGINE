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
    void register_controller_with_animation(const std::string& controller_name,
                                            const std::string& animation_id);

  private:
    std::string sanitize_controller_name(const std::string& controller_name) const;
    std::string default_controller_name() const;
    static std::string to_pascal_case(const std::string& base_name);
    static std::string build_header_guard(const std::string& base_name);
    void write_controller_files(const std::filesystem::path& header_path,
                                const std::filesystem::path& source_path,
                                const std::string& base_name,
                                const std::string& class_name) const;
    void ensure_controller_factory_registration(const std::string& base_name,
                                                const std::string& class_name) const;
    void update_asset_metadata(const std::string& base_name,
                               const std::string& animation_id) const;
    std::filesystem::path resolve_engine_root(const std::filesystem::path& start) const;
    void open_in_default_editor(const std::filesystem::path& path) const;

    std::filesystem::path asset_root_;
    std::filesystem::path asset_info_path_;
    std::filesystem::path engine_root_;
    std::filesystem::path controller_dir_;
    std::filesystem::path controller_factory_cpp_;
    std::string asset_name_;
};

}  // namespace animation_editor

