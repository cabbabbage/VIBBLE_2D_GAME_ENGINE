#pragma once

#include <string>
#include <vector>

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

        std::string type;
        float start_x = 0.0f;
        float start_y = 0.0f;

        float control_x = 0.0f;
        float control_y = 0.0f;
        float end_x   = 0.0f;
        float end_y   = 0.0f;
        int   damage  = 0;
};

    std::vector<Vector> vectors;

    std::size_t count_for_type(const std::string& type) const {
        std::size_t count = 0;
        for (const auto& v : vectors) {
            if (v.type == type) {
                ++count;
            }
        }
        return count;
    }

    Vector* vector_at(const std::string& type, std::size_t type_index) {
        std::size_t seen = 0;
        for (auto& v : vectors) {
            if (v.type != type) continue;
            if (seen == type_index) {
                return &v;
            }
            ++seen;
        }
        return nullptr;
    }

    const Vector* vector_at(const std::string& type, std::size_t type_index) const {
        std::size_t seen = 0;
        for (const auto& v : vectors) {
            if (v.type != type) continue;
            if (seen == type_index) {
                return &v;
            }
            ++seen;
        }
        return nullptr;
    }

    Vector& add_vector(const std::string& type, Vector vec = {}) {
        vec.type = type;
        vectors.push_back(vec);
        return vectors.back();
    }

    bool erase_vector(const std::string& type, std::size_t type_index) {
        std::size_t seen = 0;
        for (auto it = vectors.begin(); it != vectors.end(); ++it) {
            if (it->type != type) continue;
            if (seen == type_index) {
                vectors.erase(it);
                return true;
            }
            ++seen;
        }
        return false;
    }
};

}
