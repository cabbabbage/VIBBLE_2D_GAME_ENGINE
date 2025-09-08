
#include "AssetsManager.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"   
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "dev_mode/dev_mouse_controls.hpp"
#include "utils/input.hpp"
#include "render/scene_renderer.hpp"
#include "ui/asset_library_ui.hpp"
#include "ui/asset_info_ui.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <cmath>
#include <limits>


Assets::Assets(std::vector<Asset>&& loaded,
               AssetLibrary& library,
               Asset* ,
               std::vector<Room*> rooms,
               int screen_width_,
               int screen_height_,
               int screen_center_x,
               int screen_center_y,
               int map_radius,
               SDL_Renderer* renderer,
               const std::string& map_path)
    : window(screen_width_, screen_height_, view::Bounds{
          -map_radius, map_radius,
          -map_radius, map_radius
      }),
      activeManager(screen_width_, screen_height_, window),
      screen_width(screen_width_),
      screen_height(screen_height_),
      library_(library)   
{
    InitializeAssets::initialize(*this,
                                 std::move(loaded),
                                 std::move(rooms),
                                 screen_width_,
                                 screen_height_,
                                 screen_center_x,
                                 screen_center_y,
                                 map_radius);

    finder_ = new CurrentRoomFinder(rooms_, player);
    if (finder_) {
        window.set_up_rooms(finder_);
    }

    // Create scene renderer owned by Assets
    scene = new SceneRenderer(renderer, this, screen_width_, screen_height_, map_path);

    // Ensure all existing assets have a back-reference to this manager
    for (Asset* a : all) {
        if (a) a->set_assets(this);
    }
}


Assets::~Assets() {
    delete scene;
    delete finder_;
    delete dev_mouse;
    delete library_ui_;
    delete info_ui_;
}

void Assets::set_input(Input* m) {
    input = m;

    delete dev_mouse; 
    if (input) {
        dev_mouse = new DevMouseControls(input,
                                         this,
                                         active_assets,
                                         player,        
                                         screen_width,  
                                         screen_height);       
    } else {
        dev_mouse = nullptr;
    }
}

void Assets::update(const Input& input,
                    int screen_center_x,
                    int screen_center_y)
{
    activeManager.updateAssetVectors(player, screen_center_x, screen_center_y);

    current_room_ = finder_ ? finder_->getCurrentRoom() : nullptr;
    window.update_zoom(current_room_, finder_, player);

    dx = dy = 0;

    // Track player start position for this tick
    int start_px = player ? player->pos_X : 0;
    int start_py = player ? player->pos_Y : 0;

    // Per-asset controllers now live inside each Asset instance

    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    // Advance player (applies per-frame movement from animation)
    if (player) player->update();
    // Compute dx/dy from resulting position delta
    if (player) {
        dx = player->pos_X - start_px;
        dy = player->pos_Y - start_py;
    }
    // Update distance_to_player for all active assets before their updates,
    // so controllers can use a fresh value this frame.
    if (player) {
        const int px = player->pos_X;
        const int py = player->pos_Y;
        player->distance_to_player = 0.0f;
        for (Asset* a : active_assets) {
            if (!a || a == player) continue;
            const float dxp = float(a->pos_X - px);
            const float dyp = float(a->pos_Y - py);
            const float d2  = dxp*dxp + dyp*dyp;
            a->distance_to_player = std::sqrt(d2);
        }
    } else {
        for (Asset* a : active_assets) {
            if (!a) continue;
            a->distance_to_player = std::numeric_limits<float>::infinity();
        }
    }
    for (Asset* a : active_assets) {
        if (a && a != player)
            a->update();
    }

    if (dx != 0 || dy != 0)
        activeManager.sortByZIndex();

    
    if (dev_mode && dev_mouse) {
        bool ui_blocking = (library_ui_ && library_ui_->is_visible()) || (info_ui_ && info_ui_->is_visible());
        if (!ui_blocking) {
            dev_mouse->handle_mouse_input(input);
        }
    }


    if (input.wasScancodePressed(SDL_SCANCODE_TAB)) {
        toggle_asset_library();
    }


    update_ui(input);


    if (scene && !suppress_render_) scene->render();


    {
        std::vector<Asset*> pending;
        pending.reserve(owned_assets.size());
        for (const auto& up : owned_assets) {
            if (up && up->needs_removal()) pending.push_back(up.get());
        }
        for (Asset* a : pending) {
            if (a) a->Delete();
        }
    }
}

// Removal is centralized in Asset::~Asset and the owned_assets erasure above.

void Assets::set_dev_mode(bool mode) {
    dev_mode = mode;
    if (dev_mode) {
        close_asset_library();
        close_asset_info_editor();
    }

    if (input) input->clearClickBuffer();
    if (dev_mouse) dev_mouse->reset_click_state();
}

void Assets::set_render_suppressed(bool suppressed) {
    suppress_render_ = suppressed;
}


const std::vector<Asset*>& Assets::get_selected_assets() const {
    static std::vector<Asset*> empty;
    return dev_mouse ? dev_mouse->get_selected_assets() : empty;
}

const std::vector<Asset*>& Assets::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    return dev_mouse ? dev_mouse->get_highlighted_assets() : empty;
}

Asset* Assets::get_hovered_asset() const {
    return dev_mouse ? dev_mouse->get_hovered_asset() : nullptr;
}

nlohmann::json Assets::save_current_room(std::string room_name) {
    if (!current_room_) {
        throw std::runtime_error("[Assets] No current room to save!");
    }

    nlohmann::json j = current_room_->create_static_room_json(room_name);
    j["room_name"] = room_name; 

    return j;
}

void Assets::addAsset(const std::string& name, int gx, int gy) {
    std::cout << "\n[Assets::addAsset] Request to create asset '" << name
              << "' at grid (" << gx << ", " << gy << ")\n";


    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets::addAsset][Error] No asset info found for '" << name << "'\n";
        return;
    }
    std::cout << "[Assets::addAsset] Retrieved AssetInfo '" << info->name
              << "' at " << info << "\n";


    Area spawn_area(name, gx, gy, 1, 1, "Point", 1, 1, 1);
    std::cout << "[Assets::addAsset] Created Area '" << spawn_area.get_name()
              << "' at (" << gx << ", " << gy << ")\n";


    size_t prev_size = owned_assets.size();


    owned_assets.emplace_back(
        std::make_unique<Asset>(info, spawn_area, gx, gy, 0, nullptr));

    if (owned_assets.size() <= prev_size) {
        std::cerr << "[Assets::addAsset][Error] owned_assets did not grow!\n";
        return;
    }

    Asset* newAsset = owned_assets.back().get();
    if (!newAsset) {
        std::cerr << "[Assets::addAsset][Error] Asset allocation failed for '" << name << "'\n";
        return;
    }
    std::cout << "[Assets::addAsset][Debug] New Asset allocated at " << newAsset
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>")
              << ")\n";

    // Insert into master list
    all.push_back(newAsset);
    std::cout << "[Assets::addAsset] all.size() now = " << all.size() << "\n";

    // Set view + finalize
    try {
        set_view_recursive(newAsset, &window);
        set_assets_owner_recursive(newAsset, this);
        std::cout << "[Assets::addAsset] View set successfully\n";
        newAsset->finalize_setup();
        std::cout << "[Assets::addAsset] Finalize setup successful\n";
    } catch (const std::exception& e) {
        std::cerr << "[Assets::addAsset][Exception] " << e.what() << "\n";
    }

    // Activate in manager
    activeManager.activate(newAsset);
    activeManager.updateClosestAssets(player, 3);
    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    std::cout << "[Assets::addAsset] Active assets=" << active_assets.size()
              << ", Closest=" << closest_assets.size() << "\n";

    std::cout << "[Assets::addAsset] Successfully added asset '" << name
              << "' at (" << gx << ", " << gy << ")\n";
}


Asset* Assets::spawn_asset(const std::string& name, int world_x, int world_y) {
    std::cout << "\n[Assets::spawn_asset] Request to spawn asset '" << name
              << "' at world (" << world_x << ", " << world_y << ")\n";

    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets::spawn_asset][Error] No asset info found for '" << name << "'\n";
        return nullptr;
    }
    std::cout << "[Assets::spawn_asset] Retrieved AssetInfo '" << info->name
              << "' at " << info << "\n";

    Area spawn_area(name, world_x, world_y, 1, 1, "Point", 1, 1, 1);
    std::cout << "[Assets::spawn_asset] Created Area '" << spawn_area.get_name()
              << "' at (" << world_x << ", " << world_y << ")\n";

    size_t prev_size = owned_assets.size();
    owned_assets.emplace_back(
        std::make_unique<Asset>(info, spawn_area, world_x, world_y, 0, nullptr));

    if (owned_assets.size() <= prev_size) {
        std::cerr << "[Assets::spawn_asset][Error] owned_assets did not grow!\n";
        return nullptr;
    }

    Asset* newAsset = owned_assets.back().get();
    if (!newAsset) {
        std::cerr << "[Assets::spawn_asset][Error] Asset allocation failed for '" << name << "'\n";
        return nullptr;
    }

    std::cout << "[Assets::spawn_asset][Debug] New Asset allocated at " << newAsset
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>")
              << ")\n";

    all.push_back(newAsset);
    std::cout << "[Assets::spawn_asset] all.size() now = " << all.size() << "\n";

    try {
        set_view_recursive(newAsset, &window);
        set_assets_owner_recursive(newAsset, this);
        std::cout << "[Assets::spawn_asset] View set successfully\n";
        newAsset->finalize_setup();
        std::cout << "[Assets::spawn_asset] Finalize setup successful\n";
    } catch (const std::exception& e) {
        std::cerr << "[Assets::spawn_asset][Exception] " << e.what() << "\n";
    }

    activeManager.activate(newAsset);
    activeManager.updateClosestAssets(player, 3);
    active_assets  = activeManager.getActive();
    closest_assets = activeManager.getClosest();

    std::cout << "[Assets::spawn_asset] Active assets=" << active_assets.size()
              << ", Closest=" << closest_assets.size() << "\n";

    std::cout << "[Assets::spawn_asset] Successfully spawned asset '" << name
              << "' at (" << world_x << ", " << world_y << ")\n";

    return newAsset;
}

void Assets::delete_asset(Asset* asset) {
    if (!asset) return;
    auto it = std::find_if(owned_assets.begin(), owned_assets.end(),
                           [asset](const std::unique_ptr<Asset>& p){ return p.get() == asset; });
    if (it != owned_assets.end()) {
        owned_assets.erase(it);
    }
}


void Assets::render_overlays(SDL_Renderer* renderer) {
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->render(renderer, library_, screen_width, screen_height);
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->render(renderer, screen_width, screen_height);
    }
}

void Assets::toggle_asset_library() {
    if (!library_ui_) library_ui_ = new AssetLibraryUI();
    library_ui_->toggle();
}

void Assets::open_asset_library() {
    if (!library_ui_) library_ui_ = new AssetLibraryUI();
    library_ui_->open();
}

void Assets::close_asset_library() {
    if (library_ui_) library_ui_->close();
}

bool Assets::is_asset_library_open() const {
    return library_ui_ && library_ui_->is_visible();
}

void Assets::update_ui(const Input& input) {
    if (library_ui_ && library_ui_->is_visible()) {
        library_ui_->update(input, screen_width, screen_height, library_);
    }
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->update(input, screen_width, screen_height);
    }
}

std::shared_ptr<AssetInfo> Assets::consume_selected_asset_from_library() {
    if (!library_ui_) return nullptr;
    return library_ui_->consume_selection();
}

void Assets::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!info) return;
    if (!info_ui_) info_ui_ = new AssetInfoUI();
    info_ui_->set_info(info);
    info_ui_->open();
}

void Assets::close_asset_info_editor() {
    if (info_ui_) info_ui_->close();
}

bool Assets::is_asset_info_editor_open() const {
    return info_ui_ && info_ui_->is_visible();
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    if (info_ui_ && info_ui_->is_visible()) {
        info_ui_->handle_event(e);
    }
}