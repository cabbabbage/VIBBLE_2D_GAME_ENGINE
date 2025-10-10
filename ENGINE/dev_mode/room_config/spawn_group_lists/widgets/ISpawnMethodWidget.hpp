#pragma once

#include "dev_mode/room_config/spawn_group_lists/Signal.hpp"
#include "dev_mode/room_config/spawn_group_model.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::widgets {

class ISpawnMethodWidget {
public:
    using OnChangedSignal = Signal<>;

    virtual ~ISpawnMethodWidget() = default;

    virtual void bind(model::SpawnGroup& group) = 0;
    virtual void sync_from_model() = 0;
    virtual void clear_method_state() = 0;

    OnChangedSignal& on_changed() { return on_changed_; }

protected:
    OnChangedSignal on_changed_{};
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::widgets
