#pragma once

#include <SDL.h>
#include <memory>
#include <optional>
#include <limits>
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include "DockableCollapsible.hpp"
#include "spawn_group_config_ui.hpp"
class ButtonWidget;
class DMButton;
class Input;

// Manages a collection of spawn group panels for an assets array
class SpawnGroupsConfig : public DockableCollapsible {
public:
    explicit SpawnGroupsConfig(bool floatable = true);
    // Standalone panel controls
    void open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close);
    void close();
    bool visible() const;
    void set_position(int x, int y);
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    // Embedding helpers for RoomConfigurator or runtime editing
    using ConfigureEntryCallback = std::function<void(SpawnGroupConfigUI&, const nlohmann::json&)>;

    void load(nlohmann::json& assets,
              std::function<void()> on_change,
              std::function<void(const nlohmann::json&, const SpawnGroupConfigUI::ChangeSummary&)> on_entry_change = {},
              ConfigureEntryCallback configure_entry = {});
    void append_rows(DockableCollapsible::Rows& rows);
    void set_anchor(int x, int y);
    void open_spawn_group(const std::string& id, int x, int y);
    void request_open_spawn_group(const std::string& id, int x, int y);
    void close_all();
    bool is_open(const std::string& id) const;
    struct OpenSpawnGroupState {
        std::string id;
        SDL_Point position{0, 0};
        size_t index = std::numeric_limits<size_t>::max();
    };
    std::optional<OpenSpawnGroupState> capture_open_spawn_group() const;
    void restore_open_spawn_group(const OpenSpawnGroupState& state);
    nlohmann::json to_json() const;
    bool any_visible() const;
    bool is_point_inside(int x, int y) const;
private:
    bool floatable_mode_ = true;
    bool should_rebuild_with(const nlohmann::json& normalized_assets) const;
    struct PendingOpenRequest {
        std::string id;
        int x = 0;
        int y = 0;
    };
    std::optional<PendingOpenRequest> pending_open_;
    struct Entry {
        std::string id;
        std::unique_ptr<SpawnGroupConfigUI> cfg;
        nlohmann::json* json = nullptr;
        std::unique_ptr<DMButton> btn;
        std::unique_ptr<ButtonWidget> btn_w;
        size_t on_close_callback_id = 0;
    };
    std::vector<Entry> entries_;
    nlohmann::json* assets_json_ = nullptr;
    std::function<void()> on_change_;
    std::function<void(const nlohmann::json&, const SpawnGroupConfigUI::ChangeSummary&)> on_entry_change_;
    ConfigureEntryCallback configure_entry_;
    nlohmann::json temp_assets_;
    nlohmann::json loaded_snapshot_;
    nlohmann::json* last_loaded_source_ = nullptr;
    bool entries_loaded_ = false;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;
    std::function<void(const nlohmann::json&)> on_close_;
    void hide_temporarily();
    bool suppress_close_actions_ = false;
    bool restore_parent_visibility_pending_ = false;
    bool restore_parent_expanded_state_ = false;
    bool suppress_restore_on_close_ = false;
    void handle_entry_closed(Entry& entry);
    void open_entry(Entry& entry, int x, int y);
    std::string floating_stack_key_;
};

