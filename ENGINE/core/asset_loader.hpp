
#pragma once

#include <string>
#include <vector>
#include <memory>

class Asset;
class Assets;
class Room;
class Area;
class AssetLibrary;
struct SDL_Texture;
struct SDL_Renderer;
struct LayerSpec;

class AssetLoader {
public:
    AssetLoader(const std::string& map_dir, SDL_Renderer* renderer);

    std::vector<Asset*> collectDistantAssets(int fade_start_distance, int fade_end_distance);
    std::vector<std::vector<Asset*>> group_neighboring_assets(
        const std::vector<Asset*>& assets, 
        int tile_width, 
        int tile_height, 
        const std::string& group_type);

    
    void link_by_child(const std::vector<std::vector<Asset*>>& groups);

    std::unique_ptr<Assets> createAssets(int screen_width, int screen_height);

    std::vector<Area> getAllRoomAndTrailAreas() const;
    SDL_Texture* createMinimap(int width, int height);

private:
    
    std::string map_path_;
    SDL_Renderer* renderer_;

    
    std::vector<Room*> rooms_;
    std::vector<std::unique_ptr<Room>> all_rooms_;
    std::unique_ptr<AssetLibrary> asset_library_;

    
    std::vector<LayerSpec>              map_layers_;
    double map_center_x_ = 0.0;
    double map_center_y_ = 0.0;
    double map_radius_   = 0.0;
    std::string map_boundary_file_;

    
    void load_map_json();
    void loadRooms();
    void finalizeAssets();

    std::vector<Asset> extract_all_assets();
    void removeMergedAssets(const std::vector<Asset*>& to_remove, Asset* skip = nullptr);

    
};
