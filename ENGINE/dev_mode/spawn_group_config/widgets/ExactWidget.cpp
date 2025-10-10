#include "ExactWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

void ExactWidget::bind(model::SpawnGroup& group) {
    group_ = &group;
    ensure_config();
}

void ExactWidget::sync_from_model() {
    ensure_config();
}

void ExactWidget::clear_method_state() {
    group_ = nullptr;
}

int ExactWidget::quantity() const {
    if (const auto* config = read_config()) {
        return config->quantity;
    }
    return 0;
}

void ExactWidget::set_quantity(int value) {
    if (!group_) {
        return;
    }
    auto& config = ensure_config();
    if (config.quantity != value) {
        config.quantity = value;
        on_changed_.emit();
    }
}

model::MethodConfig::Exact& ExactWidget::ensure_config() {
    if (!group_) {
        static model::MethodConfig::Exact dummy{};
        return dummy;
    }
    auto* config = group_->method_config.as_exact();
    if (!config) {
        group_->method_config = model::MethodConfig::make_exact();
        config = group_->method_config.as_exact();
        on_changed_.emit();
    }
    return *config;
}

const model::MethodConfig::Exact* ExactWidget::read_config() const {
    if (!group_) {
        return nullptr;
    }
    return group_->method_config.as_exact();
}

}
