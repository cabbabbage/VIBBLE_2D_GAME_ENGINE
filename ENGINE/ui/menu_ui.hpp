
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <unordered_set>

#include "main.hpp"

class MenuUI : public MainApp {
public:
    enum class MenuAction {
        NONE = 0,
        EXIT,               
        RESTART,
        SETTINGS,
        DEV_MODE_TOGGLE,
        SAVE_ROOM
    };

    MenuUI(SDL_Renderer* renderer,
           int screen_w,
           int screen_h,
           const std::string& map_path);
    ~MenuUI() override;

    void init() override;
    void game_loop() override;

    void handle_event(const SDL_Event& e);
    void update(bool dev_mode);
    void render();
    MenuAction consumeAction();

    
    bool wants_return_to_main_menu() const { return return_to_main_menu_; }

private:
    struct Button {
        SDL_Rect     rect;
        std::string  label;
        bool         hovered = false;
        MenuAction   action = MenuAction::NONE;
    };

    void rebuildButtons();
    void drawTextCentered(const std::string& text, const SDL_Rect& rect, SDL_Color color);
    void toggleMenu();

    void doExit();              
    void doRestart();
    void doSettings();
    void doToggleDevMode();
    void doSaveCurrentRoom();

private:
    bool                 menu_active_ = false;
    bool                 dev_mode_local_ = false;

    std::vector<Button>  buttons_;
    TTF_Font*            font_ = nullptr;

    MenuAction           last_action_ = MenuAction::NONE;

    bool                 return_to_main_menu_ = false; 
};
