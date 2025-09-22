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
#include "asset_config_ui.hpp"
class ButtonWidget;
class DMButton;
class Input;

// Manages a collection of AssetConfig panels for an assets array
class AssetsConfig : public DockableCollapsible {
public:
    AssetsConfig();
    // Standalone panel controls
    void open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close);
    void close();
    bool visible() const;
    void set_position(int x, int y);
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    // Embedding helpers for RoomConfigurator or runtime editing
    void load(nlohmann::json& assets,
              std::function<void()> on_change,
              std::function<void(const nlohmann::json&, const AssetConfigUI::ChangeSummary&)> on_entry_change = {});
    void append_rows(DockableCollapsible::Rows& rows);
    void set_anchor(int x, int y);
    void open_asset_config(const std::string& id, int x, int y);
    void close_all_asset_configs();
    struct OpenConfigState {
        std::string id;
        SDL_Point position{0, 0};
        size_t index = std::numeric_limits<size_t>::max();
    };
    std::optional<OpenConfigState> capture_open_config() const;
    void restore_open_config(const OpenConfigState& state);
    nlohmann::json to_json() const;
    bool any_visible() const;
    bool is_point_inside(int x, int y) const;
private:
    struct Entry {
        std::string id;
        std::unique_ptr<AssetConfigUI> cfg;
        nlohmann::json* json = nullptr;
        std::unique_ptr<DMButton> btn;
        std::unique_ptr<ButtonWidget> btn_w;
    };
    std::vector<Entry> entries_;
    nlohmann::json* assets_json_ = nullptr;
    std::function<void()> on_change_;
    std::function<void(const nlohmann::json&, const AssetConfigUI::ChangeSummary&)> on_entry_change_;
    nlohmann::json temp_assets_;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;
    std::function<void(const nlohmann::json&)> on_close_;
};
