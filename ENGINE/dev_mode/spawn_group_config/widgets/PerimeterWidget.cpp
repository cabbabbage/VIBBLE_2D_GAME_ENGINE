#include "PerimeterWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

void PerimeterWidget::bind(model::SpawnGroup& group) {
    group_ = &group;
    ensure_config();
}

void PerimeterWidget::sync_from_model() {
    ensure_config();
}

void PerimeterWidget::clear_method_state() {
    group_ = nullptr;
}

int PerimeterWidget::min_number() const {
    if (const auto* config = read_config()) {
        return config->min_number;
    }
    return 0;
}

int PerimeterWidget::max_number() const {
    if (const auto* config = read_config()) {
        return config->max_number;
    }
    return 0;
}

void PerimeterWidget::set_min_number(int value) {
    if (!group_) {
        return;
    }
    auto& config = ensure_config();
    bool changed = false;
    if (config.min_number != value) {
        config.min_number = value;
        changed = true;
    }
    if (config.max_number < config.min_number) {
        config.max_number = config.min_number;
        changed = true;
    }
    if (changed) {
        on_changed_.emit();
    }
}

void PerimeterWidget::set_max_number(int value) {
    if (!group_) {
        return;
    }
    auto& config = ensure_config();
    bool changed = false;
    if (config.max_number != value) {
        config.max_number = value;
        changed = true;
    }
    if (config.max_number < config.min_number) {
        config.max_number = config.min_number;
        changed = true;
    }
    if (changed) {
        on_changed_.emit();
    }
}

model::MethodConfig::Perimeter& PerimeterWidget::ensure_config() {
    if (!group_) {
        static model::MethodConfig::Perimeter dummy{};
        return dummy;
    }
    auto* config = group_->method_config.as_perimeter();
    if (!config) {
        group_->method_config = model::MethodConfig::make_perimeter();
        config = group_->method_config.as_perimeter();
        on_changed_.emit();
    }
    return *config;
}

const model::MethodConfig::Perimeter* PerimeterWidget::read_config() const {
    if (!group_) {
        return nullptr;
    }
    return group_->method_config.as_perimeter();
}

}
