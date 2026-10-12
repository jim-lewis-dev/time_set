// Copyright (c) 2026 Jim Lewis. MIT License.
#pragma once
#include "time_set.hpp"
#include "time_event.hpp"
#include <string>
#include <string_view>

namespace ts {
// Unit strings are exact: 1ns, 250ms, 90min, 1/30s. Never floating point.
seconds parse_duration(std::string_view text);
std::string format_duration(seconds value);
instant parse_timestamp(std::string_view text);
std::string format_timestamp(const instant& value);
// Length accepts a raw tick count, a count with 't', or an exact duration.
timeline make_timeline(std::string_view length, std::string_view tick = "1s",
                       std::string_view origin = "2000-01-01T00:00:00");
// Position accepts a raw tick count or civil timestamp; durations accept units.
tick_type parse_ticks(const timeline& line, std::string_view text);
tick_type parse_position(const timeline& line, std::string_view text);
std::string format_position(const timeline& line, tick_type position);
bounds parse_bounds(std::string_view text);
// Region normalizes brackets, including empty sectors, without dropping width.
interval parse_region(const timeline& line, std::string_view text);
time_set parse_text(const timeline& line, std::string_view text);
std::string to_text(const time_set& set, bool dates = false);
std::string to_json(const time_set& set);
time_set from_json(std::string_view text);
std::string to_json(const event& value);
event event_from_json(std::string_view text);
// Used by CLI cross-set moves; both results are returned without overwriting files.
std::string move_to_json(const time_set& source, const time_set& destination);
time_set select_from_json(std::string_view text, std::string_view field);
std::string statistics_json(const time_set& set);
} // namespace ts
