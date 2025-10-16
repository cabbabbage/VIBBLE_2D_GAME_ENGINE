#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/manifest/manifest_loader.hpp"

namespace devmode::core {

class ManifestStore {
public:
    class AssetEditSession {
    public:
        AssetEditSession() = default;
        AssetEditSession(AssetEditSession&&) noexcept = default;
        AssetEditSession& operator=(AssetEditSession&&) noexcept = default;

        AssetEditSession(const AssetEditSession&) = delete;
        AssetEditSession& operator=(const AssetEditSession&) = delete;

        explicit operator bool() const { return owner_ != nullptr; }

        const std::string& name() const { return name_; }
        bool is_new_asset() const { return is_new_; }

        nlohmann::json& data() { return draft_; }
        const nlohmann::json& data() const { return draft_; }

        bool commit();
        void cancel();

    private:
        friend class ManifestStore;
        AssetEditSession(ManifestStore* owner,
                         std::string name,
                         nlohmann::json draft,
                         bool is_new_asset);

        ManifestStore* owner_ = nullptr;
        std::string name_;
        nlohmann::json draft_;
        bool is_new_ = false;
    };

    struct AssetView {
        std::string name;
        const nlohmann::json* data = nullptr;

        explicit operator bool() const { return data != nullptr; }
        const nlohmann::json* operator->() const { return data; }
        const nlohmann::json& operator*() const { return *data; }
    };

    ManifestStore();

    ManifestStore(const std::filesystem::path& manifest_path,
                  std::function<manifest::ManifestData()> loader,
                  std::function<void(const std::filesystem::path&, const nlohmann::json&, int)> submit = {},
                  std::function<void()> flush = {},
                  int indent = 2);

    std::optional<std::string> resolve_asset_name(const std::string& name);
    AssetView get_asset(const std::string& name);

    AssetEditSession begin_asset_edit(const std::string& name, bool create_if_missing = false);

    void reload();
    void flush();

    bool dirty() const { return dirty_; }
    const nlohmann::json& manifest_json();

private:
    void ensure_loaded();
    bool apply_edit(const std::string& name, const nlohmann::json& payload);
    void ensure_asset_container();

    std::filesystem::path manifest_path_;
    std::function<manifest::ManifestData()> loader_;
    std::function<void(const std::filesystem::path&, const nlohmann::json&, int)> submit_;
    std::function<void()> flush_;
    int indent_ = 2;

    bool loaded_ = false;
    bool dirty_ = false;
    nlohmann::json manifest_cache_ = nlohmann::json::object();
};

} // namespace devmode::core

