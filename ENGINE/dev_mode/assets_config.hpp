#pragma once

#include <SDL.h>
#include <memory>
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
class AssetsConfig {
public:
    AssetsConfig();
    // Standalone panel controls
    void open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close);
    void close();
    bool visible() const;
    void set_position(int x, int y);
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    // Embedding helpers for RoomConfigurator or runtime editing
    void load(nlohmann::json& assets, std::function<void()> on_change);
    void append_rows(DockableCollapsible::Rows& rows);
    void set_anchor(int x, int y);
    void open_asset_config(const std::string& id, int x, int y);
    void close_all_asset_configs();
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
    nlohmann::json temp_assets_;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
    std::unique_ptr<DockableCollapsible> panel_;
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;
    std::function<void(const nlohmann::json&)> on_close_;
};
