#pragma once

#include "dev_mode/room_config/spawn_group_lists/widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::widgets {

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

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::widgets
