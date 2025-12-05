#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "styles.hpp"
#include "button.hpp"

class MainMenu {

	public:
    struct Selection {
        std::string    id;
        nlohmann::json data;
};

    MainMenu(SDL_Renderer* renderer, int screen_w, int screen_h, const nlohmann::json& maps);
    ~MainMenu();
    void buildButtons();
    std::optional<Selection> handle_event(const SDL_Event& e);
    void render();
    void showLoadingScreen();

	private:
    SDL_Texture* loadTexture(const std::string& abs_utf8_path);
    SDL_Texture* loadTexture(const std::filesystem::path& p);
    std::filesystem::path firstImageIn(const std::filesystem::path& folder) const;
    SDL_Rect coverDst(SDL_Texture* tex) const;
    SDL_Rect fitCenter(SDL_Texture* tex, int max_w, int max_h, int cx, int cy) const;
    SDL_Point measureText(const LabelStyle& style, const std::string& s) const;
    void blitText(SDL_Renderer* r, const LabelStyle& style, const std::string& s, int x, int y, bool shadow, SDL_Color override_col) const;
    void blitTextCentered(SDL_Renderer* r, const LabelStyle& style, const std::string& s, const SDL_Rect& rect, bool shadow, SDL_Color override_col) const;
    std::string pickRandomLine(const std::filesystem::path& csv_path) const;
    void drawVignette(Uint8 alpha) const;
    void renderAnimatedBackground(SDL_Texture* tex) const;
    std::filesystem::path loading_content_root() const;
    std::vector<std::filesystem::path> list_loading_images(const std::filesystem::path& root, bool recursive) const;
    std::filesystem::path pick_loading_image() const;

        private:
    SDL_Renderer* renderer_ = nullptr;
    int screen_w_ = 0;
    int screen_h_ = 0;
    struct MenuEntry {
        Button button;
        std::string action;
        bool is_map = false;
};

    SDL_Texture* background_tex_ = nullptr;
    std::filesystem::path background_image_path_;
    std::vector<MenuEntry> buttons_;
    const nlohmann::json* maps_json_ = nullptr;
    std::unordered_map<std::string, const nlohmann::json*> map_lookup_;
    std::filesystem::path manifest_root_;
    Uint64 animation_start_ticks_ = 0;

    std::filesystem::path resolve_manifest_path(const std::string& forward_path) const;
};
