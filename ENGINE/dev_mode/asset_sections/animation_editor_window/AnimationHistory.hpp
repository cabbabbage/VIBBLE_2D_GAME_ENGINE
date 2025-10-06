#pragma once

#include <functional>
#include <string>
#include <vector>

namespace animation_editor {

class AnimationHistory {
  public:
    using SnapshotProducer = std::function<std::string()>;
    using SnapshotConsumer = std::function<void(const std::string&)>;

    AnimationHistory();

    void set_snapshot_producer(SnapshotProducer producer);
    void set_snapshot_consumer(SnapshotConsumer consumer);

    void clear();
    void push_state();
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

  private:
    // TODO: Store snapshot buffers and current cursor index.
};

}  // namespace animation_editor

