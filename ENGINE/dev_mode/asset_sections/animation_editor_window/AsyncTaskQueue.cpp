#include "AsyncTaskQueue.hpp"

#include <SDL_log.h>

#include <chrono>
#include <utility>

namespace animation_editor {

namespace {

void wait_and_log(std::future<void>& task) {
    if (!task.valid()) return;
    try {
        task.get();
    } catch (const std::exception& ex) {
        SDL_Log("AsyncTaskQueue: task failed with exception: %s", ex.what());
    } catch (...) {
        SDL_Log("AsyncTaskQueue: task failed with unknown exception");
    }
}

}

AsyncTaskQueue::AsyncTaskQueue() = default;

AsyncTaskQueue::~AsyncTaskQueue() {
    std::vector<std::future<void>> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(tasks_);
    }
    for (auto& task : tasks) {
        wait_and_log(task);
    }
}

void AsyncTaskQueue::enqueue(std::function<void()> task) {
    if (!task) return;
    std::future<void> future = std::async(std::launch::async, [task = std::move(task)]() mutable {
        try {
            task();
        } catch (const std::exception& ex) {
            SDL_Log("AsyncTaskQueue: unhandled exception in task: %s", ex.what());
            throw;
        }
    });

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(future));
}

void AsyncTaskQueue::prune_completed_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.begin();
    while (it != tasks_.end()) {
        if (!it->valid()) {
            it = tasks_.erase(it);
            continue;
        }
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            wait_and_log(*it);
            it = tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

void AsyncTaskQueue::update() { prune_completed_tasks(); }

bool AsyncTaskQueue::is_busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& task : tasks_) {
        if (!task.valid()) continue;
        if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return true;
        }
    }
    return false;
}

}

