#include "asset_spawn_id.hpp"

#include <atomic>
#include <random>
#include <sstream>
#include <iomanip>

std::string AssetSpawnId::generate() {
    static std::atomic<uint64_t> counter{1};
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<uint64_t> dist(0, (1ull<<48) - 1);

    uint64_t c = counter.fetch_add(1, std::memory_order_relaxed);
    uint64_t r = dist(rng);

    std::ostringstream oss;
    oss << "asid-" << c << "-" << std::hex << std::setw(12) << std::setfill('0') << r;
    return oss.str();
}

