# API reference

## Representation and contracts

Positions are signed 64-bit integers. A length of `L` defines ticks `[0,L)`;
`L` itself is a boundary, not a tick. Maximum length is `9223372036854775807`.
The last valid tick on that timeline is `9223372036854775806`.

An interval is `[lo,hi)`. Construction validates endpoints, discards empty
intervals, sorts, and merges overlaps and adjacency. A reversed interval is an
error, not an implicit empty set.

For `[A,A]`, the result is one tick. Other equal-endpoint styles are empty.
Closing the right endpoint at `L` is invalid when it would include the nonexistent
tick `L`; `[L,L)` remains a valid empty sector.

All mutation is transactional for validation and arithmetic errors. Copy/move
snapshots before modifying, including when source and destination are the same
object. Cross-set move computes both results before committing either one.

## C++ core (`time_set.hpp`)

Namespace: `ts`.

| Type | Meaning |
|---|---|
| `tick_type` | `std::int64_t` |
| `ticks{n}` | Explicit signed tick count |
| `seconds{numerator, denominator}` | Reduced exact rational seconds; denominator positive |
| `instant{whole_seconds, fraction}` | Origin relative to 2000-01-01; proper fraction in `[0,1)` |
| `timeline{length, tick, origin}` | Immutable public definition; origin is `optional<instant>` |
| `interval{lo,hi}` | Raw half-open interval |
| `time_set{timeline, vector<interval>}` | Normalized set |

Defaults: `timeline{}` has `length = 2^32`, `tick = seconds{1}`, origin `instant{}`.
`timeline::indexed(length, tick)` has no origin. `time_set{}` is an empty default
timeline. `ts::max_ticks` and `ts::default_length` expose the limits/default.

Timeline methods: `length()`, `tick_size()`, `origin()`, `empty()`, `full()`,
`point(p)`, `range(lo,hi,bounds)`, and `to_ticks(duration)`.
Bounds are `left_closed` (default), `closed`, `open`, `right_closed`.

### Operators and their named equivalents

| Operator | Named equivalent / meaning |
|---|---|
| `a | b`, `a |= b` | `a.united(b)` |
| `a & b`, `a &= b` | `a.intersection(b)` |
| `a - b`, `a -= b` | `a.difference(b)` |
| `a ^ b`, `a ^= b` | `a.symmetric_difference(b)` |
| `~a` | `a.complement()` within the domain |
| `a + d`, `a += d`, `d + a` | `a.shifted(d)`, positive means later |
| `a - d`, `a -= d` | Shift earlier by `d`; negative durations reverse direction |
| `a >> n`, `a >>= n` | Shift later by `n` ticks |
| `a << n`, `a <<= n` | Shift earlier by `n` ticks |
| `a == b`, `a != b` | Timeline and membership equality |
| `a <= b`, `a < b` | `is_subset_of`, `is_proper_subset_of` |
| `a >= b`, `a > b` | Reversed subset comparisons |
| `a[p]` | Read-only `contains(p)` |
| `bool(a)`, `!a` | Nonempty / empty |
| `ostream << a` | Normalized interval text |

Durations `d` can be `ticks`, `seconds`, or integral `std::chrono::duration`.
Bare integers are accepted for `<<`/`>>`, not `+`/`-`; explicit units prevent
ambiguity. Floating-point chrono durations are excluded at compile time.

Other queries: `domain()`, `intervals()`, `begin()/end()`, `empty()`,
`tick_count()`, `interval_count()`, `covered_duration()`, and `overlaps(other)`.
Interval access and iteration are read-only. Membership outside the domain is
false. Construction/editing outside the domain is an error unless explicitly
using a clipping operation.

### Editing

```
fill(lo, hi, bounds = bounds::left_closed)
clear(lo, hi, bounds = bounds::left_closed)
clear()
assign(lo, hi, bool, bounds = bounds::left_closed)

copy_region(lo, hi, to, mode = paste_mode::replace, clip = false)
move_region(lo, hi, to, mode = paste_mode::replace, clip = false)
copy_from(const time_set& source, lo, hi, to, mode = ..., clip = false)
move_from(time_set& source, lo, hi, to, mode = ..., clip = false)
```

Editing methods return `time_set&`. Source sectors for copying/moving are
half-open. `to` is the first tick of a destination sector of the same width.
`paste_mode::overlay` unions filled source portions onto the destination;
replacement overwrites the whole sector. Content outside the affected sectors
is unchanged. No time scaling occurs.

Copy/move reject an out-of-domain destination by default. `clip=true` explicitly
permits clipping it, including complete loss on a move. The source sector must
always be within the source timeline. Shifts always clip, never wrap.

For overlay moves, source clearing happens first. Existing destination content
is preserved except where it was itself part of the source sector being cleared.

### Repetition and activity fitting

```
repeated(every, count)
repeated_until(every, until)
valid_starts(duration)
```

These return new sets. Period/activity duration must be a positive whole number
of ticks. Repetition count is nonnegative, includes the original, and is finite.
`count=0` returns empty. `until` is exclusive and must be within the timeline.
Repetition only shifts forward and clips; it is not a recurring calendar rule.

For availability `[lo,hi)` and duration `d >= 1`, fitting starts are
`[lo,hi-d+1)` when `hi-lo >= d`. The exact-fit case contains one tick.

### Exceptions

Invalid values or mismatched timelines produce `std::invalid_argument` or
`std::out_of_range`. Arithmetic outside the supported representation produces
`std::overflow_error`. Allocation failure remains `std::bad_alloc`.
Equality with a different timeline is false; ordering/set operations reject it.
There is no implicit casting, rebasing, resampling, or interval truncation during
normal construction.

## Supporting C++ I/O (`time_set_io.hpp` + `src/time_set_io.cpp`)

```
parse_duration(text) -> seconds
format_duration(seconds) -> string
parse_timestamp(text) -> instant
format_timestamp(instant) -> string
make_timeline(length, tick="1s", origin="2000-01-01T00:00:00") -> timeline
parse_ticks(timeline, text) -> tick_type
parse_position(timeline, text) -> tick_type
format_position(timeline, position) -> string
parse_bounds(text) -> bounds
parse_region(timeline, text) -> interval
parse_text(timeline, text) -> time_set
to_text(time_set, dates=false) -> string
to_json(time_set) -> string
from_json(text) -> time_set
```

`parse_ticks` accepts integers or `t` for ticks; a physical unit means a duration.
`parse_position` additionally accepts a civil timestamp. These conversions can
return offsets outside a domain; factories/edits then validate bounds.
`format_position` allows the exclusive endpoint for interval display.

Text intervals can use tick counts, durations measured from the origin, or full
civil timestamps. Examples: `[10,20)`, `[9h,17h)`, `{50}`, or
`[2026-10-12T09:00:00,2026-10-12T10:00:00)`. Join with `|` or use `empty`.

Civil input: `YYYY-MM-DD`, optionally `T` or space followed by `HH:MM` or
`HH:MM:SS`, with an optional decimal fraction after seconds. Years 0001–9999
are supported by calendar helpers. Timezone suffixes and leap seconds are not.
A day is always exactly 86,400 seconds in these labels.

Decimal durations support up to 18 significant fractional places, after trailing
zeros are removed, subject to numerator/denominator limits. No exponent notation.

Fractions without a finite decimal of at most 18 places use exact timestamp
notation such as `2000-01-01T00:00:00[+1/30s]`. This notation round-trips through
the parser; it is not a timezone offset. A leading fractional timestamp part is
proper and nonnegative. The supported timestamp formatting range remains
0001–9999 even when the abstract tick timeline is much larger.

### Numeric limits

Positions, length, and rational numerators/denominators have signed 64-bit storage.
The tick denominator must be positive; origin fractions are stored separately
from whole seconds, preserving nanoseconds far from the epoch.

Exact rational arithmetic reports overflow when a result or an intermediate
cannot fit its 64-bit representation. A large tick set can still be
valid even when its total duration cannot fit a single `seconds` fraction, or its
end is outside the calendar helper's year range. Tick-only operations are not
limited by the calendar helpers. No operation falls back to floating point.

## Python (`time_set`)

`Timeline(length=2**32, *, tick="1s", origin="2000-01-01T00:00:00")`.
Numeric length means ticks; unit strings, `timedelta`, or `Fraction` mean duration.
A tick size requires a physical unit, `timedelta`, or `Fraction` seconds.
Use `origin=None` for indexed time.

Properties: `length`, `tick_size` (a `Fraction` of a second), `origin`.
Methods: `empty()`, `full()`, `range(start,end,bounds="[)")`, `point(position)`,
`parse(text)`, `position(value)`, `format(tick)`, and `to_ticks(duration)`.
Origins are canonical strings, `None`, or an exact numeric dictionary for an
advanced origin outside the calendar display range read from JSON.

`TimeSet(timeline=None, intervals=())` constructs from half-open integer pairs.
Operators mirror C++, with `p in set` added. In Python, integer durations mean
ticks; `ticks(n)` makes that explicit. `Fraction` values mean seconds, not ticks.
Unit strings and `timedelta` are accepted. Float/boolean counts are errors.

Properties: `timeline`, `intervals` (tuple of pairs), `tick_count`,
`interval_count`, `duration` (exact `Fraction` seconds).

Methods mirror the C++ named operations, plus `copy()`, `to_json()`,
`TimeSet.from_json(text)`, and `to_text(dates=False)`.
`clear()` empties the set; `clear(start,end)` clears a sector.
`copy_region(start,end,to,mode="replace",clip=False)` and its move/from variants
use keyword-only mode/clipping options. `fill/clear/assign` have keyword `bounds`.
`repeated(every, count=None, *, until=None)` requires exactly one stopping rule.

Iteration yields interval pairs. Sets are mutable and unhashable; ordering is
set inclusion, not a total sorting order. `copy.copy` and `copy.deepcopy` make
independent native sets. Aliases observe in-place operations.

Native invalid arguments/bounds map to `ValueError`; arithmetic overflow maps to
`OverflowError`; allocation failure maps to `MemoryError`. Wrapper type errors
are `TypeError`. No native exception escapes into Python.

## JSON schema

A normal set document:

```json
{
  "timeline": {
    "origin": "2026-10-12T00:00:00",
    "tick": "1/1000s",
    "length": 10000
  },
  "intervals": [[10, 20], [40, 45]]
}
```

`origin` is a civil string, `null` for indexed time, or an exact origin object:
`{"seconds": 9223372036854775807, "fraction": "1/30s"}`. The numeric origin is
relative to 2000-01-01 and allows C++ origins outside calendar display limits to
round-trip. Writers normally prefer readable civil strings.

All fields shown are required. Unknown fields, duplicate keys, wrong types,
trailing content, out-of-range integers, floats, invalid Unicode, excessive
nesting (over 64 levels), and malformed intervals are rejected. Raw intervals
can be unordered/adjacent and are normalized on load. Integer tokens must be
preserved exactly by any consumer; do not convert them through floating point.

The writer uses canonical reduced second fractions: `250ms` becomes `1/4s`.
JSON field order and whitespace do not carry meaning. The writer is compact;
original bracket style, ordering, and formatting are not retained.

### Events and cross-set moves

`ts::event{name, description, times}` is in `time_event.hpp`.
The I/O helpers overload `to_json(event)` and provide `event_from_json(text)`.
Python uses `Event(name, times, description="")`, with `to_json/from_json`.
Events are deliberately not part of the CLI set-operation pipeline.

An event document has exactly `name`, `description`, and `times` (a set document).

A cross-set CLI move returns exactly `source` and `destination`, each a complete
set document. `time_set select move.json source` or `... destination` extracts
one. C++ exposes `move_to_json(source,destination)` and
`select_from_json(text,field)`. Both halves of a selected bundle are validated.

`stats` writes `tick_count`, `interval_count`, `duration`, `duration_overflow`,
and `empty`. Duration is a reduced unit string, or `null` with overflow true when
it cannot fit the rational representation. Counts remain exact.

## CLI command reference

Run `time_set --help` for the authoritative synopsis. All set-returning commands
accept `--text` or `--dates`; JSON is default. Both flags together mean dates.
`show` is inherently text. Query output is JSON `true`/`false`; query exit status
is always 0 on a valid operation.

`make`, `union`, `intersect`, `subtract`, `xor`, `complement`, `shift`, `fill`,
`clear`, `copy`, `move`, `repeat`, `starts`, `show`, `stats`, `contains`, `subset`,
`overlaps`, `equal`, and `select` comprise the command set.

`--name=value` and `--name value` both work. Duplicate, unknown, and inapplicable
options fail. `--` ends option parsing. Input is read-only; no command rewrites an
input file. Standard input can be read only once in a single invocation. Binary
operations require explicit inputs; unary filters normally default to stdin.
