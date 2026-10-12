"""Copy holes, overlay intervals, and move overlapping sectors predictably."""
from time_set import Timeline

line = Timeline(length=100, origin=None)
original = line.parse("[10,15) | [47,50)")
replacement = original.copy().copy_region(10,20,to=40)
overlay = original.copy().copy_region(10,20,to=40,mode="overlay")
assert replacement == line.parse("[10,15) | [40,45)")
assert overlay == line.parse("[10,15) | [40,45) | [47,50)")
print("Source sector: [10,20), destination sector: [40,50)")
print("Original:   ", original)
print("Replacement:", replacement)
print("Overlay:    ", overlay)

moving = line.parse("[10,13) | [15,18) | [24,27)")
moving.move_region(10,20,to=15)
assert moving == line.parse("[15,18) | [20,23) | [25,27)")
print("Overlapping move, snapshot before clearing:", moving)

source = line.range(10,15)
destination = line.range(47,50)
destination.move_from(source,10,20,to=40)
assert not source
assert destination == line.range(40,45)
print("Cross-set move: source =", source, "; destination =", destination)

blank = line.empty()
destination.copy_from(blank,0,10,to=40)
assert not destination
print("Copying a blank sector clears the destination:", destination)
