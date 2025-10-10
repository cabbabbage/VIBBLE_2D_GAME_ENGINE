#pragma once

#include "dev_mode/spawn_group_config/widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

class RandomWidget : public ISpawnMethodWidget {
public:
    RandomWidget() = default;

    void bind(model::SpawnGroup& group) override;
    void sync_from_model() override;
    void clear_method_state() override;

private:
    void ensure_random_config();

    model::SpawnGroup* group_ = nullptr;
};

}
