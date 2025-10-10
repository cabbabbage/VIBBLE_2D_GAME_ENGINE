#pragma once

#include "dev_mode/spawn_group_config/widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

class ExactWidget : public ISpawnMethodWidget {
public:
    ExactWidget() = default;

    void bind(model::SpawnGroup& group) override;
    void sync_from_model() override;
    void clear_method_state() override;

    int quantity() const;
    void set_quantity(int value);

private:
    model::MethodConfig::Exact& ensure_config();
    const model::MethodConfig::Exact* read_config() const;

    model::SpawnGroup* group_ = nullptr;
};

}
