#include "core/manifest/manifest_loader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace manifest {
namespace {
std::filesystem::path project_root() {
#ifdef PROJECT_ROOT
    return std::filesystem::path(PROJECT_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

std::string build_missing_key_message(const std::string& key, const std::filesystem::path& path) {
    std::ostringstream oss;
    oss << "Manifest at '" << path.string() << "' is missing required top-level entry '" << key << "'.";
    return oss.str();
}

} // namespace

std::string manifest_path() {
    return (project_root() / "manifest.json").string();
}

ManifestData load_manifest() {
    const std::filesystem::path path = project_root() / "manifest.json";

    std::ifstream manifest_stream(path);
    if (!manifest_stream.is_open()) {
        std::ostringstream oss;
        oss << "Unable to open manifest file at '" << path.string() << "'.";
        throw std::runtime_error(oss.str());
    }

    nlohmann::json manifest_json;
    try {
        manifest_stream >> manifest_json;
    } catch (const nlohmann::json::parse_error& error) {
        std::ostringstream oss;
        oss << "Failed to parse manifest at '" << path.string() << "': " << error.what();
        throw std::runtime_error(oss.str());
    }

    for (const std::string key : {"assets", "maps", "rooms"}) {
        if (!manifest_json.contains(key)) {
            throw std::runtime_error(build_missing_key_message(key, path));
        }
    }

    if (!manifest_json.at("assets").is_object()) {
        std::ostringstream oss;
        oss << "Manifest entry 'assets' at '" << path.string() << "' must be a JSON object.";
        throw std::runtime_error(oss.str());
    }

    if (!manifest_json.at("maps").is_object()) {
        std::ostringstream oss;
        oss << "Manifest entry 'maps' at '" << path.string() << "' must be a JSON object.";
        throw std::runtime_error(oss.str());
    }

    if (!manifest_json.at("rooms").is_array()) {
        std::ostringstream oss;
        oss << "Manifest entry 'rooms' at '" << path.string() << "' must be a JSON array.";
        throw std::runtime_error(oss.str());
    }

    ManifestData data;
    data.raw = manifest_json;
    data.assets = data.raw.at("assets");
    data.maps = data.raw.at("maps");
    data.rooms = data.raw.at("rooms");

    return data;
}

} // namespace manifest

