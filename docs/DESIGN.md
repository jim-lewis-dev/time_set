# Design notes

## A small engine, not a calendar application

The core owns normalized interval vectors and a value-type timeline. It contains
no JSON parser, date parser, Python dependency, file access, timezone service,
event metadata, or optimizer. C++ programs can include one header and use the
mathematics directly.

The separate I/O library provides exact units, civil labels, and strict JSON.
The executable uses it. The native Python extension links the same core, I/O,
and command parser. Python's thin public classes provide idiomatic operators,
standard-library duration conversion, and ownership; they do not implement
interval algorithms and do not spawn the CLI.

## Why a vector of intervals

An hour-long continuous interval occupies one record, not thousands of ticks.
Canonical form is sorted, disjoint, nonadjacent half-open intervals. That makes
comparison deterministic and binary operations straightforward ordered scans.

With n and m input intervals, membership is O(log n); union/intersection/difference
and symmetric difference are O(n+m). Complement, shifting, and fitting starts are
O(n). Constructing arbitrary unordered input is O(n log n). Edits scan interval
lists, not every tick in a sector.

Repetition uses binary doubling of shifted blocks, not one loop iteration per
requested occurrence. This handles enormous dense repetitions without billions
of iterations. Sparse repetitions still require storage proportional to the
number of distinct occupied runs; the representation is not a lazy recurrence
rule. Requesting an enormous sparse result can exhaust memory. There is no hidden
truncation of an otherwise valid mathematical result.

## Timelines are part of a set's identity

Tick 100 on a millisecond grid is not tick 100 on a second grid. Nor is tick 100
from one origin necessarily the same time as tick 100 from another. Compatibility
is exact value equality of length, reduced tick duration, and origin. Equivalent
civil origin spellings are normalized by the date parser. Indexed time has an
explicitly absent origin, not a magic date string.

No implicit resampling is allowed. A caller must deliberately define any future
conversion policy: for example whether covering an entire coarse tick or merely
intersecting it is enough. That policy does not belong inside ordinary union.

## Finite complement, checked arithmetic

Complement means every tick in the explicit timeline that is not in the set.
The complement of an empty set is full; the complement of a full set is empty,
including the zero-length universe. The exclusive final boundary is representable.

Signed 64-bit shifts include INT64_MIN without negation overflow. Large shifts
clip directly to empty. Relative destination calculations clamp without first
forming an overflowing signed sum. Rational durations reduce factors before
multiplying; overflow or off-grid input is an explicit error, never rounding.
Calendar formatting uses portable split multiplication/division where a product
needs wider intermediates; no nonstandard integer type is required.

## Copy/move is a pattern operation

A sector includes its blank portions. Replacement copies the entire bit pattern;
overlay adds only occupied bits. Both preserve the sector's original width, even
when its content is empty.

For move, capture source bits, clear the source sector, then paste. For overlap,
that sequence is the definition, rather than an accident of traversal order.
Cross-set moves build both new results before swapping their interval vectors.
Validation/arithmetic failures leave both source and destination unchanged.

Shifts intentionally discard out-of-domain content. Copy/move use a different
safe default: reject an out-of-domain destination unless the caller explicitly
allows clipping. That prevents an ordinary move from silently deleting data.

## Formats

JSON is the machine/persistence format and includes all timeline metadata.
Human text is a view of the normalized set and needs an explicit timeline when
read back. There are no YAML/CSV/XML options or placeholder format switches.
Events and cross-set moves are different documented JSON object shapes, not
additional output encodings.

## Deliberate non-features

No Boost or binding framework. No implicit floats, timezone/DST interpretation,
calendar recurrence rules, infinities, wrapping shifts, automatic resampling,
solver, GUI, or persistence database.

The date helpers implement a simple civil-time convention. They must not be
mistaken for physical elapsed-time arithmetic through a timezone transition.
Rational metadata and conversions have explicit signed 64-bit limits; the set
engine itself remains usable across the full supported tick range.
