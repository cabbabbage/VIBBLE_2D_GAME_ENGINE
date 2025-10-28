#include "CustomControllerService.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "dev_mode/core/manifest_store.hpp"
#include "string_utils.hpp"

namespace animation_editor {
namespace {

std::string trim_left_copy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    return std::string(begin, value.end());
}

std::string quote_argument(const std::filesystem::path& path) {
    std::string raw = path.string();
    std::string quoted;
    quoted.reserve(raw.size() + 2);
    quoted.push_back('"');
    for (char ch : raw) {
        if (ch == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

}

CustomControllerService::CustomControllerService() = default;

void CustomControllerService::set_asset_root(const std::filesystem::path& asset_root) {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::absolute(asset_root, ec);
    if (ec) {
        normalized = asset_root;
    }

    if (std::filesystem::is_regular_file(normalized, ec)) {
        normalized = normalized.parent_path();
    }

    if (normalized.empty()) {
        throw std::runtime_error("Asset root path is empty");
    }

    asset_root_ = normalized.lexically_normal();

    asset_name_ = asset_root_.filename().string();
    if (asset_name_.empty() && asset_root_.has_parent_path()) {
        asset_name_ = asset_root_.parent_path().filename().string();
    }

    engine_root_ = resolve_engine_root(asset_root_);
    if (engine_root_.empty()) {
        throw std::runtime_error("Unable to locate ENGINE directory from " + asset_root_.string());
    }

    controller_dir_ = engine_root_ / "animation_update" / "custom_controllers";
    controller_factory_cpp_ = engine_root_ / "asset" / "controller_factory.cpp";
}

void CustomControllerService::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
}

void CustomControllerService::set_manifest_asset_key(std::string asset_key) {
    manifest_asset_key_ = std::move(asset_key);
}

void CustomControllerService::create_new_controller(const std::string& controller_name) {
    if (asset_root_.empty()) {
        throw std::runtime_error("Asset root has not been configured");
    }

    std::string base_name = sanitize_controller_name(controller_name);
    if (base_name.empty()) {
        base_name = default_controller_name();
    }

    if (base_name.empty()) {
        throw std::runtime_error("Unable to determine a controller name");
    }

    std::error_code ec;
    const std::filesystem::path header_path = controller_dir_ / (base_name + ".hpp");
    const std::filesystem::path source_path = controller_dir_ / (base_name + ".cpp");

    bool header_exists = std::filesystem::exists(header_path, ec);
    bool source_exists = std::filesystem::exists(source_path, ec);
    std::string class_name = to_pascal_case(base_name);

    if (!header_exists || !source_exists) {
        write_controller_files(header_path, source_path, base_name, class_name);
    }

    ensure_controller_factory_registration(base_name, class_name);
    update_asset_metadata(base_name, std::string());
}

void CustomControllerService::open_existing_controller(const std::string& controller_name) {
    if (controller_dir_.empty()) {
        throw std::runtime_error("Asset root has not been configured");
    }

    std::string base_name = sanitize_controller_name(controller_name);
    if (base_name.empty()) {
        base_name = default_controller_name();
    }

    if (base_name.empty()) {
        throw std::runtime_error("Unable to determine a controller name");
    }

    std::error_code ec;
    std::filesystem::path header_path = controller_dir_ / (base_name + ".hpp");
    std::filesystem::path source_path = controller_dir_ / (base_name + ".cpp");

    if (std::filesystem::exists(header_path, ec)) {
        open_in_default_editor(header_path);
        return;
    }
    if (std::filesystem::exists(source_path, ec)) {
        open_in_default_editor(source_path);
        return;
    }

    throw std::runtime_error("Custom controller files do not exist for " + base_name);
}

void CustomControllerService::register_controller_with_animation(const std::string& controller_name,
                                                                 const std::string& animation_id) {
    if (asset_root_.empty()) {
        throw std::runtime_error("Asset root has not been configured");
    }

    std::string base_name = sanitize_controller_name(controller_name);
    if (base_name.empty()) {
        base_name = default_controller_name();
    }

    if (base_name.empty()) {
        throw std::runtime_error("Unable to determine a controller name");
    }

    update_asset_metadata(base_name, animation_id);
}

std::string CustomControllerService::sanitize_controller_name(const std::string& controller_name) const {
    std::string trimmed = strings::trim_copy(controller_name);
    if (trimmed.empty()) {
        return std::string();
    }

    std::string result;
    result.reserve(trimmed.size());
    bool last_was_underscore = false;
    for (char ch : trimmed) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            result.push_back(ch);
            last_was_underscore = false;
        } else if (ch == '_' || ch == '-' || std::isspace(uch)) {
            if (!result.empty() && !last_was_underscore) {
                result.push_back('_');
                last_was_underscore = true;
            }
        }
    }

    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    if (!result.empty() && !std::isalpha(static_cast<unsigned char>(result.front()))) {
        result = std::string("Controller_") + result;
    }

    return result;
}

std::string CustomControllerService::default_controller_name() const {
    if (asset_name_.empty()) {
        return std::string();
    }
    return sanitize_controller_name(asset_name_ + "_controller");
}

std::string CustomControllerService::to_pascal_case(const std::string& base_name) {
    std::string normalized = base_name;
    std::replace(normalized.begin(), normalized.end(), '-', '_');

    std::stringstream stream(normalized);
    std::string part;
    std::string result;
    while (std::getline(stream, part, '_')) {
        if (part.empty()) {
            continue;
        }
        part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
        for (std::size_t i = 1; i < part.size(); ++i) {
            part[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(part[i])));
        }
        result += part;
    }

    if (result.empty()) {
        result = "CustomController";
    }
    return result;
}

std::string CustomControllerService::build_header_guard(const std::string& base_name) {
    std::string guard;
    guard.reserve(base_name.size() + 4);
    for (char ch : base_name) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            guard.push_back(static_cast<char>(std::toupper(uch)));
        } else {
            guard.push_back('_');
        }
    }
    if (guard.empty()) {
        guard = "CUSTOM_CONTROLLER";
    }
    if (!std::isalpha(static_cast<unsigned char>(guard.front()))) {
        guard.insert(guard.begin(), 'C');
    }
    guard += "_HPP";
    return guard;
}

void CustomControllerService::write_controller_files(const std::filesystem::path& header_path,
                                                     const std::filesystem::path& source_path,
                                                     const std::string& base_name,
                                                     const std::string& class_name) const {
    std::error_code ec;
    bool header_exists = std::filesystem::exists(header_path, ec);
    bool source_exists = std::filesystem::exists(source_path, ec);

    if (header_exists && source_exists) {
        return;
    }

    std::filesystem::create_directories(header_path.parent_path());

    if (class_name.empty()) {
        throw std::runtime_error("Controller class name cannot be empty");
    }

    const std::string guard = build_header_guard(base_name);

    if (!header_exists) {
        std::ofstream hpp(header_path);
        if (!hpp.is_open()) {
            throw std::runtime_error("Failed to write header file: " + header_path.string());
        }

        hpp << "#ifndef " << guard << "\n";
        hpp << "#define " << guard << "\n\n";
        hpp << "#include \"asset/asset_controller.hpp\"\n\n";
        hpp << "class Assets;\n";
        hpp << "class Asset;\n";
        hpp << "class Input;\n\n";
        hpp << "class " << class_name << " : public AssetController {\n\n";
        hpp << "public:\n";
        hpp << "    " << class_name << "(Assets* assets, Asset* self);\n";
        hpp << "    ~" << class_name << "() override = default;\n";
        hpp << "    void update(const Input& in) override;\n\n";
        hpp << "private:\n";
        hpp << "    Assets* assets_ = nullptr;\n";
        hpp << "    Asset*  self_   = nullptr;\n";
        hpp << "};\n\n";
        hpp << "#endif\n";
    }

    if (!source_exists) {
        std::ofstream cpp(source_path);
        if (!cpp.is_open()) {
            throw std::runtime_error("Failed to write source file: " + source_path.string());
        }

        cpp << "#include \"" << base_name << ".hpp\"\n\n";
        cpp << "#include \"asset/Asset.hpp\"\n";
        cpp << "#include \"core/AssetsManager.hpp\"\n";
        cpp << "#include \"map_generation/room.hpp\"\n\n";
        cpp << class_name << "::" << class_name << "(Assets* assets, Asset* self)\n";
        cpp << "    : assets_(assets), self_(self) {\n";
        cpp << "}\n\n";
        cpp << "void " << class_name << "::update(const Input& ) {\n";
        cpp << "    if (!self_) {\n";
        cpp << "        return;\n";
        cpp << "    }\n";
        cpp << "\n";
        cpp << "    const Room* current_room = assets_ ? assets_->current_room() : nullptr;\n";
        cpp << "    const auto trigger_areas = assets_\n";
        cpp << "        ? assets_->current_room_trigger_areas()\n";
        cpp << "        : std::vector<const Room::NamedArea*>{};\n";
        cpp << "    (void)current_room;\n";
        cpp << "    (void)trigger_areas;\n";
        cpp << "}\n";
    }
}

void CustomControllerService::ensure_controller_factory_registration(const std::string& base_name,
                                                                     const std::string& class_name) const {
    if (controller_factory_cpp_.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(controller_factory_cpp_, ec)) {
        return;
    }

    std::ifstream input(controller_factory_cpp_);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open controller factory: " + controller_factory_cpp_.string());
    }

    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    bool had_trailing_newline = !content.empty() && content.back() == '\n';

    std::istringstream stream(content);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    bool modified = false;

    const std::string include_line = "#include \"animation_update/custom_controllers/" + base_name + ".hpp\"";
    if (content.find(include_line) == std::string::npos) {
        int insert_index = -1;
        int last_include_index = -1;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::string trimmed = trim_left_copy(lines[i]);
            if (trimmed.rfind("#include", 0) == 0) {
                last_include_index = static_cast<int>(i);
                if (lines[i].find("animation_update/custom_controllers/") != std::string::npos) {
                    insert_index = static_cast<int>(i) + 1;
                }
            }
        }
        if (insert_index < 0) {
            insert_index = last_include_index >= 0 ? last_include_index + 1 : 0;
        }
        lines.insert(lines.begin() + insert_index, include_line);
        modified = true;
    }

    const std::string branch_prefix = "                if (key == \"" + base_name + "\")";
    bool branch_exists = std::any_of(lines.begin(), lines.end(), [&](const std::string& existing) {
        return existing.find(branch_prefix) != std::string::npos;
    });

    if (!branch_exists) {
        std::vector<std::string> branch_lines = {
            "                if (key == \"" + base_name + "\")",
            "                        return std::make_unique<" + class_name + ">(assets_, self);"
};

        auto catch_it = std::find_if(lines.begin(), lines.end(), [](const std::string& value) {
            return value.find("} catch") != std::string::npos;
        });

        if (catch_it != lines.end()) {
            std::size_t insert_pos = static_cast<std::size_t>(std::distance(lines.begin(), catch_it));
            if (insert_pos > 0 && !lines[insert_pos - 1].empty()) {
                lines.insert(lines.begin() + insert_pos, std::string());
                ++insert_pos;
            }
            lines.insert(lines.begin() + insert_pos, branch_lines.begin(), branch_lines.end());
            modified = true;
        }
    }

    if (!modified) {
        return;
    }

    std::ofstream output(controller_factory_cpp_);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to write controller factory: " + controller_factory_cpp_.string());
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        output << lines[i];
        if (i + 1 < lines.size() || had_trailing_newline) {
            output << '\n';
        }
    }
}

void CustomControllerService::update_asset_metadata(const std::string& base_name,
                                                    const std::string& animation_id) const {
    if (!manifest_store_) {
        throw std::runtime_error("Manifest store is not configured for custom controller updates.");
    }
    if (manifest_asset_key_.empty()) {
        throw std::runtime_error("Manifest asset key has not been set for controller updates.");
    }

    auto transaction = manifest_store_->begin_asset_transaction(manifest_asset_key_, true);
    if (!transaction) {
        throw std::runtime_error("Failed to begin manifest transaction for asset " + manifest_asset_key_);
    }

    nlohmann::json& data = transaction.data();
    if (!data.is_object()) {
        data = nlohmann::json::object();
    }

    data["custom_controller_key"] = base_name;

    if (!animation_id.empty()) {
        nlohmann::json* animations_container = nullptr;
        auto animations_it = data.find("animations");
        if (animations_it != data.end()) {
            if (animations_it->is_object()) {
                animations_container = &(*animations_it);
            }
        }

        if (animations_container) {
            auto entry_it = animations_container->find(animation_id);
            if (entry_it != animations_container->end() && entry_it->is_object()) {
                (*entry_it)["custom_animation_controller_key"] = base_name;
                (*entry_it)["custom_animation_controller_hpp_path"] = (controller_dir_ / (base_name + ".hpp")).string();
                (*entry_it)["has_custom_animation_controller"] = true;
            }
        }
    }

    if (!transaction.save()) {
        throw std::runtime_error("Failed to persist manifest update for " + manifest_asset_key_);
    }
}

std::filesystem::path CustomControllerService::resolve_engine_root(const std::filesystem::path& start) const {
    if (start.empty()) {
        return std::filesystem::path();
    }

    std::filesystem::path cursor = start;
    std::error_code ec;
    while (true) {
        std::filesystem::path candidate = cursor / "ENGINE";
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ec);
            return ec ? candidate : resolved;
        }
        if (!cursor.has_parent_path() || cursor == cursor.parent_path()) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return std::filesystem::path();
}

void CustomControllerService::open_in_default_editor(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
    }

    if (!std::filesystem::exists(absolute, ec)) {
        throw std::runtime_error("Controller file not found: " + absolute.string());
    }

#ifdef _WIN32
    const std::string command = "cmd /c start \"\" " + quote_argument(absolute);
#elif defined(__APPLE__)
    const std::string command = "open " + quote_argument(absolute);
#else
    const std::string command = "xdg-open " + quote_argument(absolute);
#endif

    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error("Failed to launch editor for " + absolute.string());
    }
}

}

