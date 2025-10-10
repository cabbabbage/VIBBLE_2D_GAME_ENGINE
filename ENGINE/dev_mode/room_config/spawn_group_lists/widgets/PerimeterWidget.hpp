#pragma once

#include "dev_mode/room_config/spawn_group_lists/widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::widgets {

class PerimeterWidget : public ISpawnMethodWidget {
public:
    PerimeterWidget() = default;

    void bind(model::SpawnGroup& group) override;
    void sync_from_model() override;
    void clear_method_state() override;

    int min_number() const;
    int max_number() const;
    void set_min_number(int value);
    void set_max_number(int value);

private:
    model::MethodConfig::Perimeter& ensure_config();
    const model::MethodConfig::Perimeter* read_config() const;

    model::SpawnGroup* group_ = nullptr;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::widgets
