#include "core/manifest/manifest_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <chrono>

#include "utils/log.hpp"

namespace manifest {
namespace {
std::filesystem::path project_root() {
#ifdef PROJECT_ROOT
    return std::filesystem::path(PROJECT_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

nlohmann::json make_default_manifest_json() {
    nlohmann::json manifest_json = nlohmann::json::object();
    manifest_json["version"] = 1;
    manifest_json["assets"] = nlohmann::json::object();
    manifest_json["maps"] = nlohmann::json::object();
    return manifest_json;
}

ManifestData make_manifest_data(nlohmann::json manifest_json) {
    ManifestData data;
    data.raw = std::move(manifest_json);
    data.assets = data.raw.at("assets");
    data.maps = data.raw.at("maps");
    return data;
}

static nlohmann::json& cached_manifest_ref() {
    static nlohmann::json cached = make_default_manifest_json();
    return cached;
}

void ensure_directory_exists(const std::filesystem::path& dir,
                             const char* description) {
    if (dir.empty()) {
        return;
    }

    std::error_code ec;
    if (std::filesystem::create_directories(dir, ec)) {
        return;
    }

    if (ec && !std::filesystem::exists(dir)) {
        std::ostringstream oss;
        oss << "Failed to create " << description << " directory '"
            << dir.u8string() << "': " << ec.message();
        throw std::runtime_error(oss.str());
    }
}

void ensure_project_structure(const std::filesystem::path& root) {
    ensure_directory_exists(root / "SRC", "SRC root");
    ensure_directory_exists(root / "SRC" / "assets", "SRC assets");
    ensure_directory_exists(root / "SRC" / "misc_content", "SRC misc content");
    ensure_directory_exists(root / "SRC" / "loading_screen_content", "SRC loading screen content");
    ensure_directory_exists(root / "SRC" / "LOADING CONTENT", "SRC loading content");
}

void write_manifest_file(const std::filesystem::path& path,
                         const nlohmann::json& manifest_json) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        ensure_directory_exists(parent, "manifest parent");
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        std::ostringstream oss;
        oss << "Unable to open manifest file at '" << path.string() << "' for writing.";
        throw std::runtime_error(oss.str());
    }
    out << manifest_json.dump(2);
    if (!out.good()) {
        std::ostringstream oss;
        oss << "Failed while writing manifest file at '" << path.string() << "'.";
        throw std::runtime_error(oss.str());
    }

}

}

std::string manifest_path() {
    return (project_root() / "manifest.json").string();
}

ManifestData load_manifest() {
    const std::filesystem::path root = project_root();
    const std::filesystem::path path = root / "manifest.json";

    ensure_project_structure(root);

    if (!std::filesystem::exists(path)) {
        auto data = make_manifest_data(make_default_manifest_json());
        write_manifest_file(path, data.raw);
        return data;
    }

    auto read_once = [&](nlohmann::json& out) -> bool {
        std::ifstream is(path);
        if (!is.is_open()) return false;
        try {
            is >> out;
            return true;
        } catch (const nlohmann::json::parse_error&) {
            return false;
        }
};

    nlohmann::json manifest_json;
    if (!read_once(manifest_json)) {
        vibble::log::warn(std::string("manifest: parse error reading '") + path.string() + "', retrying shortly...");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!read_once(manifest_json)) {
            vibble::log::warn(std::string("manifest: still unable to parse '") + path.string() + "'; using cached manifest");
            manifest_json = cached_manifest_ref();
        }
    }

    bool mutated = false;
    if (!manifest_json.is_object()) {
        manifest_json = make_default_manifest_json();
        mutated = true;
    }

    if (!manifest_json.contains("version") || !manifest_json["version"].is_number()) {
        manifest_json["version"] = 1;
        mutated = true;
    }

    if (!manifest_json.contains("assets") || !manifest_json["assets"].is_object()) {
        manifest_json["assets"] = nlohmann::json::object();
        mutated = true;
    }

    if (!manifest_json.contains("maps") || !manifest_json["maps"].is_object()) {
        manifest_json["maps"] = nlohmann::json::object();
        mutated = true;
    }

    if (mutated) {
        write_manifest_file(path, manifest_json);
    }

    cached_manifest_ref() = manifest_json;
    return make_manifest_data(std::move(manifest_json));
}

void save_manifest(const ManifestData& data) {
    const std::filesystem::path root = project_root();
    const std::filesystem::path path = root / "manifest.json";
    write_manifest_file(path, data.raw);
}

}

