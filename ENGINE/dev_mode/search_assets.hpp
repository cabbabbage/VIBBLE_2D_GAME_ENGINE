#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class DockableCollapsible;
class DMTextBox;
class DMButton;
class TextBoxWidget;
class ButtonWidget;
class Input;

class SearchAssets {
public:
    using Callback = std::function<void(const std::string&)>;
    SearchAssets();
    void set_position(int x, int y);
    void set_screen_dimensions(int width, int height);
    void set_floating_stack_key(std::string key);
    void set_anchor_position(int x, int y);
    void open(Callback cb);
    void close();
    bool visible() const;
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_point_inside(int x, int y) const;
private:
    struct Asset { std::string name; std::vector<std::string> tags; };
    void load_assets();
    void filter_assets();
    static std::string to_lower(std::string s);
    void apply_position(int x, int y);
    void ensure_visible_position();
    std::unique_ptr<DockableCollapsible> panel_;
    std::unique_ptr<DMTextBox> query_;
    std::unique_ptr<TextBoxWidget> query_widget_;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<ButtonWidget>> button_widgets_;
    Callback cb_;
    std::vector<Asset> all_;
    std::vector<std::pair<std::string,bool>> results_; // bool:true if tag
    std::string last_query_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;
    SDL_Point last_known_position_{64, 64};
    SDL_Point pending_position_{64, 64};
    bool has_pending_position_ = false;
    bool has_custom_position_ = false;
    std::string floating_stack_key_;
};
