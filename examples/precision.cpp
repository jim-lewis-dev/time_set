#include "time_set_io.hpp"
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;

int main() {
    const ts::timeline frames{300,ts::seconds{1,30}};
    const auto clip = frames.range(0,30);
    if ((clip + 100ms) != frames.range(3,33)) throw std::runtime_error("frame example failed");
    std::cout << "One second at 30 frames/second: " << clip << '\n'
              << "Delayed by exactly 100ms:      " << clip + 100ms << '\n'
              << "Timestamp of frame one:       " << ts::format_position(frames,1) << '\n';
    bool rejected = false;
    try { (void)(clip + 50ms); } catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) throw std::runtime_error("off-grid duration was silently rounded");
    std::cout << "50ms is 1.5 frames and is rejected, not rounded.\n";

    const auto nano = ts::make_timeline("1us","1ns","2026-10-12T00:00:00");
    const auto pulse = nano.range(100,110);
    if (pulse.valid_starts(8ns) != nano.range(100,103)) throw std::runtime_error("nanosecond example failed");
    std::cout << "A ten-nanosecond gate: " << ts::to_text(pulse,true) << '\n'
              << "Eight-nanosecond valid starts: " << pulse.valid_starts(8ns) << '\n';
}
