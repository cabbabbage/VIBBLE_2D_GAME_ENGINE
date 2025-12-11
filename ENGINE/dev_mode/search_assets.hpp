#pragma once

#include <SDL.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

#include "FloatingPanelLayoutManager.hpp"

namespace devmode::core {
class ManifestStore;
}

class DockableCollapsible;
class DMTextBox;
class DMButton;
class TextBoxWidget;
class ButtonWidget;
class Input;

class SearchAssets {
public:
    struct Result {
        std::string label;
        std::string value;
        bool is_tag = false;
};

    using ExtraResultsProvider = std::function<std::vector<Result>()>;
    using AssetFilter = std::function<bool(const nlohmann::json&)>;

    using Callback = std::function<void(const std::string&)>;
    explicit SearchAssets(devmode::core::ManifestStore* manifest_store = nullptr);
    ~SearchAssets();
    void set_position(int x, int y);
    void set_screen_dimensions(int width, int height);
    void set_floating_stack_key(std::string key);
    void set_anchor_position(int x, int y);
    void layout_with_parent(const FloatingPanelLayoutManager::SlidingParentInfo& parent);
    void open(Callback cb);
    void close();
    bool visible() const;
    void set_embedded_mode(bool embedded);
    void set_embedded_rect(const SDL_Rect& rect);
    SDL_Rect rect() const;
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_point_inside(int x, int y) const;
    void set_manifest_store(devmode::core::ManifestStore* manifest_store);
    void set_query_for_testing(const std::string& value);
    std::vector<std::pair<std::string, bool>> results_for_testing() const;
    void set_extra_results_provider(ExtraResultsProvider provider);
    void set_asset_filter(AssetFilter filter);
private:
    struct Asset { std::string name; std::vector<std::string> tags; const nlohmann::json* payload = nullptr; };
    void load_assets();
    void filter_assets();
    static std::string to_lower(std::string s);
    void apply_position(int x, int y);
    void ensure_visible_position(const FloatingPanelLayoutManager::SlidingParentInfo* parent = nullptr);
    FloatingPanelLayoutManager::PanelInfo build_panel_info(bool force_layout) const;
    std::unique_ptr<DockableCollapsible> panel_;
    std::unique_ptr<DMTextBox> query_;
    std::unique_ptr<TextBoxWidget> query_widget_;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<ButtonWidget>> button_widgets_;
    Callback cb_;
    std::vector<Asset> all_;
    std::vector<Result> results_;
    std::string last_query_;
    std::uint64_t tag_data_version_ = 0;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    std::unique_ptr<devmode::core::ManifestStore> owned_manifest_store_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;
    SDL_Point last_known_position_{64, 64};
    SDL_Point pending_position_{64, 64};
    bool has_pending_position_ = false;
    bool has_custom_position_ = false;
    std::string floating_stack_key_;
    bool embedded_ = false;
    SDL_Rect embedded_rect_{0, 0, 0, 0};
    ExtraResultsProvider extra_results_provider_{};
    AssetFilter asset_filter_{};
};
