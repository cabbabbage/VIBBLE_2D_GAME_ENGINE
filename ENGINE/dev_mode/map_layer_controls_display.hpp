#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "SlidingWindowContainer.hpp"

class DMButton;
class DMRangeSlider;
class Input;
class MapLayersController;
union SDL_Event;
struct SDL_Renderer;

class RoomSelectorPopup;

class MapLayerControlsDisplay {
public:
    MapLayerControlsDisplay();
    ~MapLayerControlsDisplay();

    void attach_container(SlidingWindowContainer* container);
    void detach_container();

    void set_controller(std::shared_ptr<MapLayersController> controller);
    void set_selected_layer(int index);
    void refresh();
    void set_on_change(std::function<void()> cb);
    void set_on_show_rooms_list(std::function<void()> cb);
    void set_on_create_room(std::function<void()> cb);

private:
    struct CandidateRow {
        struct ChildRow {
            std::string room_key;
            SDL_Rect label_rect{0, 0, 0, 0};
            std::unique_ptr<DMButton> remove_button;
};

        int candidate_index = -1;
        std::string room_key;
        std::string display_label;
        int min_instances = 0;
        int max_instances = 0;
        SDL_Rect label_rect{0, 0, 0, 0};
        SDL_Rect background_rect{0, 0, 0, 0};
        std::unique_ptr<DMButton> remove_button;
        std::unique_ptr<DMRangeSlider> range_slider;
        std::vector<ChildRow> children;
        std::unique_ptr<DMButton> add_child_button;
        SDL_Rect children_header_rect{0, 0, 0, 0};
        SDL_Rect children_placeholder_rect{0, 0, 0, 0};
        bool hovered = false;
        bool slider_active = false;
};

    void configure_container(SlidingWindowContainer& container);
    void clear_container_callbacks(SlidingWindowContainer& container);

    int layout_content(const SlidingWindowContainer::LayoutContext& ctx);
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    void update(const Input& input, int screen_w, int screen_h);

    void ensure_data() const;
    void rebuild_content() const;
    void rebuild_available_rooms() const;
    void mark_dirty() const;
    void update_header_text() const;
    void update_header_navigation_button();
    void open_room_selector();
    void close_room_selector();
    void on_room_selected(const std::string& room_key);
    void open_child_selector(int candidate_index);
    void close_child_selector();
    void on_child_room_selected(const std::string& room_key);
    bool handle_slider_change(CandidateRow& row);
    void notify_change();
    void handle_back_to_rooms();
    void handle_create_room();
    void begin_slider_dirty_suppression(const DMRangeSlider* slider) const;
    void end_slider_dirty_suppression(const DMRangeSlider* slider) const;

    SlidingWindowContainer* container_ = nullptr;
    std::shared_ptr<MapLayersController> controller_{};
    mutable std::size_t controller_listener_id_ = 0;

    mutable bool data_dirty_ = true;
    int selected_layer_index_ = -1;
    mutable bool has_layer_data_ = false;

    std::unique_ptr<DMButton> add_room_button_;
    std::unique_ptr<DMButton> new_room_button_;
    mutable std::vector<CandidateRow> candidates_;
    mutable std::vector<std::string> info_lines_;
    mutable std::vector<SDL_Rect> info_rects_;
    mutable std::string layer_name_;
    mutable std::string empty_state_message_;
    mutable SDL_Rect empty_state_rect_{0, 0, 0, 0};

    mutable std::vector<std::string> available_rooms_;
    mutable std::vector<std::string> filtered_rooms_;

    std::unique_ptr<RoomSelectorPopup> room_selector_;
    std::unique_ptr<RoomSelectorPopup> child_selector_;
    std::vector<std::string> child_selector_rooms_;
    int pending_child_candidate_index_ = -1;
    std::function<void()> on_change_{};
    std::function<void()> on_show_rooms_list_{};
    std::function<void()> on_create_room_{};

    mutable bool suppress_slider_dirty_notifications_ = false;
    mutable bool pending_slider_dirty_refresh_ = false;
    mutable const DMRangeSlider* active_slider_dirty_owner_ = nullptr;
};

