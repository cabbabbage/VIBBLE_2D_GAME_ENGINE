#include "controller_factory.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "core/AssetsManager.hpp"
#include "animation_update/custom_controllers/Davey_controller.hpp"
#include "animation_update/custom_controllers/Vibble_controller.hpp"
#include "animation_update/custom_controllers/Frog_controller.hpp"
#include "animation_update/custom_controllers/Bomb_controller.hpp"
#include "animation_update/custom_controllers/Bartender_controller.hpp"
#include "animation_update/custom_controllers/Carrie_controller.hpp"
#include "animation_update/custom_controllers/Gary_controller.hpp"
#include "animation_update/custom_controllers/spider_controller.hpp"

#include "animation_update/custom_controllers/default_controller.hpp"
#include "utils/log.hpp"

#include <exception>
#include <new>
#include <string>

namespace {

std::string asset_name_for_log(const Asset* asset) {
        if (!asset) return "<null asset>";
        if (!asset->info) return "<missing info>";
        return asset->info->name.empty() ? std::string{"<unnamed>"} : asset->info->name;
}

std::size_t animation_frame_count(const AssetInfo* info) {
        if (!info) return 0;
        std::size_t count = 0;
        for (const auto& entry : info->animations) {
                count += entry.second.frames.size();
        }
        return count;
}

std::size_t default_animation_frame_count(const AssetInfo* info) {
        if (!info) return 0;
        auto it = info->animations.find("default");
        if (it == info->animations.end()) return 0;
        return it->second.frames.size();
}

std::size_t resident_variant_slot_count(const AssetInfo* info) {
        if (!info) return 0;
        std::size_t count = 0;
        for (const auto& entry : info->animations) {
                const Animation& animation = entry.second;
                const std::size_t variants = animation.variant_count();
                count += animation.frames.size() * (variants == 0 ? 1 : variants);
        }
        return count;
}

bool has_loaded_default_animation(const AssetInfo* info) {
        if (!info) return false;
        auto it = info->animations.find("default");
        return it != info->animations.end() && !it->second.frames.empty();
}

bool is_player_asset(const Asset* asset) {
        if (!asset || !asset->info) return false;
        return asset->info->type == "Player" || asset->info->type == "player";
}

std::string controller_context_for_log(const Assets* assets,
                                       const Asset* asset,
                                       const std::string& key) {
        std::string message = "controller='" + (key.empty() ? std::string{"<default>"} : key) +
                              "' asset='" + asset_name_for_log(asset) + "'";
        if (asset) {
                message += " spawn_id='" + (asset->spawn_id.empty() ? std::string{"<none>"} : asset->spawn_id) + "'";
                message += " children=" + std::to_string(asset->asset_children.size());
        }
        if (asset && asset->info) {
                message += " animations_loaded=" + std::string{has_loaded_default_animation(asset->info.get()) ? "yes" : "no"};
                message += " default_frames=" + std::to_string(default_animation_frame_count(asset->info.get()));
                message += " total_frames=" + std::to_string(animation_frame_count(asset->info.get()));
                message += " variant_slots=" + std::to_string(resident_variant_slot_count(asset->info.get()));
                message += " animation_children=" + std::to_string(asset->info->animation_children.size());
                message += " async_children=" + std::to_string(asset->info->async_children.size());
                message += " areas=" + std::to_string(asset->info->areas.size());
        }
        if (assets) {
                message += " assets_known=" + std::to_string(assets->all.size());
        }
        return message;
}

std::unique_ptr<AssetController> make_default_controller_or_null(Asset* self,
                                                                 const std::string& source_key,
                                                                 const char* reason) {
        try {
                return std::make_unique<DefaultController>(self);
        } catch (const std::bad_alloc&) {
                vibble::log::debug(std::string{"[ControllerFactory] Default controller allocation failed after "} +
                                   reason + " for " + controller_context_for_log(nullptr, self, source_key) +
                                   "; leaving asset without controller.");
                return nullptr;
        } catch (const std::exception& e) {
                vibble::log::debug(std::string{"[ControllerFactory] Default controller construction failed after "} +
                                   reason + " for asset '" + asset_name_for_log(self) + "': " + e.what());
                return nullptr;
        } catch (...) {
                vibble::log::debug(std::string{"[ControllerFactory] Default controller construction failed after "} +
                                   reason + " for asset '" + asset_name_for_log(self) + "'.");
                return nullptr;
        }
}

} // namespace

ControllerFactory::ControllerFactory(Assets* assets)
: assets_(assets)
{}

ControllerFactory::~ControllerFactory() = default;

std::unique_ptr<AssetController>
ControllerFactory::create_by_key(const std::string& key, Asset* self) const {
        if (!assets_ || !self || !self->info) return nullptr;
        try {
                if (key == "Davey_controller")
                        return std::make_unique<DaveyController>(assets_, self);
                if (is_player_asset(self))
                        return std::make_unique<VibbleController>(self);
                if (key == "Frog_controller" || self->info->name == "frog")
                        return std::make_unique<FrogController>(assets_, self);
                if (key == "Carrie_controller" || self->info->name == "Carrie")
                        return std::make_unique<CarrieController>(assets_, self);
                if (key == "Gary_controller" || self->info->name == "Gary")
                        return std::make_unique<GaryController>(assets_, self);
                if (key == "Bartender_controller" || self->info->name == "Bartender")
                        return std::make_unique<BartenderController>(assets_, self);
                if (key == "spider_controller" || self->info->name == "spider")
                        return std::make_unique<spiderController>(assets_, self);

                if (key == "Bomb_controller" || self->info->name == "bomb")
                        return std::make_unique<BombController>(assets_, self);
        } catch (const std::bad_alloc&) {
                vibble::log::debug("[ControllerFactory] bad_alloc while constructing custom controller; " +
                                   controller_context_for_log(assets_, self, key) +
                                   ". Not allocating fallback controller.");
                return nullptr;
        } catch (const std::exception& e) {
                vibble::log::debug("[ControllerFactory] Failed to construct controller '" + key +
                                   "' for asset '" + asset_name_for_log(self) + "': " + e.what() +
                                   "; using default controller.");
                return make_default_controller_or_null(self, key, "custom controller exception");
        } catch (...) {
                vibble::log::debug("[ControllerFactory] Failed to construct controller '" + key +
                                   "' for asset '" + asset_name_for_log(self) +
                                   "': unknown exception; using default controller.");
                return make_default_controller_or_null(self, key, "custom controller exception");
        }

        if (!key.empty()) {
                vibble::log::debug("[ControllerFactory] Unknown controller key '" + key +
                                   "' for asset '" + asset_name_for_log(self) +
                                   "'; using default controller.");
        }
        return make_default_controller_or_null(self, key, "unknown controller key");
}

std::unique_ptr<AssetController>
ControllerFactory::create_for_asset(Asset* self) const {
        if (!assets_ || !self || !self->info) return nullptr;
        const std::string key = self->info->custom_controller_key;
        if (is_player_asset(self)) {
                try {
                        return std::make_unique<VibbleController>(self);
                } catch (const std::bad_alloc&) {
                        vibble::log::debug("[ControllerFactory] bad_alloc while constructing player controller; " +
                                           controller_context_for_log(assets_, self, key) +
                                           ". Not allocating fallback controller.");
                        return nullptr;
                } catch (const std::exception& e) {
                        vibble::log::debug("[ControllerFactory] Failed to construct player controller for asset '" +
                                           asset_name_for_log(self) + "': " + e.what());
                        return nullptr;
                } catch (...) {
                        vibble::log::debug("[ControllerFactory] Failed to construct player controller for asset '" +
                                           asset_name_for_log(self) + "': unknown exception.");
                        return nullptr;
                }
        }
        if (!key.empty()) {
                return create_by_key(key, self);
        }
        return make_default_controller_or_null(self, key, "missing controller key");
}
