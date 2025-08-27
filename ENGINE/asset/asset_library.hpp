// === File: asset_library.hpp ===
#pragma once

#include "asset_info.hpp"
#include <string>
#include <unordered_map>
#include <memory>

class AssetLibrary {
public:
    AssetLibrary();

    void load_all_from_SRC();
    std::shared_ptr<AssetInfo> get(const std::string& name) const;
    const std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& all() const;
    void loadAllAnimations(SDL_Renderer* renderer);

private:
    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> info_by_name_;
};
