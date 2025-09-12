#pragma once

#include "Asset.hpp"

inline void set_camera_recursive(Asset* asset, camera* v) {
	if (!asset) return;
	asset->set_camera(v);
	for (Asset* child : asset->children) {
		set_camera_recursive(child, v);
	}
}

inline void set_assets_owner_recursive(Asset* asset, Assets* owner) {
	if (!asset) return;
	asset->set_assets(owner);
	for (Asset* child : asset->children) {
		set_assets_owner_recursive(child, owner);
	}
}
