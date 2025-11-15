#pragma once

#include <string>
#include <vector>

// Lightweight per-frame combat geometry types used by both the runtime
// and editor tooling. All coordinates are expressed in the asset's local
// space at 100% scale, with (0,0) at the bottom-center of the sprite.
// Positive X is right, positive Y is up. Callers are responsible for
// applying scale/flip and translating from the bottom-center anchor to
// world space.

namespace animation_update {

struct FrameHitGeometry {
    struct HitBox {
        std::string type;
        float center_x   = 0.0f;
        float center_y   = 0.0f;
        float half_width = 0.0f;
        float half_height = 0.0f;
        float rotation_degrees = 0.0f;

        bool is_empty() const {
            return half_width <= 0.0f || half_height <= 0.0f;
        }
    };

    std::vector<HitBox> boxes;

    HitBox* find_box(const std::string& type) {
        for (auto& box : boxes) {
            if (box.type == type) return &box;
        }
        return nullptr;
    }

    const HitBox* find_box(const std::string& type) const {
        for (const auto& box : boxes) {
            if (box.type == type) return &box;
        }
        return nullptr;
    }
};

struct FrameAttackGeometry {
    struct Vector {
        // Quadratic Bezier segment in local space, typically used as an attack ray.
        std::string type;
        float start_x = 0.0f;
        float start_y = 0.0f;
        // Control point for curvature (quadratic Bezier). When equal to the
        // midpoint between start and end, the curve appears as a straight line.
        float control_x = 0.0f;
        float control_y = 0.0f;
        float end_x   = 0.0f;
        float end_y   = 0.0f;
        int   damage  = 0;    // damage value applied if this vector hits
    };

    std::vector<Vector> vectors;

    Vector* find_vector(const std::string& type) {
        for (auto& v : vectors) {
            if (v.type == type) return &v;
        }
        return nullptr;
    }

    const Vector* find_vector(const std::string& type) const {
        for (const auto& v : vectors) {
            if (v.type == type) return &v;
        }
        return nullptr;
    }
};

} // namespace animation_update
