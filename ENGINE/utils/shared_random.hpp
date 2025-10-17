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