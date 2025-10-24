#include "EdgeWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

void EdgeWidget::bind(model::SpawnGroup& group) {
    group_ = &group;
    ensure_config();
}

void EdgeWidget::sync_from_model() {
    ensure_config();
}

void EdgeWidget::clear_method_state() {
    group_ = nullptr;
}

int EdgeWidget::min_number() const {
    if (const auto* config = read_config()) {
        return config->min_number;
    }
    return 0;
}

int EdgeWidget::max_number() const {
    if (const auto* config = read_config()) {
        return config->max_number;
    }
    return 0;
}

int EdgeWidget::inset_percent() const {
    if (const auto* config = read_config()) {
        return config->inset_percent;
    }
    return 0;
}

void EdgeWidget::set_min_number(int value) {
    if (!group_) {
        return;
    }
    auto& config = ensure_config();
    bool changed = false;
    if (value < 1) {
        value = 1;
    }
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

void EdgeWidget::set_max_number(int value) {
    if (!group_) {
        return;
    }
    auto& config = ensure_config();
    bool changed = false;
    if (value < config.min_number) {
        value = config.min_number;
    }
    if (config.max_number != value) {
        config.max_number = value;
        changed = true;
    }
    if (changed) {
        on_changed_.emit();
    }
}

void EdgeWidget::set_inset_percent(int value) {
    if (!group_) {
        return;
    }
    auto& config = ensure_config();
    if (value < 0) value = 0;
    if (value > 200) value = 200;
    if (config.inset_percent != value) {
        config.inset_percent = value;
        on_changed_.emit();
    }
}

model::MethodConfig::Edge& EdgeWidget::ensure_config() {
    if (!group_) {
        static model::MethodConfig::Edge dummy{};
        return dummy;
    }
    auto* config = group_->method_config.as_edge();
    if (!config) {
        group_->method_config = model::MethodConfig::make_edge();
        config = group_->method_config.as_edge();
        on_changed_.emit();
    }
    return *config;
}

const model::MethodConfig::Edge* EdgeWidget::read_config() const {
    if (!group_) {
        return nullptr;
    }
    return group_->method_config.as_edge();
}

}

