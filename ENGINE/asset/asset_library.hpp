#pragma once

#include "asset_info.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

class AssetLibrary {

	public:
    AssetLibrary();
    void load_all_from_SRC();
    std::shared_ptr<AssetInfo> get(const std::string& name) const;
    const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& all() const;
    void loadAllAnimations(SDL_Renderer* renderer);
    void loadAnimationsFor(SDL_Renderer* renderer, const std::unordered_set<std::string>& names);

	private:
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> info_by_name_;
};

