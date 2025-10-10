#pragma once

#include "../DockableCollapsible.hpp"

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class AssetInfoUI;
class Input;
class DMButton;
class ButtonWidget;

class Section_SpawnGroups : public DockableCollapsible {
public:
    Section_SpawnGroups();

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }

    void build() override;
    void layout() override;
    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;

private:
    void reload_from_file();
    bool save_to_file();
    void renumber_priorities();

    void add_spawn_group();
    void duplicate_spawn_group(const std::string& id);
    void delete_spawn_group(const std::string& id);
    void move_spawn_group(const std::string& id, int dir);

    int index_of(const std::string& id) const;

    SDL_Point editor_anchor_point() const;

    void schedule_rebuild();

private:
    AssetInfoUI* ui_ = nullptr;
    nlohmann::json groups_ = nlohmann::json::array();

    std::unique_ptr<class SpawnGroupConfig> list_;

    std::unique_ptr<DMButton> add_btn_;
    std::unique_ptr<ButtonWidget> add_btn_w_;

    int screen_w_ = 1920;
    int screen_h_ = 1080;

    bool rebuilding_ = false;
    bool rebuild_requested_ = false;
};
