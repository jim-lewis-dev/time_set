# Verification record

The delivered source was built and exercised in this Linux x86-64 environment:
GCC 14.2.0, Clang 17.0.0, Python 3.13.5, and CMake 3.31.6.
The project targets C++20 and Python 3.10 or newer; other Python versions and
non-Linux systems were not runtime-tested in this session.

## Results

| Check | Result |
|---|---|
| GCC build, strict warnings treated as errors | Passed |
| Clang build, strict warnings treated as errors | Passed |
| Full GCC CTest run | 15 of 15 passed |
| Full Clang CTest run | 15 of 15 passed |
| C++/CLI address + undefined-behavior sanitizers, leak detection enabled | 9 of 9 passed |
| Native Python extension under address + undefined-behavior sanitizers | All 21 Python test methods passed |
| Source package built and installed in a clean virtual environment | Passed |
| Python API tests against that installed package | All 21 methods passed |
| Installed Python console entry point | Passed |
| Separate CMake consumer using installed `time_set::io` | Compiled, linked, and ran |
| Standalone header-only C++ compilation | Passed |
| README examples compiled/executed verbatim | All 3 passed |

The native-extension sanitizer run disables interpreter-wide leak detection
because the host is an existing, unsanitized CPython runtime. Leak detection
**was enabled** for the standalone C++ and CLI sanitizer run. No sanitizer
findings were reported in either run.

## Core test detail

```
exhaustive set algebra (all pairs through 8 ticks): 6591981 checks passed
all bracket styles and normalization: 2567 checks passed
exhaustive shifts, fitting starts and repetitions: 790692 checks passed
exhaustive sector edits against snapshot oracle: 3239188 checks passed
64-bit limits, aliases, operators, chrono and errors: 94 checks passed
randomized larger timelines: 776000 checks passed
TOTAL: 11400522 checks passed
```

These are assertion/check counts, not 11 million separately named unit tests.
The independent reference models manipulate individual bits rather than interval
lists. Copy/move tests cover holes, overlapping sectors, both paste modes,
cross-set edits, clipping, and unchanged inputs after rejected operations.

## I/O, bindings, and integration

The I/O suite passed 4,623 checks. It also ran 20,000 bounded random mutations of
set/event JSON: 370 were valid and round-tripped; 19,630 were rejected with
catchable validation errors. These probes ran under sanitizers as well.

Tests cover duplicate keys, trailing data, malformed structures, Unicode and
surrogate escapes, integer overflow, values above floating-point exact-integer
range, invalid dates, forbidden timezone suffixes, zero/negative durations,
nanosecond precision, exact frame fractions, and mismatched timelines.

The Python suite has 21 test methods with additional parameterized/randomized
checks. It exercises all operator families, in-place alias behavior, copies,
ownership/destruction, event serialization, exact duration conversion, and native
boundary errors. It independently compares randomized results against Python's
built-in sets and exact `Fraction` arithmetic.

The CLI suite has 18 test methods and runs twice in each full build: once against
the executable and once through `python -m time_set`. It checks actual stdout,
stderr, exit codes, files with spaces, JSON pipes, invalid flags, replacement and
overlay, cross-set move bundles, precision, and arithmetic boundaries.

All nine example programs (four C++, four Python, one shell) are CTest targets.
Three marked README examples are executed directly from their published code.

## Reproduce

```
./check.sh
./check.sh --sanitize
```

To choose Clang for a separate build:

```
cmake -S . -B build-clang -DCMAKE_CXX_COMPILER=clang++ \
  -DTIME_SET_BUILD_PYTHON=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-clang --parallel 2
ctest --test-dir build-clang --output-on-failure --parallel 2
```

Tests provide evidence, not a proof that every possible program or input is
bug-free. Numeric representation limits, calendar conventions, and intentionally
unsupported behavior are documented in API.md and DESIGN.md.
