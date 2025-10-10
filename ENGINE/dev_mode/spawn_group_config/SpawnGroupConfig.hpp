#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include <deque>

#include <nlohmann/json.hpp>

#include "DockableCollapsible.hpp"

class Input;

class SpawnGroupConfig : public DockableCollapsible {
    struct RowEntry;
public:
    struct ChangeSummary {
        bool method_changed = false;
        bool quantity_changed = false;
        bool candidates_changed = false;
        std::string method;
};

    struct Callbacks {
        std::function<void(const std::string&)> on_regenerate;
        std::function<void(const std::string&)> on_duplicate;
        std::function<void(const std::string&)> on_delete;
        std::function<void(const std::string&)> on_move_up;
        std::function<void(const std::string&)> on_move_down;
        std::function<void()> on_add;
};

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
        explicit RowController(RowEntry* row) : row_(row) {}
        RowEntry* row_ = nullptr;
        friend class SpawnGroupConfig;
    };

    using ConfigureEntryCallback = std::function<void(RowController&, const nlohmann::json&)>;

    struct EntryCallbacks {
        std::function<void(const std::string&)> on_method_changed;
        std::function<void(int min_number, int max_number)> on_quantity_changed;
        std::function<void(const nlohmann::json&)> on_candidates_changed;
};

    explicit SpawnGroupConfig(bool floatable = true);
    ~SpawnGroupConfig() override;

    void set_screen_dimensions(int width, int height);

    void load(nlohmann::json& groups,
              std::function<void()> on_change,
              std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change = {},
              ConfigureEntryCallback configure_entry = {});

    void bind_entry(nlohmann::json& entry,
                    EntryCallbacks callbacks = {},
                    ConfigureEntryCallback configure_entry = {});
    void bind_entry(nlohmann::json& entry,
                    std::function<void()> on_change,
                    std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change,
                    EntryCallbacks callbacks = {},
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

    void open(nlohmann::json& groups, std::function<void(const nlohmann::json&)> on_save);
    void request_open_spawn_group(const std::string& id, int x, int y);
    void set_anchor(int x, int y);
    void close_asset_search();

private:
    struct RowEntry;

    void load_impl(nlohmann::json* array, nlohmann::json* entry, std::function<void()> on_change, std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change, ConfigureEntryCallback configure_entry);
    void rebuild_rows();
    void apply_configuration(RowEntry& row);
    void rebuild_layout();
    void mark_layout_dirty();
    DockableCollapsible::Rows build_layout_rows();
    const nlohmann::json* current_source() const;
    void enqueue_notification(std::function<void()> cb);
    void process_pending_notifications();
    void fire_entry_callbacks(const nlohmann::json& entry, const ChangeSummary& summary);

private:
    bool default_floatable_mode_ = true;
    bool embedded_mode_ = false;
    bool layout_dirty_ = true;
    int screen_w_ = 1920;
    int screen_h_ = 1080;

    std::vector<std::unique_ptr<RowEntry>> rows_;
    nlohmann::json* bound_array_ = nullptr;
    nlohmann::json* bound_entry_ = nullptr;
    nlohmann::json single_entry_shadow_{};
    nlohmann::json readonly_snapshot_{};

    std::function<void()> on_change_{};
    std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change_{};
    ConfigureEntryCallback configure_entry_{};
    EntryCallbacks entry_callbacks_{};
    Callbacks callbacks_{};
    std::function<void()> on_layout_change_{};

    std::unordered_set<std::string> expanded_{};
    SDL_Point anchor_{0, 0};
    std::optional<std::string> pending_focus_id_{};
    std::function<void(const nlohmann::json&)> pending_save_callback_{};

    bool suppress_layout_change_callback_ = false;
    std::unique_ptr<DMButton> add_button_{};
    std::unique_ptr<ButtonWidget> add_button_widget_{};
    std::unique_ptr<class LabelWidget> empty_state_label_{};

    std::deque<std::function<void()>> pending_notifications_{};
    bool processing_notifications_ = false;

    friend class SpawnGroupConfigTestAccessor;
};

