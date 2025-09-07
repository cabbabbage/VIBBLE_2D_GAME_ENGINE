#include "default_controller.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"

DefaultController::DefaultController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
 : assets_(assets)
 , self_(self)
 , aam_(aam)
{}

DefaultController::~DefaultController() {}

void DefaultController::update(const Input& /*in*/) {
 // dummy controller no animation updates
 // animation progression and on end mapping handled by AnimationManager
 // future basic behavior (eg idle state changes) can go here
 (void)assets_;
 (void)self_;
 (void)aam_;
}
