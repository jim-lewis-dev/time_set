// Build with just: c++ -std=c++20 -Iinclude examples/algebra.cpp -o algebra
#include "time_set.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    const ts::timeline line{60};
    const auto a = line.range(5,20);
    const auto b = line.range(15,30,ts::bounds::closed);
    const auto overlap = a & b;
    if (overlap != line.range(15,20) || (a | b) != line.range(5,31))
        throw std::runtime_error("algebra example failed");
    const auto shifted = a + ts::ticks{10};
    if (shifted != line.range(15,30) || !overlap.is_subset_of(a))
        throw std::runtime_error("shift/subset example failed");
    std::cout << "A:                 " << a << '\n'
              << "B, closed at 30:   " << b << '\n'
              << "Both:              " << overlap << '\n'
              << "Either:            " << (a | b) << '\n'
              << "Only A:            " << (a - b) << '\n'
              << "Exactly one:       " << (a ^ b) << '\n'
              << "Neither:           " << ~(a | b) << '\n'
              << "A, ten ticks later:" << ' ' << shifted << '\n';
}
