#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <cstdint>

class Input;
class AssetInfo;
class AssetLibrary;
class Asset;
class Assets;
class DockableCollapsible;
class DMButton;
class DMTextBox;
class TextBoxWidget;
class Widget;

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

    void update(const Input& input, int screen_w, int screen_h, AssetLibrary& lib, Assets& assets, devmode::core::ManifestStore& store);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;
    bool handle_event(const SDL_Event& e);

    std::shared_ptr<AssetInfo> consume_selection();

private:
    void ensure_items(AssetLibrary& lib);
    void rebuild_rows();
    void refresh_tiles(Assets& assets);
    bool matches_query(const AssetInfo& info, const std::string& query) const;
    bool matches_tag_query(const std::string& tag, const std::string& query) const;
    SDL_Texture* get_default_frame_texture(const AssetInfo& info) const;
    void request_delete(const std::shared_ptr<AssetInfo>& info);
    void cancel_delete_request();
    void confirm_delete_request();
    void clear_delete_state();
    bool handle_delete_modal_event(const SDL_Event& e);
    void update_delete_modal_geometry(int screen_w, int screen_h);
    bool create_new_asset(const std::string& name);
    bool refresh_tag_items();
    void rebuild_tag_asset_lookup();
    std::shared_ptr<AssetInfo> resolve_tag_to_asset(const std::string& tag) const;
    int count_assets_for_tag(const std::string& tag) const;
    void delete_hashtag(const std::string& tag);
    bool remove_tag_from_manifest_assets(const std::string& tag);
    bool remove_tag_from_manifest_maps(const std::string& tag);

private:
    std::unique_ptr<DockableCollapsible> floating_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<class ButtonWidget> add_button_widget_;
    std::unique_ptr<DMTextBox> search_box_;
    std::unique_ptr<TextBoxWidget> search_widget_;
    std::vector<std::shared_ptr<AssetInfo>> items_;
    bool items_cached_ = false;
    bool tag_items_initialized_ = false;
    std::string search_query_;
    bool filter_dirty_ = true;

    struct AssetTileWidget;
    struct HashtagTileWidget;
    std::vector<std::unique_ptr<Widget>> tiles_;
    std::vector<std::string> tag_items_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<AssetInfo>>> tag_asset_lookup_;
    std::uint64_t tag_version_token_ = 0;
    bool tag_assets_dirty_ = true;

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
