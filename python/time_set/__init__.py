"""Exact finite sets of timeline ticks, backed by the C++ time_set engine.

Integers represent ticks, unit strings represent exact durations, and naive
civil datetime/date objects are accepted as positions. Floats and timezone-
aware timestamps are deliberately rejected. No interval algorithms live here.
"""
from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime, timedelta
from fractions import Fraction
from typing import Iterable, Iterator
from . import _native

__all__ = ["Timeline", "TimeSet", "Ticks", "ticks", "Event", "MAX_TICKS"]
MAX_TICKS = (1 << 63) - 1
_MIN_TICKS = -(1 << 63)
_DEFAULT_LENGTH = 1 << 32
_DEFAULT_ORIGIN = "2000-01-01T00:00:00"


def _integer(value: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError("expected an integer, not a float or boolean")
    if not _MIN_TICKS <= value <= MAX_TICKS:
        raise OverflowError("integer exceeds signed 64-bit range")
    return value


@dataclass(frozen=True, slots=True)
class Ticks:
    """An explicit signed count of ticks, independent of the tick duration."""
    count: int

    def __post_init__(self) -> None:
        _integer(self.count)


def ticks(count: int) -> Ticks:
    return Ticks(count)


def _duration(value):
    if isinstance(value, Ticks):
        return value.count
    if isinstance(value, timedelta):
        value = Fraction((value.days * 86400 + value.seconds) * 1_000_000 + value.microseconds, 1_000_000)
    if isinstance(value, Fraction):
        return f"{value.numerator}/{value.denominator}s"
    if isinstance(value, str):
        return value
    return _integer(value)


def _position(value):
    if isinstance(value, datetime):
        if value.utcoffset() is not None:
            raise ValueError("timezone-aware datetimes are not accepted; use explicit civil labels")
        return value.isoformat()
    if isinstance(value, date):
        return value.isoformat()
    return _duration(value)


def _size(value) -> str:
    if isinstance(value, (timedelta, Fraction, str)):
        return _duration(value)
    raise TypeError("tick size must have units, such as '1ms', or be Fraction seconds/timedelta")


class Timeline:
    """Immutable timeline definition. Use origin=None for a purely indexed axis.

    Numeric length is a tick count; a unit string is an exact duration.
    Equivalent origins and reduced tick fractions compare equal.
    """
    __slots__ = ("_handle",)
    __hash__ = None

    def __init__(self, length=_DEFAULT_LENGTH, *, tick="1s", origin=_DEFAULT_ORIGIN):
        if origin is not None:
            if isinstance(origin, datetime) and origin.utcoffset() is not None:
                raise ValueError("timezone-aware origins are not accepted")
            if isinstance(origin, (datetime, date)):
                origin = origin.isoformat()
            if not isinstance(origin, str):
                raise TypeError("origin must be a civil date string, naive date/datetime, or None")
        self._handle = _native.timeline(_duration(length), _size(tick), origin)

    @classmethod
    def _wrap(cls, handle) -> Timeline:
        value = object.__new__(cls)
        value._handle = handle
        return value

    @property
    def length(self) -> int:
        return _native.timeline_info(self._handle)["length"]

    @property
    def tick_size(self) -> Fraction:
        info = _native.timeline_info(self._handle)
        return Fraction(info["numerator"], info["denominator"])

    @property
    def origin(self):
        return _native.timeline_info(self._handle)["origin"]

    def __eq__(self, other):
        if not isinstance(other, Timeline):
            return NotImplemented
        return _native.timeline_equal(self._handle, other._handle)

    def __repr__(self) -> str:
        return f"Timeline(length={self.length}, tick={str(self.tick_size) + 's'!r}, origin={self.origin!r})"

    def empty(self) -> TimeSet:
        return TimeSet(self)

    def full(self) -> TimeSet:
        return ~self.empty()

    def range(self, start, end, *, bounds="[)") -> TimeSet:
        return TimeSet._wrap(_native.range(self._handle, _position(start), _position(end), bounds))

    def point(self, position) -> TimeSet:
        return TimeSet._wrap(_native.point(self._handle, _position(position)))

    def parse(self, text: str) -> TimeSet:
        """Read '[10,20) | {30}' using this timeline's metadata."""
        return TimeSet._wrap(_native.parse(self._handle, text))

    def position(self, value) -> int:
        """Convert an exact position to ticks; construction validates the domain."""
        return _native.position(self._handle, _position(value), "position")

    def format(self, position: int) -> str:
        """Format a tick/boundary as an exact civil timestamp."""
        return _native.position(self._handle, _integer(position), "format")

    def to_ticks(self, duration) -> int:
        return _native.position(self._handle, _duration(duration), "duration")


class TimeSet:
    """Mutable interval set. Operators return new sets; assignments/edits mutate.

    Iteration yields half-open (start, end) pairs, never individual ticks.
    Use tick_count and interval_count explicitly; there is no ambiguous len().
    """
    __slots__ = ("_handle",)
    __hash__ = None

    def __init__(self, timeline: Timeline | None = None,
                 intervals: Iterable[tuple[int, int]] = ()):
        if timeline is None:
            timeline = Timeline()
        if not isinstance(timeline, Timeline):
            raise TypeError("timeline must be a Timeline")
        self._handle = _native.make(timeline._handle, intervals)

    @classmethod
    def _wrap(cls, handle) -> TimeSet:
        value = object.__new__(cls)
        value._handle = handle
        return value

    @property
    def timeline(self) -> Timeline:
        return Timeline._wrap(_native.domain(self._handle))

    @property
    def intervals(self) -> tuple[tuple[int, int], ...]:
        return _native.intervals(self._handle)

    @property
    def tick_count(self) -> int:
        return _native.query(self._handle, "ticks", None)

    @property
    def interval_count(self) -> int:
        return _native.query(self._handle, "interval_count", None)

    @property
    def duration(self) -> Fraction:
        """Exact covered seconds; may raise OverflowError for a huge rational."""
        return Fraction(*_native.query(self._handle, "duration", None))

    def empty(self) -> bool:
        return _native.query(self._handle, "empty", None)

    def __bool__(self) -> bool:
        return not self.empty()

    def __iter__(self) -> Iterator[tuple[int, int]]:
        return iter(self.intervals)

    def __contains__(self, point) -> bool:
        return _native.query(self._handle, "contains", _position(point))

    def __getitem__(self, point) -> bool:
        return point in self

    def contains(self, point) -> bool:
        return point in self

    def copy(self) -> TimeSet:
        return TimeSet._wrap(_native.clone(self._handle))

    def __copy__(self) -> TimeSet:
        return self.copy()

    def __deepcopy__(self, memo) -> TimeSet:
        result = self.copy()
        memo[id(self)] = result
        return result

    def _binary(self, other, operation: str, inplace=False):
        if not isinstance(other, TimeSet):
            return NotImplemented
        handle = _native.binary(self._handle, other._handle, operation, inplace)
        return self if inplace else TimeSet._wrap(handle)

    def __or__(self, other): return self._binary(other, "union")
    def __and__(self, other): return self._binary(other, "intersection")
    def __xor__(self, other): return self._binary(other, "xor")
    def __ior__(self, other): return self._binary(other, "union", True)
    def __iand__(self, other): return self._binary(other, "intersection", True)
    def __ixor__(self, other): return self._binary(other, "xor", True)
    def __invert__(self): return TimeSet._wrap(_native.complement(self._handle))

    def _shift(self, amount, reverse=False, inplace=False):
        handle = _native.shift(self._handle, _duration(amount), reverse, inplace)
        return self if inplace else TimeSet._wrap(handle)

    def __add__(self, duration): return self._shift(duration)
    def __radd__(self, duration): return self._shift(duration)
    def __iadd__(self, duration): return self._shift(duration, inplace=True)
    def __sub__(self, other):
        return self._binary(other, "difference") if isinstance(other, TimeSet) else self._shift(other, reverse=True)
    def __isub__(self, other):
        return self._binary(other, "difference", True) if isinstance(other, TimeSet) else self._shift(other, reverse=True, inplace=True)
    def __rshift__(self, count): return self._shift(_integer(count))
    def __lshift__(self, count): return self._shift(_integer(count), reverse=True)
    def __irshift__(self, count): return self._shift(_integer(count), inplace=True)
    def __ilshift__(self, count): return self._shift(_integer(count), reverse=True, inplace=True)

    def _compare(self, other, operation):
        if not isinstance(other, TimeSet):
            return NotImplemented
        return _native.query(self._handle, operation, other._handle)

    def __eq__(self, other): return self._compare(other, "equal")
    def __le__(self, other): return self._compare(other, "subset")
    def __lt__(self, other): return self._compare(other, "proper_subset")
    def __ge__(self, other):
        return other._compare(self, "subset") if isinstance(other, TimeSet) else NotImplemented
    def __gt__(self, other):
        return other._compare(self, "proper_subset") if isinstance(other, TimeSet) else NotImplemented

    def _require_other(self, other) -> TimeSet:
        if not isinstance(other, TimeSet):
            raise TypeError("expected a TimeSet")
        return other

    def united(self, other): return self | self._require_other(other)
    def intersection(self, other): return self & self._require_other(other)
    def difference(self, other): return self - self._require_other(other)
    def symmetric_difference(self, other): return self ^ self._require_other(other)
    def complement(self): return ~self
    def shifted(self, duration): return self._shift(duration)
    def is_subset_of(self, other): return self <= self._require_other(other)
    def is_proper_subset_of(self, other): return self < self._require_other(other)
    def overlaps(self, other): return self._compare(self._require_other(other), "overlaps")

    def _edit(self, operation, start=None, end=None, to=None, mode="replace", clip=False, source=None, bounds="[)"):
        if not isinstance(clip, bool):
            raise TypeError("clip must be a boolean")
        if source is not None:
            self._require_other(source)
        _native.edit(self._handle, operation, None if source is None else source._handle,
                     None if start is None else _position(start),
                     None if end is None else _position(end),
                     None if to is None else _position(to), mode, clip, bounds)
        return self

    def fill(self, start, end, *, bounds="[)"):
        return self._edit("fill", start, end, bounds=bounds)

    def clear(self, start=None, end=None, *, bounds="[)"):
        if (start is None) != (end is None):
            raise ValueError("clear needs both endpoints, or neither")
        return self._edit("clear_all" if start is None else "clear", start, end, bounds=bounds)

    def assign(self, start, end, value: bool, *, bounds="[)"):
        if not isinstance(value, bool):
            raise TypeError("assigned value must be a boolean")
        return self.fill(start, end, bounds=bounds) if value else self.clear(start, end, bounds=bounds)

    def copy_region(self, start, end, to, *, mode="replace", clip=False):
        return self._edit("copy", start, end, to, mode, clip)

    def move_region(self, start, end, to, *, mode="replace", clip=False):
        return self._edit("move", start, end, to, mode, clip)

    def copy_from(self, source, start, end, to, *, mode="replace", clip=False):
        return self._edit("copy", start, end, to, mode, clip, source)

    def move_from(self, source, start, end, to, *, mode="replace", clip=False):
        return self._edit("move", start, end, to, mode, clip, source)

    def repeated(self, every, count=None, *, until=None) -> TimeSet:
        return TimeSet._wrap(_native.repeat(self._handle, _duration(every),
            None if count is None else _integer(count), None if until is None else _position(until)))

    def valid_starts(self, duration) -> TimeSet:
        return TimeSet._wrap(_native.starts(self._handle, _duration(duration)))

    def to_json(self) -> str:
        return _native.dump(self._handle)

    @classmethod
    def from_json(cls, text: str) -> TimeSet:
        return cls._wrap(_native.load(text))

    def to_text(self, *, dates=False) -> str:
        if not isinstance(dates, bool):
            raise TypeError("dates must be a boolean")
        return _native.text(self._handle, dates)

    def __str__(self) -> str:
        return self.to_text()

    def __repr__(self) -> str:
        return f"TimeSet({self.timeline!r}, intervals={self.intervals!r})"


@dataclass
class Event:
    """Optional event metadata kept separate from time-set mathematics."""
    name: str
    times: TimeSet
    description: str = ""

    def __post_init__(self) -> None:
        if not isinstance(self.name, str) or not isinstance(self.description, str):
            raise TypeError("event name and description must be strings")
        if not isinstance(self.times, TimeSet):
            raise TypeError("event times must be a TimeSet")

    def to_json(self) -> str:
        return _native.event_dump(self.name, self.description, self.times._handle)

    @classmethod
    def from_json(cls, text: str) -> Event:
        name, description, handle = _native.event_load(text)
        return cls(name, TimeSet._wrap(handle), description)


def main(argv=None) -> int:
    """Entry point for the same C++ CLI, also installed by pip."""
    import sys
    return _native.cli(sys.argv[1:] if argv is None else argv)
