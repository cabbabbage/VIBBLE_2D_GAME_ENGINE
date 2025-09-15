#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <memory>

class Input;
class AssetInfo;
class AssetLibrary;
class Asset;
class Assets;
class DockableCollapsible;
class DMButton;

// Floating asset library panel using DockableCollapsible
class AssetLibraryUI {

	public:
    AssetLibraryUI();
    ~AssetLibraryUI();

    void toggle();
    bool is_visible() const;         // mirrors floating panel visibility
    void open();                     // visible + keep current expand state
    void close();
    void set_position(int x, int y);
    void set_expanded(bool e);
    bool is_expanded() const;
    bool is_input_blocking() const;  // legacy: expanded or dragging
    bool is_input_blocking_at(int mx, int my) const; // block only when hovered/dragging

    void update(const Input& input, int screen_w, int screen_h, AssetLibrary& lib, Assets& assets);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;
    void handle_event(const SDL_Event& e);

    // Legacy API no longer used, kept for compatibility
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

    // Tile widgets live here and are referenced by rows in floating_
    struct AssetTileWidget;
    std::vector<std::unique_ptr<AssetTileWidget>> tiles_;

    // Drag state from library into world
    class Asset* drag_spawned_ = nullptr; // non-owning
    std::shared_ptr<AssetInfo> drag_info_{};
    bool dragging_from_library_ = false;
};
