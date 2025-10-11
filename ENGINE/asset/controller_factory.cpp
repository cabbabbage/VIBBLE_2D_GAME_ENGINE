#include "controller_factory.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "custom_controllers/Davey_controller.hpp"
#include "custom_controllers/Vibble_controller.hpp"
#include "custom_controllers/Frog_controller.hpp"
#include "custom_controllers/Bomb_controller.hpp"
#include "custom_controllers/default_controller.hpp"

ControllerFactory::ControllerFactory(Assets* assets)
: assets_(assets)
{}

ControllerFactory::~ControllerFactory() = default;

std::unique_ptr<AssetController>
ControllerFactory::create_by_key(const std::string& key, Asset* self) const {
	if (!assets_ || !self) return nullptr;
        try {
                if (key == "Davey_controller")
                        return std::make_unique<DaveyController>(assets_, self);
                if (key == "Vibble_controller")
                        return std::make_unique<VibbleController>(self);
                if (key == "Frog_controller")

                        return std::make_unique<FrogController>(assets_, self);

                if (key == "Bomb_controller")
                        return std::make_unique<BombController>(assets_, self);
        } catch (...) {
        }
        return std::make_unique<DefaultController>(self);
}

std::unique_ptr<AssetController>
ControllerFactory::create_for_asset(Asset* self) const {
        if (!assets_ || !self || !self->info) return nullptr;
        const std::string key = self->info->custom_controller_key;
        if (!key.empty()) {
                return create_by_key(key, self);
        }
        return std::make_unique<DefaultController>(self);
}
