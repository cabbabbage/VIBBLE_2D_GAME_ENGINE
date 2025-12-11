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
    struct AreaRef {
        std::string room_name;
        std::string area_name;
};
    std::optional<AreaRef> consume_area_selection();

private:
    struct PendingDeleteInfo {
        std::string name;
        std::string asset_dir;
};
    enum class CreateAssetResult {
        Success,
        AlreadyExists,
        Failed
};

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
    CreateAssetResult create_new_asset(const std::string& name);
    void handle_create_button_pressed();
    void show_search_error(const std::string& message);
    void clear_search_error();
    bool refresh_tag_items();
    void rebuild_tag_asset_lookup();
    std::shared_ptr<AssetInfo> resolve_tag_to_asset(const std::string& tag) const;
    int count_assets_for_tag(const std::string& tag) const;
    void delete_hashtag(const std::string& tag);
    bool remove_tag_from_manifest_assets(const std::string& tag);
    bool remove_tag_from_manifest_maps(const std::string& tag);
    void toggle_multi_select_mode();
    void update_multi_select_controls();
    void handle_multi_select_selection(const std::shared_ptr<AssetInfo>& info, bool selected);
    void handle_delete_all_request();
    void begin_bulk_delete(std::vector<PendingDeleteInfo> requests);
    void execute_bulk_delete_queue();
    void perform_delete(const PendingDeleteInfo& pending, bool defer_multi_select_refresh = false);

private:
    std::unique_ptr<DockableCollapsible> floating_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<class ButtonWidget> add_button_widget_;
    std::unique_ptr<DMButton> multi_select_button_;
    std::unique_ptr<class ButtonWidget> multi_select_button_widget_;
    std::unique_ptr<DMButton> delete_all_button_;
    std::unique_ptr<class ButtonWidget> delete_all_button_widget_;
    std::unique_ptr<DMTextBox> search_box_;
    std::unique_ptr<TextBoxWidget> search_widget_;
    std::vector<std::shared_ptr<AssetInfo>> items_;
    bool items_cached_ = false;
    bool tag_items_initialized_ = false;
    std::string search_query_;
    bool search_error_active_ = false;
    bool filter_dirty_ = true;

    struct AssetTileWidget;
    struct HashtagTileWidget;
    struct RoomAreaTileWidget;
    std::vector<std::unique_ptr<Widget>> tiles_;
    std::vector<std::string> tag_items_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<AssetInfo>>> tag_asset_lookup_;
    std::uint64_t tag_version_token_ = 0;
    bool tag_assets_dirty_ = true;

    Assets* assets_owner_ = nullptr;
    AssetLibrary* library_owner_ = nullptr;
    devmode::core::ManifestStore* manifest_store_owner_ = nullptr;
    mutable std::unordered_set<std::string> preview_attempted_;

    std::shared_ptr<AssetInfo> pending_selection_{};
    std::optional<AreaRef> pending_area_selection_{};
    bool multi_select_mode_ = false;
    std::unordered_set<std::string> multi_select_selection_;

    bool showing_delete_popup_ = false;
    std::optional<PendingDeleteInfo> pending_delete_;
    SDL_Rect delete_modal_rect_{0, 0, 0, 0};
    SDL_Rect delete_yes_rect_{0, 0, 0, 0};
    SDL_Rect delete_no_rect_{0, 0, 0, 0};
    bool delete_yes_hovered_ = false;
    bool delete_no_hovered_ = false;
    bool delete_yes_pressed_ = false;
    bool delete_no_pressed_ = false;
    bool delete_skip_hovered_ = false;
    bool delete_skip_pressed_ = false;
    SDL_Rect delete_skip_rect_{0, 0, 0, 0};
    bool skip_delete_confirmation_in_session_ = false;
    std::vector<PendingDeleteInfo> bulk_delete_queue_;
    bool bulk_delete_mode_ = false;
};
