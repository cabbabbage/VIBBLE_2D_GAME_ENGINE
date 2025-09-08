#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "core/active_assets_manager.hpp"
DefaultController::DefaultController(Assets* assets, Asset* self, ActiveAssetsManager& aam)
 : assets_(assets)
 , self_(self)
 , aam_(aam)
{}

DefaultController::~DefaultController() = default;

void DefaultController::update(const Input& /*in*/) {
 // No behavior; drive animation progression explicitly.
 if (self_) self_->update_animation_manager();
}
