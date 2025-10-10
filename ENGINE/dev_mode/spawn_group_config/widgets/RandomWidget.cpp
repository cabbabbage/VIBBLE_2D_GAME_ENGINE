#include "RandomWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

void RandomWidget::bind(model::SpawnGroup& group) {
    group_ = &group;
    ensure_random_config();
}

void RandomWidget::sync_from_model() {
    ensure_random_config();
}

void RandomWidget::clear_method_state() {
    group_ = nullptr;
}

void RandomWidget::ensure_random_config() {
    if (!group_) {
        return;
    }
    if (!group_->method_config.as_random()) {
        group_->method_config = model::MethodConfig::make_random();
        on_changed_.emit();
    }
}

}
