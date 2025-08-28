// === File: ui/menu_ui.hpp ===
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <unordered_set>

#include "main.hpp"         // for MainApp base
#include "text_style.hpp"   // for TextStyle / TextStyles

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
    ~MenuUI();

    void init();

    // Called by the outer loop to decide whether to return to main menu
    bool wants_return_to_main_menu() const { return return_to_main_menu_; }

private:
    struct Button {
        SDL_Rect   rect;
        std::string label;
        bool        hovered = false;
        MenuAction  action  = MenuAction::NONE;
    };

    // State
    std::vector<Button> buttons_;
    bool menu_active_          = true;
    bool return_to_main_menu_  = false;
    bool dev_mode_local_       = false;
    MenuAction last_action_    = MenuAction::NONE;

    // Main loop pieces
    void game_loop();
    void toggleMenu();
    void handle_event(const SDL_Event& e);
    void update(bool dev_mode_now);
    void render();

    // Actions
    MenuAction consumeAction();
    void rebuildButtons();
    void doExit();
    void doRestart();
    void doSettings();
    void doToggleDevMode();
    void doSaveCurrentRoom();

    // Text helpers using centralized styles
    void drawTextCentered(const std::string& text,
                          const SDL_Rect& rect,
                          const TextStyle& style,
                          bool hovered);
};
