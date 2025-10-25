#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <limits>

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
        AssetEditSession(ManifestStore* owner, std::string name, nlohmann::json draft, bool is_new_asset);

        ManifestStore* owner_ = nullptr;
        std::string name_;
        nlohmann::json draft_;
        bool is_new_ = false;
};

    class AssetTransaction {
    public:
        AssetTransaction() = default;
        AssetTransaction(AssetTransaction&&) noexcept = default;
        AssetTransaction& operator=(AssetTransaction&&) noexcept = default;

        AssetTransaction(const AssetTransaction&) = delete;
        AssetTransaction& operator=(const AssetTransaction&) = delete;

        explicit operator bool() const { return owner_ != nullptr; }

        nlohmann::json& data() { return draft_; }
        const nlohmann::json& data() const { return draft_; }

        bool save();
        bool finalize();
        void cancel();

    private:
        friend class ManifestStore;
        AssetTransaction(ManifestStore* owner, std::string name, nlohmann::json draft, bool is_new_asset);

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
    AssetTransaction begin_asset_transaction(const std::string& name, bool create_if_missing = false);

    bool remove_asset(const std::string& name);

    void reload();
    void flush();

    bool dirty() const { return dirty_; }
    const nlohmann::json& manifest_json();
    std::vector<AssetView> assets();

    bool update_map_entry(const std::string& map_id, const nlohmann::json& payload);
    const nlohmann::json* find_map_entry(const std::string& map_id) const;

private:
    void ensure_loaded();
    bool apply_edit(const std::string& name, const nlohmann::json& payload);
    bool apply_map_edit(const std::string& name, const nlohmann::json& payload);
    void ensure_asset_container();

    std::filesystem::path manifest_path_;
    std::function<manifest::ManifestData()> loader_;
    std::function<void(const std::filesystem::path&, const nlohmann::json&, int)> submit_;
    std::function<void()> flush_;
    int indent_ = 2;

    bool loaded_ = false;
    bool dirty_ = false;
    nlohmann::json manifest_cache_ = nlohmann::json::object();
    std::uint64_t last_known_tag_version_ = std::numeric_limits<std::uint64_t>::max();
};

}

