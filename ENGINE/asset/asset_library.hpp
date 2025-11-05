#pragma once

#include "asset_info.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

class AssetLibrary {

	public:
    explicit AssetLibrary(bool auto_load = true);
    void load_all_from_SRC();
    void add_asset(const std::string& name, const nlohmann::json& metadata);
    std::shared_ptr<AssetInfo> get(const std::string& name) const;
    const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& all() const;
    void loadAllAnimations(SDL_Renderer* renderer);
    void ensureAllAnimationsLoaded(SDL_Renderer* renderer);
    void loadAnimationsFor(SDL_Renderer* renderer, const std::unordered_set<std::string>& names);
    bool remove(const std::string& name);

        private:
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> info_by_name_;
    bool animations_fully_cached_ = false;
};
