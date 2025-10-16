#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_set>
#include <optional>

class Input;
class AssetInfo;
class AssetLibrary;
class Asset;
class Assets;
class DockableCollapsible;
class DMButton;
class DMTextBox;
class TextBoxWidget;

namespace devmode::core {
class ManifestStore;
}

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
    bool is_locked() const;

    void update(const Input& input,
                int screen_w,
                int screen_h,
                AssetLibrary& lib,
                Assets& assets,
                devmode::core::ManifestStore& store);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;
    bool handle_event(const SDL_Event& e);

    std::shared_ptr<AssetInfo> consume_selection();

private:
    void ensure_items(AssetLibrary& lib);
    void rebuild_rows();
    void refresh_tiles(Assets& assets);
    bool matches_query(const AssetInfo& info, const std::string& query) const;
    SDL_Texture* get_default_frame_texture(const AssetInfo& info) const;
    void request_delete(const std::shared_ptr<AssetInfo>& info);
    void cancel_delete_request();
    void confirm_delete_request();
    void clear_delete_state();
    bool handle_delete_modal_event(const SDL_Event& e);
    void update_delete_modal_geometry(int screen_w, int screen_h);
    bool create_new_asset(const std::string& name);

private:
    std::unique_ptr<DockableCollapsible> floating_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<class ButtonWidget> add_button_widget_;
    std::unique_ptr<DMTextBox> search_box_;
    std::unique_ptr<TextBoxWidget> search_widget_;
    std::vector<std::shared_ptr<AssetInfo>> items_;
    bool items_cached_ = false;
    std::string search_query_;
    bool filter_dirty_ = true;

    struct AssetTileWidget;
    std::vector<std::unique_ptr<AssetTileWidget>> tiles_;

    struct PendingDeleteInfo {
        std::string name;
        std::string asset_dir;
    };

    Assets* assets_owner_ = nullptr;
    AssetLibrary* library_owner_ = nullptr;
    devmode::core::ManifestStore* manifest_store_owner_ = nullptr;
    mutable std::unordered_set<std::string> preview_attempted_;

    std::shared_ptr<AssetInfo> pending_selection_{};

    bool showing_create_popup_ = false;
    std::string new_asset_name_;
    bool showing_delete_popup_ = false;
    std::optional<PendingDeleteInfo> pending_delete_;
    SDL_Rect delete_modal_rect_{0, 0, 0, 0};
    SDL_Rect delete_yes_rect_{0, 0, 0, 0};
    SDL_Rect delete_no_rect_{0, 0, 0, 0};
    bool delete_yes_hovered_ = false;
    bool delete_no_hovered_ = false;
    bool delete_yes_pressed_ = false;
    bool delete_no_pressed_ = false;
};
