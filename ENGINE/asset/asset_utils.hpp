#pragma once

#include "Asset.hpp"

// Recursively assign the rendering view to an asset and all of its children.
inline void set_view_recursive(Asset* asset, view* v) {
    if (!asset) return;
    asset->set_view(v);
    for (Asset* child : asset->children) {
        set_view_recursive(child, v);
    }
}

// Recursively assign the owning Assets manager to an asset hierarchy.
inline void set_assets_owner_recursive(Asset* asset, Assets* owner) {
    if (!asset) return;
    asset->set_assets(owner);
    for (Asset* child : asset->children) {
        set_assets_owner_recursive(child, owner);
    }
}

