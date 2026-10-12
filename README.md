# time_set

**Set arithmetic for time.** Combine availability, detect conflicts, repeat
patterns, copy or move timeline sectors, and find every place an activity fits.

One C++ engine, three interfaces: a standalone C++20 header, a command-line
program, and a native Python module. No Boost, no scheduling solver, no database.

## Build and check everything

On Ubuntu, the prerequisites are a C++20 compiler, CMake, Python 3.10 or newer,
and the matching Python development headers. To install missing prerequisites:

```bash
sudo apt install build-essential cmake python3-dev python3-venv
```

From this directory:

```bash
./check.sh
./build/time_set --help
PYTHONPATH=build/python python3 examples/availability.py
```

`check.sh` builds all three interfaces and runs the tests and examples. It does
not install anything, download dependencies, or alter another project.
`./check.sh --cpp-only` does not require Python development headers.
`./check.sh --sanitize` checks the C++ code and executable with sanitizers.

### Install the Python package and its CLI

A local virtual environment keeps the installation separate from system Python:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install .
.venv/bin/time_set --help
.venv/bin/python examples/precision.py
```

The package has no runtime dependencies outside the Python standard library.
Its native extension requires a C++20 compiler and Python headers when built.
The pip build uses setuptools; its normal build isolation can fetch build tools.
`python -m time_set` and the installed `time_set` command run the **same C++ CLI**
as `build/time_set`, not a Python reimplementation.

### Use the C++ library

For set mathematics alone, include `include/time_set.hpp`; there is nothing to link.
Date/text/JSON helpers are declared in `time_set_io.hpp` and implemented in
`src/time_set_io.cpp`. The optional `time_event.hpp` holds event metadata.

CMake consumers can use `add_subdirectory(...)` and link `time_set::core` or
`time_set::io`. To install the executable, headers, and I/O library locally:

```bash
cmake --install build --prefix "$HOME/.local"
```

Installed CMake consumers can use `find_package(time_set CONFIG REQUIRED)`.

## C++: an entire vocabulary of useful operations

<!-- verify: cpp -->
```cpp
#include "time_set.hpp"
#include <cassert>
#include <iostream>

int main() {
    ts::timeline line{100};
    auto a = line.range(10, 20);                     // [10,20)
    auto b = line.range(15, 25, ts::bounds::closed);  // [15,25]

    auto overlap = a & b;             // [15,20)
    auto either = a | b;              // [10,26)
    auto free = ~either;              // [0,10) | [26,100)
    auto later = a + ts::ticks{5};     // [15,25)
    auto starts = a.valid_starts(ts::ticks{4}); // [10,17)

    assert(overlap == line.range(15,20));
    assert(free == (line.range(0,10) | line.range(26,100)));
    assert(later == line.range(15,25));
    assert(starts == line.range(10,17));
    assert(a <= either);
    std::cout << starts << '\n';
}
```

`| & - ^ ~` are union, intersection, difference, symmetric difference, and
complement. `+ duration` shifts later; `- duration` shifts earlier. `>> n` moves
later by ticks; `<< n` moves earlier. Assignment forms modify the set.
`<= < >= >` mean subset/proper subset/superset/proper superset, not chronology.
Named methods are available too. Integer `std::chrono` durations are accepted:
`available.valid_starts(90min)` and `busy + 15min`.

## Python: use the same engine directly

<!-- verify: python -->
```python
from time_set import Timeline

day = Timeline(length="1d", tick="1min", origin="2026-10-12")
office = day.parse("[9h,17h)")
alice_busy = day.parse("[9h,10h) | [12h,13h) | [15h,16h)")
bob_busy = day.parse("[10h,11h) | [16h,17h)")

shared_free = office - (alice_busy | bob_busy)
starts = shared_free.valid_starts("90min")

assert shared_free == day.parse("[11h,12h) | [13h,15h)")
assert starts == day.range(780,811)
print("A 90-minute meeting can start at any minute from 13:00 through 13:30.")
```

The final fitting start is included. Two separate free hours cannot hold one
90-minute activity, even though their total duration exceeds 90 minutes.

Python accepts integer ticks, `ticks(n)`, exact unit strings, `Fraction` seconds,
and `timedelta` durations. Positions also accept naive `date`/`datetime` objects.
Floats and timezone-aware timestamps are rejected. Python iteration returns
interval pairs; `tick_count` and `interval_count` deliberately replace an
ambiguous `len()`. Operators return new sets; in-place operators and edits mutate.

## CLI: JSON for storage, text for your eyes

There are exactly two output representations:

**JSON** is the default. It carries the timeline and intervals, preserves integer
positions, and can be saved or piped straight into another command.

**Text** is a normalized interval expression, such as `[10,20) | [40,45)`.
Use `show` or `--text`; `--dates` writes exact civil timestamps instead of ticks.
Text lacks timeline metadata, so input files are JSON. Text can be turned back
into a set with `make` plus an explicit timeline definition.

There is no `--format` option, no implicit rounding, and no hidden file rewriting.
Use `./build/time_set` below when the executable is not on your PATH.

<!-- verify: bash -->
```bash
time_set make --length 100 '[10,20)' \
  | time_set shift --by 5t \
  | time_set starts --duration 4t --text
```

```text
[15,22)
```

`INPUT` is a JSON file or `-` for standard input; filters default to stdin.
Run `time_set --help` for commands, or `time_set move --help` for operation details.
Quote intervals in the shell. Outputs go to stdout; errors go to stderr with
exit status 2. A successful boolean query always exits 0, even when it prints
`false`.

Do not redirect a result onto a file the command is also reading: the shell
truncates the file first. Write a new file, inspect it, then replace the old one.

## Copy and move: replacement or overlay

A sector's width is its selected width, **including holes**. For source sector
`[10,20)`, source content `[10,15)`, and existing destination content `[47,50)`:

| Copy to tick 40 | Content in destination sector `[40,50)` |
|---|---|
| Replacement, the default | `[40,45)` |
| Overlay | `[40,45) | [47,50)` |

Copying a completely empty sector clears the destination in replacement mode
and changes nothing in overlay mode. A move snapshots the original source,
clears that source sector, then pastes the snapshot. Overlapping moves therefore
have one defined result, independent of traversal direction.

C++: `set.copy_region(lo, hi, to, ts::paste_mode::overlay)`.
Python: `set.copy_region(lo, hi, to=40, mode="overlay")`.
CLI: `time_set copy file.json '[10,20)' --to 40 --overlay`.

Cross-set transfers use `destination.copy_from(source, ...)` / `move_from(...)`.
The CLI's `--into other.json` does the same. Cross-set move returns a JSON object
containing both `source` and `destination`; `select` extracts either one.
Neither input file is modified.

## Timelines and precision

A timeline is a length, a positive exact tick duration, and an optional origin.
Equal definitions are compatible even when constructed separately. Mismatched
origins, tick sizes, or lengths are errors for set operations. Equality returns
false for mismatched definitions. No automatic resampling takes place.

The default is **4,294,967,296 one-second ticks from January 1, 2000**.
Custom timelines use signed 64-bit positions, with `0 <= position < length` and
an exclusive endpoint at `length`. Zero-length timelines are valid.

All four input boundary styles work: `[A,B)`, `[A,B]`, `(A,B)`, `(A,B]`.
Closing an endpoint includes **one tick**, not necessarily one second. Internally,
intervals are always sorted, merged, nonempty, half-open ranges. `{A}` is one tick.

Tick sizes are exact rational seconds. `1ns`, `250ms`, `1min`, and `1/30s` work;
smaller or unusual resolutions can use fractions. Units are `ns us ms s min h d w`.
Dates are simple Gregorian civil labels, with no timezone conversion, daylight
saving, or leap seconds. Use `origin=None` in Python, `timeline::indexed(...)` in
C++, or `--origin none` in the CLI for a non-calendar axis.

For limits, exact fractional timestamp notation, JSON schemas, and a complete
reference, see [docs/API.md](docs/API.md). Design and failure semantics are in
[docs/DESIGN.md](docs/DESIGN.md).

## Examples and verification

The examples are small programs with checked answers, not screenshots or sketches:

| Example | Demonstrates |
|---|---|
| `examples/algebra.cpp` | All basic operators, with only the core header |
| `examples/availability.cpp` / `.py` | Shared availability and uninterrupted activity fitting |
| `examples/sectors.cpp` / `.py` | Replacement, overlay, holes, overlapping and cross-set moves |
| `examples/precision.cpp` / `.py` | Frames, nanoseconds, rational ticks, and off-grid rejection |
| `examples/routines.py` | Repeating routines, weekdays, exceptions, and optional events |
| `examples/pipeline.sh` | JSON pipes and a cross-set move without overwriting inputs |

`ctest` runs all example programs and the marked README examples. Tests also
compare interval operations to independent bit-by-bit reference models, enumerate
all pairs of sets on small timelines, test copy/move combinations and arithmetic
boundaries, exercise invalid JSON, and run the same CLI tests through both entry
points. See [docs/VERIFICATION.md](docs/VERIFICATION.md) for the delivered checks.

MIT License. Copyright (c) 2026 Jim Lewis.
