#pragma once

#include <functional>

namespace animation_editor {

class AsyncTaskQueue {
  public:
    AsyncTaskQueue();

    void enqueue(std::function<void()> task);
    void update();
    bool is_busy() const;

  private:
    // TODO: Hold worker threads and completion callbacks for background tasks.
};

}  // namespace animation_editor

