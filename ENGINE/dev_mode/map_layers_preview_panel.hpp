#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"

class Input;
class MapLayersController;
class MapLayersPreviewWidget;

class MapLayersPreviewPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    explicit MapLayersPreviewPanel(int x = 72, int y = 40);
    ~MapLayersPreviewPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_controller(std::shared_ptr<MapLayersController> controller);

    void set_on_select_layer(std::function<void(int)> cb);
    void set_on_select_room(std::function<void(const std::string&)> cb);
    void set_on_show_room_list(std::function<void()> cb);

    void update(const Input& input, int screen_w = 0, int screen_h = 0) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    bool is_point_inside(int x, int y) const override;

private:
    void build_rows();
    void trigger_save();

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_{};
    std::shared_ptr<MapLayersController> controller_;

    std::vector<std::unique_ptr<class Widget>> owned_widgets_;
    MapLayersPreviewWidget* preview_widget_ = nullptr;

    std::unique_ptr<class DMButton> add_layer_btn_;
    std::unique_ptr<class DMButton> create_room_btn_;
    std::unique_ptr<class DMButton> reload_btn_;

    std::function<void(int)> on_select_layer_{};
    std::function<void(const std::string&)> on_select_room_{};
    std::function<void()> on_show_room_list_{};
};

