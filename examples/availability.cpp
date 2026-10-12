#include "time_set_io.hpp"
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;

int main() {
    const auto day = ts::make_timeline("1d","1min","2026-10-12");
    const auto office = ts::parse_text(day,"[9h,17h)");
    const auto alice = ts::parse_text(day,"[9h,10h) | [12h,13h) | [15h,16h)");
    const auto bob = ts::parse_text(day,"[10h,11h) | [16h,17h)");
    const auto free = office - (alice | bob);
    const auto starts = free.valid_starts(90min);
    if (free != ts::parse_text(day,"[11h,12h) | [13h,15h)") ||
        starts != day.range(780,811)) throw std::runtime_error("availability example failed");
    std::cout << "Shared free time:\n" << ts::to_text(free,true) << '\n'
              << "90-minute meeting: start at any minute from 13:00 through 13:30, inclusive.\n";
    const auto fragmented = ts::parse_text(day,"[9h,10h) | [11h,12h)");
    if (fragmented.tick_count() != 120 || fragmented.valid_starts(90min))
        throw std::runtime_error("fragmentation example failed");
    std::cout << "Two separate free hours give 120 minutes total, but no 90-minute opening.\n";
}
