#include "dev_mode/core/manifest_store.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <limits>

#include "dev_mode/core/dev_json_store.hpp"
#include "dev_mode/tag_utils.hpp"

namespace devmode::core {
namespace {
std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
}

ManifestStore::AssetEditSession::AssetEditSession(ManifestStore* owner,
                                                  std::string name,
                                                  nlohmann::json draft,
                                                  bool is_new_asset)
    : owner_(owner),
      name_(std::move(name)),
      draft_(std::move(draft)),
      is_new_(is_new_asset) {}

bool ManifestStore::AssetEditSession::commit() {
    if (!owner_) {
        return false;
    }
    ManifestStore* owner = owner_;
    owner_ = nullptr;
    return owner->apply_edit(name_, draft_);
}

void ManifestStore::AssetEditSession::cancel() {
    owner_ = nullptr;
}

ManifestStore::AssetTransaction::AssetTransaction(ManifestStore* owner,
                                                  std::string name,
                                                  nlohmann::json draft,
                                                  bool is_new_asset)
    : owner_(owner),
      name_(std::move(name)),
      draft_(std::move(draft)),
      is_new_(is_new_asset) {}

bool ManifestStore::AssetTransaction::save() {
    if (!owner_) {
        return false;
    }
    return owner_->apply_edit(name_, draft_);
}

bool ManifestStore::AssetTransaction::finalize() {
    if (!save()) {
        return false;
    }
    owner_ = nullptr;
    return true;
}

void ManifestStore::AssetTransaction::cancel() {
    owner_ = nullptr;
}

ManifestStore::ManifestStore()
    : ManifestStore(manifest::manifest_path(),
                    []() { return manifest::load_manifest(); },
                    [](const std::filesystem::path& path, const nlohmann::json& data, int indent) {
                        DevJsonStore::instance().submit(path, data, indent);
                    },
                    []() { DevJsonStore::instance().flush_all(); },
                    2) {}

ManifestStore::ManifestStore(const std::filesystem::path& manifest_path,
                             std::function<manifest::ManifestData()> loader,
                             std::function<void(const std::filesystem::path&, const nlohmann::json&, int)> submit,
                             std::function<void()> flush,
                             int indent)
    : manifest_path_(manifest_path),
      loader_(std::move(loader)),
      submit_(std::move(submit)),
      flush_(std::move(flush)),
      indent_(indent) {
    if (!submit_) {
        submit_ = [](const std::filesystem::path& path, const nlohmann::json& data, int indent) {
            DevJsonStore::instance().submit(path, data, indent);
};
    }
    if (!flush_) {
        flush_ = []() { DevJsonStore::instance().flush_all(); };
    }
}

std::optional<std::string> ManifestStore::resolve_asset_name(const std::string& name) {
    ensure_loaded();

    if (!manifest_cache_.contains("assets") || !manifest_cache_["assets"].is_object()) {
        return std::nullopt;
    }

    nlohmann::json& assets = manifest_cache_["assets"];
    auto direct = assets.find(name);
    if (direct != assets.end()) {
        return name;
    }

    std::string needle = to_lower(name);
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        if (to_lower(it.key()) == needle) {
            return it.key();
        }
    }
    return std::nullopt;
}

ManifestStore::AssetView ManifestStore::get_asset(const std::string& name) {
    ensure_loaded();

    auto resolved = resolve_asset_name(name);
    if (!resolved) {
        return {};
    }

    nlohmann::json& assets = manifest_cache_["assets"];
    auto it = assets.find(*resolved);
    if (it == assets.end()) {
        return {};
    }
    return AssetView{*resolved, &(*it)};
}

ManifestStore::AssetEditSession ManifestStore::begin_asset_edit(const std::string& name,
                                                                bool create_if_missing) {
    ensure_loaded();
    ensure_asset_container();

    auto resolved = resolve_asset_name(name);
    if (!resolved && !create_if_missing) {
        return {};
    }

    const bool is_new_asset = !resolved.has_value();
    std::string target_name = resolved.has_value() ? *resolved : name;

    nlohmann::json existing = nlohmann::json::object();
    nlohmann::json& assets = manifest_cache_["assets"];
    if (!is_new_asset) {
        auto it = assets.find(target_name);
        if (it == assets.end()) {
            return {};
        }
        existing = *it;
    }

    return AssetEditSession(this, std::move(target_name), std::move(existing), is_new_asset);
}

ManifestStore::AssetTransaction ManifestStore::begin_asset_transaction(const std::string& name,
                                                                       bool create_if_missing) {
    ensure_loaded();
    ensure_asset_container();

    auto resolved = resolve_asset_name(name);
    if (!resolved && !create_if_missing) {
        return {};
    }

    const bool is_new_asset = !resolved.has_value();
    std::string target_name = resolved.has_value() ? *resolved : name;

    nlohmann::json existing = nlohmann::json::object();
    nlohmann::json& assets = manifest_cache_["assets"];
    if (!is_new_asset) {
        auto it = assets.find(target_name);
        if (it == assets.end()) {
            return {};
        }
        existing = *it;
    }

    return AssetTransaction(this, std::move(target_name), std::move(existing), is_new_asset);
}

bool ManifestStore::remove_asset(const std::string& name) {
    ensure_loaded();
    ensure_asset_container();

    auto resolved = resolve_asset_name(name);
    if (!resolved) {
        return false;
    }

    nlohmann::json& assets = manifest_cache_["assets"];
    auto erased = assets.erase(*resolved);
    if (erased == 0) {
        return false;
    }

    dirty_ = true;
    if (submit_) {
        submit_(manifest_path_, manifest_cache_, indent_);
    }
    return true;
}

void ManifestStore::reload() {
    loaded_ = false;
    dirty_ = false;
    manifest_cache_ = nlohmann::json::object();
    last_known_tag_version_ = std::numeric_limits<std::uint64_t>::max();
    ensure_loaded();
}

void ManifestStore::flush() {
    if (flush_) {
        flush_();
        dirty_ = false;
    }
}

const nlohmann::json& ManifestStore::manifest_json() {
    ensure_loaded();
    return manifest_cache_;
}

std::vector<ManifestStore::AssetView> ManifestStore::assets() {
    ensure_loaded();
    std::vector<AssetView> views;
    if (!manifest_cache_.contains("assets") || !manifest_cache_["assets"].is_object()) {
        return views;
    }
    nlohmann::json& assets_json = manifest_cache_["assets"];
    views.reserve(assets_json.size());
    for (auto it = assets_json.begin(); it != assets_json.end(); ++it) {
        views.push_back(AssetView{it.key(), &(*it)});
    }
    return views;
}

bool ManifestStore::update_map_entry(const std::string& map_id, const nlohmann::json& payload) {
    if (map_id.empty()) {
        return false;
    }
    ensure_loaded();
    ensure_asset_container();
    return apply_map_edit(map_id, payload);
}

const nlohmann::json* ManifestStore::find_map_entry(const std::string& map_id) const {
    if (map_id.empty()) {
        return nullptr;
    }
    auto* self = const_cast<ManifestStore*>(this);
    self->ensure_loaded();
    const auto maps_it = self->manifest_cache_.find("maps");
    if (maps_it == self->manifest_cache_.end() || !maps_it->is_object()) {
        return nullptr;
    }
    const auto it = maps_it->find(map_id);
    if (it == maps_it->end()) {
        return nullptr;
    }
    return &(*it);
}

void ManifestStore::ensure_loaded() {
    const std::uint64_t current_version = tag_utils::tag_version();
    if (loaded_ && current_version == last_known_tag_version_) {
        return;
    }
    manifest::ManifestData data = loader_ ? loader_() : manifest::load_manifest();
    manifest_cache_ = data.raw;
    if (!manifest_cache_.is_object()) {
        manifest_cache_ = nlohmann::json::object();
    }
    ensure_asset_container();
    loaded_ = true;
    dirty_ = false;
    last_known_tag_version_ = current_version;
}

bool ManifestStore::apply_edit(const std::string& name, const nlohmann::json& payload) {
    ensure_loaded();
    ensure_asset_container();

    manifest_cache_["assets"][name] = payload;
    dirty_ = true;
    if (submit_) {
        submit_(manifest_path_, manifest_cache_, indent_);
    }
    return true;
}

bool ManifestStore::apply_map_edit(const std::string& name, const nlohmann::json& payload) {
    manifest_cache_["maps"][name] = payload;
    dirty_ = true;
    if (submit_) {
        submit_(manifest_path_, manifest_cache_, indent_);
    }
    return true;
}

void ManifestStore::ensure_asset_container() {
    if (!manifest_cache_.contains("assets") || !manifest_cache_["assets"].is_object()) {
        manifest_cache_["assets"] = nlohmann::json::object();
    }
    if (!manifest_cache_.contains("maps")) {
        manifest_cache_["maps"] = nlohmann::json::object();
    }
}

}

