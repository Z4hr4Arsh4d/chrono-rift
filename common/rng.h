#pragma once
#include "constants.h"
#include <cstdlib>


inline void rng_seed_once() {
    static bool seeded = false;
    if (!seeded) {
        std::srand((unsigned)COMBINED_RNG_SEED);
        seeded = true;
    }
}

inline int rand_range(int lo, int hi) {
    rng_seed_once();
    if (hi <= lo) return lo;
    return lo + (std::rand() % (hi - lo + 1));
}

inline bool rand_chance(double p) {
    rng_seed_once();
    return ((double)std::rand() / (double)RAND_MAX) < p;
}