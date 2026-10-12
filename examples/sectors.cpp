#include "time_set.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    const ts::timeline line{100};
    const auto original = line.range(10,15) | line.range(47,50);
    auto replacement = original, overlay = original;
    replacement.copy_region(10,20,40);
    overlay.copy_region(10,20,40,ts::paste_mode::overlay);
    if (replacement != (line.range(10,15) | line.range(40,45)))
        throw std::runtime_error("replacement example failed");
    if (overlay != (replacement | line.range(47,50)))
        throw std::runtime_error("overlay example failed");
    std::cout << "Original:    " << original << '\n'
              << "Replacement: " << replacement << '\n'
              << "Overlay:     " << overlay << '\n';

    auto overlapping = line.range(10,13) | line.range(15,18) | line.range(24,27);
    overlapping.move_region(10,20,15);
    if (overlapping != (line.range(15,18) | line.range(20,23) | line.range(25,27)))
        throw std::runtime_error("overlapping move example failed");
    std::cout << "Overlapping move, snapshot first: " << overlapping << '\n';

    auto destination = line.range(40,50);
    destination.copy_from(line.empty(),0,10,40);
    if (destination) throw std::runtime_error("empty-sector copy example failed");
    std::cout << "Copying ten empty ticks over a full sector clears it: " << destination << '\n';
}
