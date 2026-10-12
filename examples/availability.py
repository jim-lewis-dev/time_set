"""Two people's free time, and the exact set of usable meeting starts."""
from time_set import Timeline

day = Timeline(length="1d", tick="1min", origin="2026-10-12")
office = day.parse("[9h,17h)")
alice_busy = day.parse("[9h,10h) | [12h,13h) | [15h,16h)")
bob_busy = day.parse("[10h,11h) | [16h,17h)")
shared_free = office - (alice_busy | bob_busy)
starts = shared_free.valid_starts("90min")
assert shared_free == day.parse("[11h,12h) | [13h,15h)")
assert starts == day.range(780,811)
print("Shared free time:", shared_free.to_text(dates=True))
lo, hi = starts.intervals[0]
print("90-minute meeting starts:", day.format(lo), "through", day.format(hi - 1), "inclusive")

fragmented = day.parse("[9h,10h) | [11h,12h)")
assert fragmented.tick_count == 120
assert not fragmented.valid_starts("90min")
print("Two separate free hours total 120 minutes, but still cannot hold a 90-minute meeting.")
