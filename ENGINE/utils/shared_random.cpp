#include "shared_random.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <random>
#include <cmath>

    SharedRandom::SharedRandom() 
        : SharedRandom(static_cast<uint64_t>(time(NULL))) {}
    SharedRandom::SharedRandom(uint64_t seed_) 
        : seed(seed_), rng(seed_) {}

    int SharedRandom::randRange(int min, int max) {
        std::uniform_int_distribution<> distribution(min, max);
        return distribution(rng);
    }

    float SharedRandom::randFloat(float min, float max) {
        std::uniform_real_distribution<float> distribution(min, max);
        return distribution(rng);
    }

    bool SharedRandom::coinFlip() {
        std::bernoulli_distribution distribution(0.5);
        return distribution(rng);
    }

    std::vector<int> SharedRandom::choice(const std::vector<int>& vec) {
        if(vec.empty()) return {};
        int i = randRange(0, vec.size() - 1);
        return {vec[i]};
    }

/**
 * Summary
Add a centralized random number generator so all randomness in the engine uses the same seed. 
This allows reproducibility when a seed is given and consistent behavior across systems.
Requirements
    New class SharedRandom in utils/.
    Initialize with optional uint64_t seed; if none, generate internally.
    Store and expose the seed.
    Provide helpers:
    int randRange(int min, int max)
    float randFloat(float min, float max)
    bool coinFlip()
    4 choice(const std::vector& vec)
Integration
    Replace all direct uses of std::random_device, std::mt19937, std::uniform_*_distribution.
    Apply to controllers, asset/animation frame picking, procedural generation, etc.
Testing
    With fixed seed → deterministic results.
    With no seed → results vary across runs.
    Log seed at startup.
 */