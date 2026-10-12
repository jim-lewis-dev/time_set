// Copyright (c) 2026 Jim Lewis. MIT License.
#include "cli.hpp"
#include "time_set_io.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace ts::cli {
namespace {
const char* help = R"(time_set — exact interval-set arithmetic

Create
  time_set make [--length N|DURATION] [--tick DURATION]
                [--origin DATE|none] [INTERVAL ...]
  time_set make --length 100 '[10,20)' '[30,40]' '{50}'

Combine
  time_set union|intersect|subtract|xor INPUT INPUT [INPUT ...]
  time_set complement [INPUT]
  time_set shift [INPUT] --by OFFSET

Edit
  time_set fill [INPUT] REGION
  time_set clear [INPUT] [REGION]
  time_set copy|move [INPUT] REGION --to POSITION [--overlay] [--clip]
                    [--into DESTINATION]

Repeat and fit
  time_set repeat [INPUT] --every DURATION (--count N | --until POSITION)
  time_set starts [INPUT] --duration DURATION

Inspect
  time_set show [INPUT] [--dates]
  time_set stats [INPUT] [--text]
  time_set contains [INPUT] POSITION
  time_set subset INPUT INPUT [--proper]
  time_set overlaps|equal INPUT INPUT
  time_set select [INPUT] source|destination

INPUT is a JSON file, or '-' for standard input. Filters default to stdin.
Set results are JSON by default; --text prints intervals, --dates prints dates.
show is shorthand for readable interval output. No --format option is needed.
Text does not carry timeline metadata; JSON does and can be piped back in.
Use raw integers or 't' for ticks: 5, 5t, -5t. Exact durations: 90min, 1ns,
1/30s. Units: ns us ms s min h d w. Date labels are civil time, not timezones.
Bounds: [A,B) [A,B] (A,B) (A,B], and {A}. Quote intervals in your shell.

Default timeline: 4294967296 one-second ticks from 2000-01-01T00:00:00.
make with no intervals creates an empty set; --full creates the whole timeline.
Copy/move replace the entire destination sector; --overlay preserves its gaps.
Cross-set copy returns the destination. Cross-set move returns a JSON object
with 'source' and 'destination'; select extracts either set. Inputs are never
rewritten. Shifts clip; copy/move reject out-of-range destinations unless --clip.

Examples
  time_set make --length 100 '[10,20)' | time_set starts --duration 4t --text
  time_set make --length 1d --tick 1min '[9h,17h)' | time_set show --dates
  time_set union alice.json bob.json | time_set complement > free.json

Exit status: 0 success (including a false query), 2 invalid input or I/O error.
)";
std::string command_help(const std::string& command) {
    if (command == "make") return
        "time_set make [--length N|DURATION] [--tick DURATION] [--origin DATE|none] [INTERVAL ...]\n"
        "  Numeric lengths are ticks. Dates can be YYYY-MM-DD or full civil timestamps.\n"
        "  --full fills the timeline instead of accepting intervals. Default is empty.\n"
        "  Example: time_set make --length 10s --tick 1ms '[1s,2s)' --text\n";
    if (command == "copy" || command == "move") return
        "time_set " + command + " [INPUT] REGION --to POSITION [--overlay] [--clip] [--into DESTINATION]\n"
        "  REGION may contain holes. Replace copies those holes; --overlay preserves destination content.\n"
        "  Source is snapshotted before editing. Move then clears the source before pasting.\n"
        "  --into transfers between compatible sets. Cross-set move returns both sets as a JSON bundle.\n"
        "  --clip explicitly permits loss at the timeline edges. Inputs are never overwritten.\n"
        "  Example: time_set " + command + " day.json '[10,20)' --to 40 --overlay --text\n";
    if (command == "repeat") return
        "time_set repeat [INPUT] --every DURATION (--count N | --until POSITION)\n"
        "  Count includes the original; zero produces an empty set. Period must be positive.\n"
        "  --until is an exclusive endpoint. Repetitions are fixed-duration shifts, not calendar rules.\n"
        "  Example: time_set repeat routine.json --every 1d --count 7\n";
    if (command == "starts") return
        "time_set starts [INPUT] --duration DURATION\n"
        "  Return every tick at which the positive duration fits without interruption.\n"
        "  The last fitting tick is included; an exact fit has one valid starting tick.\n"
        "  Example: time_set starts available.json --duration 90min --dates\n";
    return help;
}
struct arguments {
    std::vector<std::string> positional;
    std::map<std::string,std::string> options;
    std::set<std::string> flags;
    explicit arguments(const std::vector<std::string>& raw) {
        const std::set<std::string> boolean = {"text","dates","overlay","clip","full","proper"};
        const std::set<std::string> valued = {"length","tick","origin","by","to","into","every","count","until","duration"};
        bool literal = false;
        for (std::size_t i = 1; i < raw.size(); ++i) {
            const auto& item = raw[i];
            if (!literal && item == "--") { literal = true; continue; }
            if (!literal && item.starts_with("--")) {
                const auto equals = item.find('=');
                const auto name = item.substr(2, equals == item.npos ? item.npos : equals-2);
                if (boolean.contains(name)) {
                    if (equals != item.npos) throw std::invalid_argument("--" + name + " does not take a value");
                    if (!flags.insert(name).second) throw std::invalid_argument("duplicate option --" + name);
                } else if (valued.contains(name)) {
                    std::string value;
                    if (equals != item.npos) value = item.substr(equals+1);
                    else {
                        if (++i == raw.size() || raw[i].starts_with("--"))
                            throw std::invalid_argument("--" + name + " needs a value");
                        value = raw[i];
                    }
                    if (value.empty() || !options.emplace(name,std::move(value)).second)
                        throw std::invalid_argument("empty or duplicate option --" + name);
                } else throw std::invalid_argument("unknown option --" + name);
            } else positional.push_back(item);
        }
    }
    bool flag(const std::string& name) { return flags.erase(name) != 0; }
    std::optional<std::string> option(const std::string& name) {
        auto found = options.find(name);
        if (found == options.end()) return std::nullopt;
        auto value = found->second; options.erase(found); return value;
    }
    std::string required(const std::string& name) {
        auto value = option(name);
        if (!value) throw std::invalid_argument("missing --" + name);
        return *value;
    }
    void count(std::size_t minimum, std::size_t maximum) const {
        if (positional.size() < minimum || positional.size() > maximum)
            throw std::invalid_argument("wrong number of arguments; use the command's --help");
    }
    void done() const {
        if (!options.empty()) throw std::invalid_argument("option --" + options.begin()->first + " is not valid for this command");
        if (!flags.empty()) throw std::invalid_argument("option --" + *flags.begin() + " is not valid for this command");
    }
};
class inputs {
    bool stdin_used_ = false;
public:
    std::string read(const std::string& path) {
        if (path == "-") {
            if (stdin_used_) throw std::invalid_argument("standard input can only be read once");
            stdin_used_ = true;
            std::string text{std::istreambuf_iterator<char>{std::cin}, {}};
            if (std::cin.bad()) throw std::runtime_error("could not read standard input");
            return text;
        }
        std::ifstream file{path,std::ios::binary};
        if (!file) throw std::runtime_error("cannot open '" + path + "'");
        std::string text{std::istreambuf_iterator<char>{file}, {}};
        if (file.bad()) throw std::runtime_error("could not read '" + path + "'");
        return text;
    }
    time_set set(const std::string& path) { return from_json(read(path)); }
};
} // namespace
int run(const std::vector<std::string>& raw) {
    try {
        if (raw.empty() || raw[0] == "--help" || raw[0] == "-h" || raw[0] == "help") {
            std::cout << (raw.size() == 2 && raw[0] == "help" ? command_help(raw[1]) : help);
            return 0;
        }
        const auto& command = raw[0];
        // Honor the literal-argument delimiter, even for a file named --help.
        for (std::size_t i = 1; i < raw.size() && raw[i] != "--"; ++i)
            if (raw[i] == "--help" || raw[i] == "-h") { std::cout << command_help(command); return 0; }
        arguments args{raw};
        const bool text_requested = args.flag("text");
        const bool dates = args.flag("dates");
        const bool text = text_requested || dates || command == "show";
        inputs files;
        auto output = [&](const time_set& set) { std::cout << (text ? to_text(set,dates) : to_json(set)) << '\n'; };
        auto unary = [&] { args.count(0,1); return files.set(args.positional.empty() ? "-" : args.positional[0]); };
        auto region_input = [&] {
            args.count(1,2);
            return std::pair{files.set(args.positional.size() == 1 ? "-" : args.positional[0]), args.positional.back()};
        };
        if (command == "make") {
            const auto length = args.option("length").value_or(std::to_string(default_length));
            const auto tick = args.option("tick").value_or("1s");
            const auto origin = args.option("origin").value_or("2000-01-01T00:00:00");
            const bool full = args.flag("full");
            args.done();
            if (full && !args.positional.empty()) throw std::invalid_argument("--full cannot be combined with intervals");
            const auto line = make_timeline(length,tick,origin);
            auto result = full ? line.full() : line.empty();
            for (const auto& value : args.positional) result |= parse_text(line,value);
            output(result);
        } else if (command == "union" || command == "intersect" || command == "subtract" || command == "xor") {
            args.count(2,std::numeric_limits<std::size_t>::max()); args.done();
            auto result = files.set(args.positional[0]);
            for (std::size_t i = 1; i < args.positional.size(); ++i) {
                const auto other = files.set(args.positional[i]);
                if (command == "union") result |= other;
                else if (command == "intersect") result &= other;
                else if (command == "subtract") result -= other;
                else result ^= other;
            }
            output(result);
        } else if (command == "show" || command == "complement" || command == "stats") {
            args.done(); const auto set = unary();
            if (command == "complement") output(~set);
            else if (command == "show") output(set);
            else if (text) {
                if (dates) throw std::invalid_argument("--dates does not apply to stats");
                std::cout << "covered: " << set.tick_count() << " ticks\nintervals: " << set.interval_count() << '\n';
                try { const auto duration = format_duration(set.covered_duration()); std::cout << "duration: " << duration << '\n'; }
                catch (const std::overflow_error&) { std::cout << "duration: exceeds rational 64-bit range; tick count remains exact\n"; }
            } else std::cout << statistics_json(set) << '\n';
        } else if (command == "shift" || command == "starts") {
            const auto value = args.required(command == "shift" ? "by" : "duration");
            args.done(); const auto set = unary();
            const auto duration = ticks{parse_ticks(set.domain(),value)};
            output(command == "shift" ? set.shifted(duration) : set.valid_starts(duration));
        } else if (command == "fill" || command == "clear") {
            args.done();
            args.count(0,2);
            const bool one_region = args.positional.size() == 1 && !args.positional[0].empty() &&
                (args.positional[0][0] == '[' || args.positional[0][0] == '(' || args.positional[0][0] == '{');
            if (command == "clear" && (args.positional.empty() || (args.positional.size() == 1 && !one_region))) {
                auto set = unary(); set.clear(); output(set);
            } else {
                auto [set, spec] = region_input();
                const auto sector = parse_region(set.domain(),spec);
                set.assign(sector.lo,sector.hi,command == "fill"); output(set);
            }
        } else if (command == "copy" || command == "move") {
            const auto destination_spec = args.required("to");
            const auto into = args.option("into");
            const auto mode = args.flag("overlay") ? paste_mode::overlay : paste_mode::replace;
            const bool clip = args.flag("clip"); args.done();
            auto [source, spec] = region_input();
            const auto sector = parse_region(source.domain(),spec);
            const auto to = parse_position(source.domain(),destination_spec);
            if (into) {
                auto destination = files.set(*into);
                if (command == "copy") {
                    destination.copy_from(source,sector.lo,sector.hi,to,mode,clip); output(destination);
                } else {
                    destination.move_from(source,sector.lo,sector.hi,to,mode,clip);
                    if (text) {
                        const auto description = "source: " + to_text(source,dates) + "\ndestination: " + to_text(destination,dates);
                        std::cout << description << '\n';
                    }
                    else std::cout << move_to_json(source,destination) << '\n';
                }
            } else {
                if (command == "copy") source.copy_region(sector.lo,sector.hi,to,mode,clip);
                else source.move_region(sector.lo,sector.hi,to,mode,clip);
                output(source);
            }
        } else if (command == "repeat") {
            const auto every = args.required("every");
            const auto count = args.option("count"), until = args.option("until");
            if (count.has_value() == until.has_value()) throw std::invalid_argument("provide exactly one of --count or --until");
            args.done(); const auto set = unary();
            const auto step = ticks{parse_ticks(set.domain(),every)};
            if (count) {
                // Count is deliberately not parsed as a duration.
                if (count->find_first_not_of("+-0123456789") != count->npos)
                    throw std::invalid_argument("repeat count must be an integer");
                output(set.repeated(step,parse_ticks(set.domain(),*count)));
            } else output(set.repeated_until(step,parse_position(set.domain(),*until)));
        } else if (command == "contains") {
            if (dates) throw std::invalid_argument("--dates does not apply to boolean queries");
            args.done(); auto [set, position] = region_input();
            std::cout << (set.contains(parse_position(set.domain(),position)) ? "true" : "false") << '\n';
        } else if (command == "subset" || command == "overlaps" || command == "equal") {
            if (dates) throw std::invalid_argument("--dates does not apply to boolean queries");
            const bool proper = command == "subset" && args.flag("proper");
            args.count(2,2); args.done();
            const auto a = files.set(args.positional[0]), b = files.set(args.positional[1]);
            const bool result = command == "equal" ? a == b : command == "overlaps" ? a.overlaps(b) : proper ? a < b : a <= b;
            std::cout << (result ? "true" : "false") << '\n';
        } else if (command == "select") {
            args.count(1,2); args.done();
            const auto encoded = files.read(args.positional.size() == 1 ? "-" : args.positional[0]);
            output(select_from_json(encoded,args.positional.back()));
        } else throw std::invalid_argument("unknown command '" + command + "'; use time_set --help");
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("could not write standard output");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "time_set: " << error.what() << '\n';
        return 2;
    }
}
} // namespace ts::cli
