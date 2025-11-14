#pragma once

#include "../DockableCollapsible.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class AssetInfoUI;
class Input;
class DMButton;
class ButtonWidget;
class ReadOnlyTextBoxWidget;
namespace devmode::core {
class ManifestStore;
}

struct SectionSpawnGroupsTestAccess;

class Section_SpawnGroups : public DockableCollapsible {
public:
    Section_SpawnGroups();
    ~Section_SpawnGroups() override;

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    void set_manifest_store(devmode::core::ManifestStore* store);
    void set_spawn_config_listener(std::function<void(const nlohmann::json&)> listener);
    void set_spawn_group_removed_listener(std::function<void(const std::string&)> listener);

    void build() override;
    void layout() override;
    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;

    const nlohmann::json& groups() const { return groups_; }

private:
    void reload_from_file();
    bool save_to_file();
    void renumber_priorities();

    void add_spawn_group();
    void delete_spawn_group(const std::string& id);
    void reorder_spawn_group(const std::string& id, size_t new_index);

    int index_of(const std::string& id) const;

    SDL_Point editor_anchor_point() const;

    void schedule_rebuild();
    void notify_spawn_config_listeners(const nlohmann::json& entry);
    void notify_spawn_group_removed(const std::string& id);

private:
    AssetInfoUI* ui_ = nullptr;
    nlohmann::json groups_ = nlohmann::json::array();
    devmode::core::ManifestStore* manifest_store_ = nullptr;

    std::unique_ptr<class SpawnGroupConfig> list_;

    std::unique_ptr<DMButton> add_btn_;
    std::unique_ptr<ButtonWidget> add_btn_w_;
    std::unique_ptr<ReadOnlyTextBoxWidget> empty_state_widget_;

    int screen_w_ = 1920;
    int screen_h_ = 1080;

    bool rebuilding_ = false;
    bool rebuild_requested_ = false;

    std::function<void(const nlohmann::json&)> spawn_config_listener_{};
    std::function<void(const std::string&)> spawn_group_removed_listener_{};

protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "spawn_groups"; }

    friend struct SectionSpawnGroupsTestAccess;
};
