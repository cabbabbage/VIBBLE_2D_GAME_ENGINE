#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <filesystem>

#include "main.hpp"
#include "styles.hpp"
#include "button.hpp"

class MenuUI : public MainApp {

	public:
    enum class MenuAction {
    NONE = 0,
    EXIT,
    RESTART,
    SETTINGS,
    SAVE_ROOM
        };
    MenuUI(SDL_Renderer* renderer, int screen_w, int screen_h, const std::string& map_path);
    ~MenuUI();
    void init();
    bool wants_return_to_main_menu() const;

	private:
    void game_loop();
    void toggleMenu();
    void handle_event(const SDL_Event& e);
    void render();
    MenuAction consumeAction();
    void rebuildButtons();
    SDL_Point measureText(const LabelStyle& style, const std::string& s) const;
    void blitText(SDL_Renderer* r, const LabelStyle& style, const std::string& s, int x, int y, bool shadow, SDL_Color override_col) const;
    void blitTextCentered(SDL_Renderer* r, const LabelStyle& style, const std::string& s, const SDL_Rect& rect, bool shadow, SDL_Color override_col) const;
    SDL_Texture* loadTexture(const std::string& abs_utf8_path);
    SDL_Texture* loadTexture(const std::filesystem::path& p);
    std::filesystem::path firstImageIn(const std::filesystem::path& folder) const;
    SDL_Rect coverDst(SDL_Texture* tex) const;
    SDL_Rect fitCenter(SDL_Texture* tex, int max_w, int max_h, int cx, int cy) const;
    std::string pickRandomLine(const std::filesystem::path& csv_path) const;
    void drawVignette(Uint8 alpha) const;
    void doExit();
    void doRestart();
    void doSettings();
    void doToggleDevMode();
    void doSaveCurrentRoom();

	private:
    struct MenuButton {
    Button     button;
    MenuAction action = MenuAction::NONE;
	};
    bool menu_active_ = false;
    MenuAction last_action_ = MenuAction::NONE;
    bool return_to_main_menu_ = false;
    SDL_Texture* background_tex_ = nullptr;
    std::vector<MenuButton> buttons_;
};
