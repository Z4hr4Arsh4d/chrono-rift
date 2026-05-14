#include <cstdlib>

bool rand_chance(double p) {
    return (rand() / (double)RAND_MAX) < p;
}

int rand_range(int a, int b) {
    return a + rand() % (b - a + 1);
}