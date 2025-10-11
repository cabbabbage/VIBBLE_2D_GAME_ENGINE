#pragma once

#include "dev_mode/spawn_group_config/widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

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

}
