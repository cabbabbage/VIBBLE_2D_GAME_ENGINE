#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_set>

class Input;
class AssetInfo;
class AssetLibrary;
class Asset;
class Assets;
class DockableCollapsible;
class DMButton;

class AssetLibraryUI {
public:
    AssetLibraryUI();
    ~AssetLibraryUI();

    void toggle();
    bool is_visible() const;
    void open();
    void close();
    void set_position(int x, int y);
    void set_expanded(bool e);
    bool is_expanded() const;
    bool is_input_blocking() const;
    bool is_input_blocking_at(int mx, int my) const;
    bool is_dragging_asset() const;

    void update(const Input& input, int screen_w, int screen_h, AssetLibrary& lib, Assets& assets);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;
    void handle_event(const SDL_Event& e);

    std::shared_ptr<AssetInfo> consume_selection();

private:
    void ensure_items(AssetLibrary& lib);
    void rebuild_rows();
    SDL_Texture* get_default_frame_texture(const AssetInfo& info) const;

private:
    std::unique_ptr<DockableCollapsible> floating_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<class ButtonWidget> add_button_widget_;
    std::vector<std::shared_ptr<AssetInfo>> items_;
    bool items_cached_ = false;

    struct AssetTileWidget;
    std::vector<std::unique_ptr<AssetTileWidget>> tiles_;

    Assets* assets_owner_ = nullptr;
    mutable std::unordered_set<std::string> preview_attempted_;

    class Asset* drag_spawned_ = nullptr;
    std::shared_ptr<AssetInfo> drag_info_{};
    bool dragging_from_library_ = false;

    bool showing_create_popup_ = false;
    std::string new_asset_name_;
};
