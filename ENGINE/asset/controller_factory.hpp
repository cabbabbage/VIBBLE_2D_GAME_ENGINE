#pragma once
#include <memory>
#include <string>

class Assets;
class Asset;
class AssetController;

class ControllerFactory {

	public:
    ControllerFactory(Assets* assets);
    ~ControllerFactory();
    std::unique_ptr<AssetController> create_by_key(const std::string& key, Asset* self) const;
    std::unique_ptr<AssetController> create_for_asset(Asset* self) const;

        private:
    Assets* assets_;
};
