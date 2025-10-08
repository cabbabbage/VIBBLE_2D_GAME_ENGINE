#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DockableCollapsible.hpp"

class ButtonWidget;
class DMButton;
class Input;
class Widget;
class SearchAssets;
class AreaLinkPanel;

// SpawnGroupList now owns the full spawn group editing UI. Each row is
// collapsible and contains the controls that were previously managed by the
// standalone SpawnGroupsConfigPanel.
class SpawnGroupList : public DockableCollapsible {
public:
    struct ChangeSummary {
        bool method_changed = false;
        bool quantity_changed = false;
        std::string method;
    };

    struct Callbacks {
        std::function<void(const std::string&)> on_duplicate;
        std::function<void(const std::string&)> on_delete;
        std::function<void(const std::string&)> on_move_up;
        std::function<void(const std::string&)> on_move_down;
        std::function<void()> on_add;
    };

    // Forward-declare internal row type for controller
    struct EntryRow;

    class RowController {
    public:
        void set_ownership_label(const std::string& label, SDL_Color color);
        void clear_ownership_label();
        void set_area_names_provider(std::function<std::vector<std::string>()> provider);
        void set_stack_key(std::string key);
        void lock_method_to(const std::string& method);
        void clear_method_lock();
        void set_quantity_hidden(bool hidden);

    private:
        explicit RowController(struct EntryRow* row) : row_(row) {}
        struct EntryRow* row_ = nullptr;
        friend class SpawnGroupList;
    };

    using ConfigureEntryCallback = std::function<void(RowController&, const nlohmann::json&)>;

    explicit SpawnGroupList(bool floatable = true);
    ~SpawnGroupList() override;

    void set_screen_dimensions(int width, int height);

    void load(nlohmann::json& groups,
              std::function<void()> on_change,
              std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change = {},
              ConfigureEntryCallback configure_entry = {});

    void load(const nlohmann::json& groups);

    void append_rows(Rows& rows);
    void set_callbacks(Callbacks cb);
    void set_on_layout_changed(std::function<void()> cb);
    void refresh_row_configuration();

    void set_embedded_mode(bool embedded);

    void expand_group(const std::string& id);
    void collapse_group(const std::string& id);
    bool is_expanded(const std::string& id) const;

    std::vector<std::string> expanded_groups() const;
    void restore_expanded_groups(const std::vector<std::string>& ids);

    nlohmann::json to_json() const;

    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;
    void render_content(SDL_Renderer* r) const override;

    // Floating usage helpers
    void open(nlohmann::json& groups, std::function<void(const nlohmann::json&)> on_save);
    void request_open_spawn_group(const std::string& id, int x, int y);
    void set_anchor(int x, int y);

private:
    class CandidateList;
    struct EntryRow;
    friend class CandidateList;

    struct RowRef {
        std::string id;
        int index = -1;
        bool valid() const { return !id.empty() || index >= 0; }
    };

    EntryRow* find_row(const std::string& id);
    const EntryRow* find_row(const std::string& id) const;

    void bind_row_ref(RowRef& ref, EntryRow& row);
    EntryRow* lookup_row(RowRef& ref);
    const EntryRow* lookup_row(const RowRef& ref) const;
    void clear_row_ref(RowRef& ref);

    void rebuild_layout();
    void request_layout();
    void notify_data_changed(EntryRow& row, bool structure_changed, bool summary_changed);
    void ensure_asset_search();
    void request_asset_search_open(EntryRow& row, std::function<void(const std::string&)> callback = {});
    void open_asset_search(EntryRow& row, std::function<void(const std::string&)> callback = {});
    void close_asset_search();
    void ensure_area_panel();
    void open_area_panel(EntryRow& row);
    void close_area_panel();

    bool default_floatable_mode_ = true;
    bool embedded_mode_ = false;
    bool layout_dirty_ = true;
    int screen_w_ = 1920;
    int screen_h_ = 1080;

    std::vector<std::unique_ptr<EntryRow>> rows_;
    nlohmann::json* bound_array_ = nullptr;
    nlohmann::json readonly_snapshot_;

    std::function<void()> on_change_;
    std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change_;
    ConfigureEntryCallback configure_entry_;
    Callbacks callbacks_{};
    std::function<void()> on_layout_change_{};
    bool suppress_layout_callback_ = false;

    std::unique_ptr<SearchAssets> asset_search_;
    std::unique_ptr<AreaLinkPanel> area_panel_;
    std::unique_ptr<DMButton> add_group_btn_;
    std::unique_ptr<ButtonWidget> add_group_btn_w_;
    SDL_Point anchor_{0,0};
    RowRef asset_search_row_ref_{};
    RowRef area_panel_row_ref_{};
    bool pending_asset_search_open_ = false;
    RowRef pending_asset_search_row_ref_{};
    std::function<void(const std::string&)> pending_asset_search_callback_{};
};
