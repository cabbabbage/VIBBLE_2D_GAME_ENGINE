#include "dev_mode/core/dev_json_store.hpp"

#include <SDL_log.h>

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdio>
#ifdef _WIN32
#  include <share.h>
#  include <io.h>
#  include <fcntl.h>
#  include <windows.h>
#endif

namespace devmode::core {
namespace {
constexpr std::chrono::milliseconds kDefaultDebounce{400};

struct PathHash {
    std::size_t operator()(const std::filesystem::path& p) const noexcept {
        return std::filesystem::hash_value(p);
    }
};

struct DigestEntry {
    std::filesystem::file_time_type mtime{};
    std::size_t hash{};
    nlohmann::json data = nlohmann::json::object();
    bool valid = false;
};

struct PendingWrite {
    std::filesystem::path path;
    nlohmann::json data;
    std::string serialized;
    std::size_t hash{};
    std::chrono::steady_clock::time_point deadline;
    std::size_t coalesce_count = 0;
};

void log_error(const std::string& message) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
}

void log_info(const std::string& message) {
    SDL_Log("%s", message.c_str());
}

bool write_file(const std::filesystem::path& path,
                const std::string& payload,
                std::ostream& error_sink) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error_sink << "[DevJsonStore] Failed to create parent directory for '" << path.string() << "': " << ec.message() << "\n";
            return false;
        }
    }

    const std::filesystem::path tmp_path = path;
    const std::filesystem::path tmp_full = tmp_path.string() + ".tmp";

    std::filesystem::perms target_perms = std::filesystem::perms::unknown;
    const bool target_exists = std::filesystem::exists(path, ec);
    if (!ec && target_exists) {
        target_perms = std::filesystem::status(path, ec).permissions();
        ec.clear();
    } else {
        ec.clear();
    }

#ifdef _WIN32

    std::wstring wtmp = tmp_full.wstring();
    FILE* fp = _wfsopen(wtmp.c_str(), L"wb", _SH_DENYRW);
    if (!fp) {
        error_sink << "[DevJsonStore] Failed to open temp file '" << tmp_full.string() << "' for writing\n";
        return false;
    }
    const size_t written = fwrite(payload.data(), 1, payload.size(), fp);
    if (written != payload.size()) {
        error_sink << "[DevJsonStore] Short write to temp file '" << tmp_full.string() << "'\n";
        fclose(fp);
        std::filesystem::remove(tmp_full, ec);
        return false;
    }
    fflush(fp);

    _commit(_fileno(fp));
    fclose(fp);

    if (target_perms != std::filesystem::perms::unknown) {
        std::filesystem::permissions(tmp_full, target_perms, ec);
        ec.clear();
    }

    std::wstring wdst = path.wstring();
    if (!MoveFileExW(wtmp.c_str(), wdst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        error_sink << "[DevJsonStore] MoveFileExW failed replacing '" << path.string() << "' with temp: error " << err << "\n";

        std::filesystem::remove(tmp_full, ec);
        return false;
    }

    return true;
#else

    {
        std::ofstream out(tmp_full, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error_sink << "[DevJsonStore] Failed to open temp file '" << tmp_full << "' for writing\n";
            return false;
        }
        out << payload;
        out.flush();
        if (!out.good()) {
            error_sink << "[DevJsonStore] Stream error while writing temp '" << tmp_full << "'\n";
            return false;
        }
    }

    if (target_perms != std::filesystem::perms::unknown) {
        std::filesystem::permissions(tmp_full, target_perms, ec);
        ec.clear();
    }

    std::filesystem::rename(tmp_full, path, ec);
    if (ec) {
        error_sink << "[DevJsonStore] rename('" << tmp_full.string() << "' -> '" << path.string() << "') failed: " << ec.message() << "\n";

        std::filesystem::remove(tmp_full, ec);
        return false;
    }
    return true;
#endif
}

}

struct DevJsonStore::Impl {
    Impl()
#ifndef DEV_MODE_DISABLE_JSON_DEBOUNCE
        : worker_([this]() { this->worker_loop(); })
#endif
    {}

    ~Impl() {
        shutdown();
    }

    nlohmann::json load(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            if (ec) {
                log_error("[DevJsonStore] exists(" + path.string() + ") failed: " + ec.message());
            }
            return nlohmann::json::object();
        }

        auto file_time = std::filesystem::last_write_time(path, ec);
        if (ec) {
            log_error("[DevJsonStore] last_write_time(" + path.string() + ") failed: " + ec.message());
            return nlohmann::json::object();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = digest_cache_.find(path);
            if (it != digest_cache_.end() && it->second.valid && it->second.mtime == file_time) {
                return it->second.data;
            }
        }

        std::ifstream in(path);
        if (!in.is_open()) {
            log_error("[DevJsonStore] Failed to open '" + path.string() + "' for reading");
            return nlohmann::json::object();
        }
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::size_t hash = std::hash<std::string>{}(contents);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = digest_cache_.find(path);
            if (it != digest_cache_.end() && it->second.valid && it->second.mtime == file_time && it->second.hash == hash) {
                return it->second.data;
            }
        }

        nlohmann::json parsed = nlohmann::json::object();
        try {
            parsed = nlohmann::json::parse(contents);
            if (!parsed.is_object()) {
                parsed = nlohmann::json::object();
            }
        } catch (...) {
            parsed = nlohmann::json::object();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            digest_cache_[path] = DigestEntry{file_time, hash, parsed, true};
        }
        return parsed;
    }

    void submit(const std::filesystem::path& path,
                const nlohmann::json& data,
                int indent) {
#ifdef DEV_MODE_DISABLE_JSON_DEBOUNCE
        std::ostringstream errors;
        std::string payload = data.dump(indent);
        if (!write_file(path, payload, errors)) {
            log_error(errors.str());
            return;
        }
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(path, ec);
        if (ec) {
            mtime = std::filesystem::file_time_type::clock::now();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            digest_cache_[path] = DigestEntry{mtime, std::hash<std::string>{}(payload), data, true};
        }
        log_info("[DevJsonStore] Wrote '" + path.string() + "' (synchronous)");
#else
        PendingWrite pending;
        pending.path = path;
        pending.data = data;
        pending.serialized = data.dump(indent);
        pending.hash = std::hash<std::string>{}(pending.serialized);
        pending.deadline = std::chrono::steady_clock::now() + kDefaultDebounce;
        pending.coalesce_count = 1;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = pending_writes_.find(path);
            if (it != pending_writes_.end()) {
                it->second.data = pending.data;
                it->second.serialized = pending.serialized;
                it->second.hash = pending.hash;
                it->second.deadline = pending.deadline;
                it->second.coalesce_count += 1;
            } else {
                pending_writes_.emplace(path, std::move(pending));
            }
        }
        cv_.notify_one();
#endif
    }

    void flush_all() {
#ifdef DEV_MODE_DISABLE_JSON_DEBOUNCE

        return;
#else
        std::vector<PendingWrite> ready;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [path, pending] : pending_writes_) {
                ready.push_back(std::move(pending));
            }
            pending_writes_.clear();
        }
        flush_ready(std::move(ready));
#endif
    }

    void shutdown() {
#ifdef DEV_MODE_DISABLE_JSON_DEBOUNCE
        return;
#else
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) {
                return;
            }
            stopped_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
#endif
    }

#ifndef DEV_MODE_DISABLE_JSON_DEBOUNCE
    void worker_loop() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            if (stopped_) {
                break;
            }

            if (pending_writes_.empty()) {
                cv_.wait(lock, [this]() { return stopped_ || !pending_writes_.empty(); });
                continue;
            }

            auto next_deadline = std::chrono::steady_clock::time_point::max();
            auto now = std::chrono::steady_clock::now();
            for (const auto& [path, pending] : pending_writes_) {
                if (pending.deadline < next_deadline) {
                    next_deadline = pending.deadline;
                }
            }

            if (cv_.wait_until(lock, next_deadline, [this]() { return stopped_ || pending_writes_.empty(); })) {
                continue;
            }

            now = std::chrono::steady_clock::now();
            std::vector<PendingWrite> ready;
            for (auto it = pending_writes_.begin(); it != pending_writes_.end();) {
                if (it->second.deadline <= now) {
                    ready.push_back(std::move(it->second));
                    it = pending_writes_.erase(it);
                } else {
                    ++it;
                }
            }

            if (ready.empty()) {
                continue;
            }

            lock.unlock();
            flush_ready(std::move(ready));
            lock.lock();
        }

        std::vector<PendingWrite> remaining;
        for (auto& [path, pending] : pending_writes_) {
            remaining.push_back(std::move(pending));
        }
        pending_writes_.clear();
        lock.unlock();
        flush_ready(std::move(remaining));
    }

    void flush_ready(std::vector<PendingWrite>&& writes) {
        if (writes.empty()) {
            return;
        }

        struct Result {
            std::filesystem::path path;
            nlohmann::json data;
            std::size_t hash{};
            std::filesystem::file_time_type mtime{};
            std::size_t coalesce_count{};
            bool success = false;
};

        std::vector<Result> results;
        results.reserve(writes.size());

        std::ostringstream error_buffer;
        std::vector<std::pair<std::string, std::size_t>> flushed_paths;
        for (auto& pending : writes) {
            Result result;
            result.path = pending.path;
            result.data = pending.data;
            result.hash = pending.hash;
            result.coalesce_count = pending.coalesce_count;

            std::ostringstream local_errors;
            if (write_file(pending.path, pending.serialized, local_errors)) {
                std::error_code ec;
                result.mtime = std::filesystem::last_write_time(pending.path, ec);
                if (ec) {
                    result.mtime = std::filesystem::file_time_type::clock::now();
                    error_buffer << "[DevJsonStore] last_write_time('" << pending.path.string() << "') failed after write: " << ec.message() << "\n";
                }
                result.success = true;
                flushed_paths.emplace_back(pending.path.string(), pending.coalesce_count);
            } else {
                error_buffer << local_errors.str();
            }

            results.push_back(std::move(result));
        }

        if (!flushed_paths.empty()) {
            std::string message = "[DevJsonStore] Flushed " + std::to_string(flushed_paths.size()) + " JSON file(s): ";
            for (size_t i = 0; i < flushed_paths.size(); ++i) {
                message += flushed_paths[i].first;
                message += " (coalesced: ";
                message += std::to_string(flushed_paths[i].second);
                message += ')';
                if (i + 1 < flushed_paths.size()) {
                    message += ", ";
                }
            }
            log_info(message);
        }

        if (!error_buffer.str().empty()) {
            log_error(error_buffer.str());
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& result : results) {
                if (!result.success) {
                    continue;
                }
                digest_cache_[result.path] = DigestEntry{result.mtime, result.hash, result.data, true};
            }
        }
    }
#endif

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::filesystem::path, DigestEntry, PathHash> digest_cache_;
#ifndef DEV_MODE_DISABLE_JSON_DEBOUNCE
    std::unordered_map<std::filesystem::path, PendingWrite, PathHash> pending_writes_;
    std::thread worker_;
    bool stopped_ = false;
#endif
};

DevJsonStore& DevJsonStore::instance() {
    static DevJsonStore store;
    return store;
}

DevJsonStore::DevJsonStore()
    : impl_(new Impl()) {}

DevJsonStore::~DevJsonStore() {
    delete impl_;
}

nlohmann::json DevJsonStore::load(const std::filesystem::path& path) {
    return impl_->load(path);
}

void DevJsonStore::submit(const std::filesystem::path& path,
                          const nlohmann::json& data,
                          int indent) {
    impl_->submit(path, data, indent);
}

void DevJsonStore::flush_all() {
    impl_->flush_all();
}

void DevJsonStore::shutdown() {
    impl_->shutdown();
}

}
