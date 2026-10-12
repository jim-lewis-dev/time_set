// Copyright (c) 2026 Jim Lewis. MIT License.
#pragma once
#include "time_set.hpp"
#include <string>
namespace ts {
// Optional application metadata; the set engine does not know about events.
struct event {
    std::string name;
    std::string description;
    time_set times;
    friend bool operator==(const event&, const event&) = default;
};
}
