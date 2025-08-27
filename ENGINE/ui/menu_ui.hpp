// === File: menu_ui.hpp ===
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

class MenuUI {
public:
    enum class MenuAction {
        NONE,
        EXIT,
        RESTART,
        SETTINGS,
        DEV_MODE_TOGGLE,
        SAVE_ROOM
    };

    struct Button {
        SDL_Rect rect;
        std::string label;
        bool hovered = false;
        MenuAction action;
    };

    MenuUI(SDL_Renderer* renderer, int screen_w, int screen_h, bool dev_mode);
    ~MenuUI();

    void handle_event(const SDL_Event& e);
    void update(bool dev_mode);
    void render();

    // one-shot read + clear
    MenuAction consumeAction() {
        MenuAction a = last_action_;
        last_action_ = MenuAction::NONE;
        return a;
    }

    // allow engine to update the label when dev mode flips
    void setDevMode(bool enabled);

private:
    SDL_Renderer* renderer_;
    int screen_w_, screen_h_;
    TTF_Font* font_ = nullptr;

    bool dev_mode_ = false;
    std::vector<Button> buttons_;
    MenuAction last_action_ = MenuAction::NONE;

    void drawTextCentered(const std::string& text, const SDL_Rect& rect, SDL_Color color);
    void rebuildButtons();
};
