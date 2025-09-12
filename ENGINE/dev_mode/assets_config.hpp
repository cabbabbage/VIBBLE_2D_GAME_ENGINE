#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "search_assets.hpp"
#include "widgets.hpp"

class FloatingCollapsible;
class ButtonWidget;
class DropdownWidget;
class SliderWidget;
class Input;

class AssetsConfig {
public:
    AssetsConfig();
    void set_position(int x, int y);
    void open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close);
    void close();
    bool visible() const;
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
private:
    struct Entry {
        std::string name;
        int method = 0;
        int min = 0;
        int max = 0;
        std::unique_ptr<DMButton> label;
        std::unique_ptr<ButtonWidget> label_w;
        std::unique_ptr<DMDropdown> dd_method;
        std::unique_ptr<DropdownWidget> dd_method_w;
        std::unique_ptr<DMSlider> s_min;
        std::unique_ptr<SliderWidget> s_min_w;
        std::unique_ptr<DMSlider> s_max;
        std::unique_ptr<SliderWidget> s_max_w;
        std::unique_ptr<DMButton> b_delete;
        std::unique_ptr<ButtonWidget> b_delete_w;
    };
    void rebuild_entry_widgets();
    void rebuild_rows();
    nlohmann::json build_json() const;
    std::unique_ptr<FloatingCollapsible> panel_;
    std::vector<Entry> entries_;
    std::unique_ptr<DMButton> b_add_;
    std::unique_ptr<ButtonWidget> b_add_w_;
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;
    SearchAssets search_;
    std::function<void(const nlohmann::json&)> on_close_;
    std::vector<std::string> spawn_methods_;
};
