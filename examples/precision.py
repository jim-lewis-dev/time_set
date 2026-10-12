"""Exact frames, nanoseconds, and picoseconds without floating-point timestamps."""
from datetime import timedelta
from fractions import Fraction
from time_set import Timeline

frames = Timeline(length=300, tick=Fraction(1,30))
clip = frames.range(0,30)
assert clip.duration == 1
assert clip + timedelta(milliseconds=100) == frames.range(3,33)
assert frames.position(frames.format(1)) == 1
print("30-frame clip delayed by 100ms:", clip + "100ms")
print("Frame one has an exact timestamp:", frames.format(1))
try:
    clip + "50ms"
except ValueError:
    print("50ms = 1.5 frames: rejected rather than rounded.")
else:
    raise AssertionError("off-grid input was accepted")

nano = Timeline(length="1us", tick="1ns", origin="2026-10-12T00:00:00.000000123")
pulse = nano.range(100,110)
assert pulse.valid_starts("8ns") == nano.range(100,103)
assert nano.format(1) == "2026-10-12T00:00:00.000000124"
print("Ten-nanosecond gate:", pulse.to_text(dates=True))

pico = Timeline(length="1ns", tick=Fraction(1,10**12), origin=None)
assert pico.length == 1000
assert pico.point(5).duration == Fraction(1,10**12)
print("A one-nanosecond timeline at picosecond resolution has", pico.length, "ticks.")
