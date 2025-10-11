#pragma once

#include <functional>
#include <future>
#include <mutex>
#include <vector>

namespace animation_editor {

class AsyncTaskQueue {
  public:
    AsyncTaskQueue();
    ~AsyncTaskQueue();

    void enqueue(std::function<void()> task);
    void update();
    bool is_busy() const;

  private:
    void prune_completed_tasks() const;

  private:
    mutable std::mutex mutex_;
    mutable std::vector<std::future<void>> tasks_;
};

}

