class SharedRandom {
    uint64_t seed;
    std::mt19937_64 rng;

    public:
        SharedRandom();
        SharedRandom(uint64_t seed);

        int randRange(int min, int max);
        float randFloat(float min, float max);
        bool coinFlip();
        std::vector<int> choice(const std::vector& vec);
};

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