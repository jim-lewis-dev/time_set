"""Repeat a simple pattern, restrict weekdays, then add explicit exceptions."""
from fractions import Fraction
from time_set import Event, Timeline

week = Timeline(length="7d", tick="1min", origin="2026-10-12")
day, hour = week.to_ticks("1d"), week.to_ticks("1h")
pattern = week.range("9h","11h")
weekly = pattern.repeated(every="1d", count=7)
weekdays = weekly & week.range(0,5 * day)
wednesday = week.range(2 * day + 9 * hour,2 * day + 11 * hour)
saturday_extra = week.range(5 * day + 14 * hour,5 * day + 16 * hour)
focus = (weekdays - wednesday) | saturday_extra
assert weekly.interval_count == 7
assert focus.interval_count == 5
assert focus.duration / 3600 == Fraction(10)
event = Event("Focus time", focus, "Weekdays except Wednesday, plus Saturday afternoon")
assert Event.from_json(event.to_json()) == event
print("Five focus blocks, ten hours total:")
for lo, hi in focus:
    print(" ", week.format(lo), "to", week.format(hi))
print("The optional Event wrapper adds metadata without changing the set engine.")
