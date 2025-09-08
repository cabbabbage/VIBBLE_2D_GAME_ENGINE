#include "controller_factory.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"
#include "custom_controllers/Davey_controller.hpp"
#include "custom_controllers/Vibble_controller.hpp"
#include "custom_controllers/Frog_controller.hpp"
#include "custom_controllers/Bomb_controller.hpp"
#include "custom_controllers/default_controller.hpp"

ControllerFactory::ControllerFactory(Assets* assets, ActiveAssetsManager& aam)
: assets_(assets)
, aam_(aam)
{}

ControllerFactory::~ControllerFactory() = default;

std::unique_ptr<AssetController>
ControllerFactory::create_by_key(const std::string& key, Asset* self) const {
	if (!assets_ || !self) return nullptr;
	try {
		if (key == "Davey_controller")
		return std::make_unique<DaveyController>(assets_, self, aam_);
		if (key == "Vibble_controller")
		return std::make_unique<VibbleController>(assets_, self, aam_);
		if (key == "Frog_controller")
		return std::make_unique<FrogController>(assets_, self, aam_);
		if (key == "Bomb_controller")
		return std::make_unique<BombController>(assets_, self, aam_);
	} catch (...) {
	}
	return std::make_unique<DefaultController>(assets_, self, aam_);
}

std::unique_ptr<AssetController>
ControllerFactory::create_for_asset(Asset* self) const {
	if (!assets_ || !self || !self->info) return nullptr;
	const std::string key = self->info->custom_controller_key;
	if (!key.empty()) {
		return create_by_key(key, self);
	}
	return std::make_unique<DefaultController>(assets_, self, aam_);
}
