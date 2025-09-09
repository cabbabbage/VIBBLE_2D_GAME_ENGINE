#include "move.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
void Move::apply(Asset* self, const Animation::FrameMovement& fm) {
	if (!self) return;
	self->pos.x += fm.dx;
	self->pos.y += fm.dy;
	if (fm.sort_z_index) {
		self->recompute_z_index();
		if (Assets* as = self->get_assets()) {
			as->activeManager.sortByZIndex();
		}
	}
}
