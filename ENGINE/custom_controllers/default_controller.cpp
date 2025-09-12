#include "default_controller.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"

DefaultController::DefaultController(Asset* self)
    : self_(self) {}

void DefaultController::update(const Input& /*in*/) {

}
