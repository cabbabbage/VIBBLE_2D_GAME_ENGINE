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