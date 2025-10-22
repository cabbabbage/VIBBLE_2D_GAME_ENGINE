#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "SlidingWindowContainer.hpp"

class DMButton;
class DMRangeSlider;
class Input;
class MapLayersController;
struct SDL_Event;
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

private:
    struct CandidateRow {
        int candidate_index = -1;
        std::string room_name;
        int min_instances = 0;
        int max_instances = 0;
        SDL_Rect label_rect{0, 0, 0, 0};
        SDL_Rect remove_rect{0, 0, 0, 0};
        std::unique_ptr<DMButton> remove_button;
        std::unique_ptr<DMRangeSlider> range_slider;
    };

    void configure_container(SlidingWindowContainer& container);
    void clear_container_callbacks(SlidingWindowContainer& container);

    int layout_content(const SlidingWindowContainer::LayoutContext& ctx);
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    void update(const Input& input, int screen_w, int screen_h);

    void ensure_data() const;
    void rebuild_rows() const;
    void rebuild_available_rooms() const;
    void mark_dirty() const;
    void update_header_text() const;
    void open_room_selector();
    void close_room_selector();
    void on_room_selected(const std::string& room_key);
    bool handle_slider_change(CandidateRow& row);

    SlidingWindowContainer* container_ = nullptr;
    std::shared_ptr<MapLayersController> controller_{};
    mutable std::size_t controller_listener_id_ = 0;

    mutable bool data_dirty_ = true;
    int selected_layer_index_ = -1;
    mutable bool has_layer_data_ = false;

    std::unique_ptr<DMButton> add_room_button_;
    mutable std::vector<CandidateRow> candidates_;
    mutable std::vector<std::string> summary_lines_;
    mutable std::vector<SDL_Rect> summary_rects_;
    mutable std::string layer_name_;
    mutable int layer_min_rooms_ = 0;
    mutable int layer_max_rooms_ = 0;
    mutable std::string empty_state_message_;
    mutable SDL_Rect empty_state_rect_{0, 0, 0, 0};

    mutable std::vector<std::string> available_rooms_;
    mutable std::vector<std::string> filtered_rooms_;

    std::unique_ptr<RoomSelectorPopup> room_selector_;
};

