"""Python API, exact conversions, ownership, errors, and native parity."""
import copy
from datetime import date, datetime, timedelta, timezone
from fractions import Fraction
import gc
import json
import random
import unittest
from time_set import Event, MAX_TICKS, Timeline, TimeSet, Ticks, ticks


class PythonInterfaceTest(unittest.TestCase):
    def setUp(self):
        self.line = Timeline(length=100)
        self.a = self.line.range(10,20)
        self.b = self.line.range(15,25,bounds="[]")

    def test_default_and_metadata(self):
        default = Timeline()
        self.assertEqual(default.length,1 << 32)
        self.assertEqual(default.origin,"2000-01-01T00:00:00")
        self.assertEqual(default.tick_size,Fraction(1))
        self.assertFalse(TimeSet())
        self.assertEqual(self.a.timeline,self.line)
        self.assertEqual(self.a.intervals,((10,20),))
        self.assertEqual(self.a.tick_count,10)
        self.assertEqual(self.a.interval_count,1)
        self.assertEqual(self.a.duration,Fraction(10))
        self.assertIn("Timeline",repr(self.line))
        self.assertIn("TimeSet",repr(self.a))
        with self.assertRaises(AttributeError): self.line.length = 3
        with self.assertRaises(AttributeError): self.a.intervals = ()
        with self.assertRaises(TypeError): len(self.a)
        with self.assertRaises(TypeError): hash(self.a)

    def test_timeline_equivalence(self):
        a = Timeline(length=100,tick="2/60s",origin="2026-10-11")
        b = Timeline(length=100,tick=Fraction(1,30),origin=datetime(2026,10,11))
        self.assertEqual(a,b)
        self.assertEqual(a,Timeline(length=100,tick="1/30s",origin=date(2026,10,11)))
        self.assertNotEqual(a,Timeline(length=101,tick="1/30s",origin="2026-10-11"))
        self.assertNotEqual(a,Timeline(length=100,tick="1/30s",origin=None))
        self.assertNotEqual(a,None)
        self.assertIsNone(Timeline(origin=None).origin)

    def test_duration_inputs(self):
        line = Timeline(length="10s",tick="1ms")
        self.assertEqual(line.length,10000)
        self.assertEqual(line.to_ticks("0.1s"),100)
        self.assertEqual(line.to_ticks(Fraction(1,10)),100)
        self.assertEqual(line.to_ticks(timedelta(milliseconds=100)),100)
        self.assertEqual(line.to_ticks(ticks(100)),100)
        self.assertEqual(line.to_ticks(100),100)
        self.assertEqual(line.to_ticks(timedelta(microseconds=-1000)),-1)
        self.assertEqual(Timeline(length=100,tick=timedelta(microseconds=1)).tick_size,Fraction(1,1000000))
        self.assertEqual(Timeline(length=timedelta(seconds=10),tick="1ms").length,10000)
        for value in [0.1,True,None,object()]:
            with self.subTest(value=value), self.assertRaises(TypeError): line.to_ticks(value)
        with self.assertRaises(ValueError): line.to_ticks("0.1ms")
        with self.assertRaises(OverflowError): line.to_ticks("9223372036854775807s")

    def test_timeline_validation(self):
        for kwargs in [{"length":-1},{"tick":"0s"},{"tick":"-1s"},{"length":"1s","tick":"3s"},
                       {"origin":"2026-02-30"},{"origin":"2026-10-11T00:00:00Z"}]:
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError): Timeline(**kwargs)
        for kwargs in [{"length":1.0},{"length":True},{"tick":1},{"origin":0}]:
            with self.subTest(kwargs=kwargs), self.assertRaises(TypeError): Timeline(**kwargs)
        with self.assertRaises(OverflowError): Timeline(length=MAX_TICKS+1)
        with self.assertRaises(ValueError): Timeline(origin=datetime.now(timezone.utc))
        with self.assertRaises(ValueError): self.line.range(datetime(2000,1,1,tzinfo=timezone.utc),10)
        self.assertFalse(Timeline(length=0).full())
        self.assertFalse(~Timeline(length=0).empty())

    def test_boundaries_and_normalization(self):
        expected = {"[)":((10,13),),"[]":((10,14),),"()":((11,13),),"(]":((11,14),)}
        for ends,intervals in expected.items():
            self.assertEqual(self.line.range(10,13,bounds=ends).intervals,intervals)
        self.assertEqual(self.line.range(10,10,bounds="[]"),self.line.point(10))
        self.assertFalse(self.line.range(10,10))
        self.assertEqual(TimeSet(self.line,[(4,6),(1,3),(3,4),(0,0),(5,6)]),self.line.range(1,6))
        self.assertEqual(TimeSet(self.line,((n,n+1) for n in range(3))),self.line.range(0,3))
        for intervals in [[(-1,2)],[(3,2)],[(0,101)],[(1,)],[(1,2,3)],[(1.0,2)],[(True,2)]]:
            with self.subTest(intervals=intervals), self.assertRaises((TypeError,ValueError)):
                TimeSet(self.line,intervals)
        with self.assertRaises(ValueError): self.line.range(0,100,bounds="[]")
        with self.assertRaises(ValueError): self.line.range(10,9)
        with self.assertRaises(ValueError): self.line.range(0,10,bounds="bad")
        with self.assertRaises(ValueError): self.line.point(100)
        with self.assertRaises(TypeError): TimeSet(5)

    def test_set_operators_and_named_operations(self):
        self.assertEqual(self.a | self.b,self.line.range(10,26))
        self.assertEqual(self.a & self.b,self.line.range(15,20))
        self.assertEqual(self.a - self.b,self.line.range(10,15))
        self.assertEqual(self.a ^ self.b,self.line.parse("[10,15) | [20,26)"))
        self.assertEqual(~self.a,self.line.parse("[0,10) | [20,100)"))
        self.assertEqual(self.a.united(self.b),self.a | self.b)
        self.assertEqual(self.a.intersection(self.b),self.a & self.b)
        self.assertEqual(self.a.difference(self.b),self.a - self.b)
        self.assertEqual(self.a.symmetric_difference(self.b),self.a ^ self.b)
        self.assertEqual(self.a.complement(),~self.a)
        for op in [lambda:self.a.united(1),lambda:self.a & 1,lambda:self.a.overlaps(1)]:
            with self.assertRaises(TypeError): op()

    def test_shifts_and_mutating_operators(self):
        for amount in [ticks(5),5,"5s",Fraction(5),timedelta(seconds=5)]:
            self.assertEqual(self.a + amount,self.line.range(15,25))
            self.assertEqual(amount + self.a,self.line.range(15,25))
            self.assertEqual(self.a - amount,self.line.range(5,15))
        self.assertEqual(self.a >> 5,self.a + ticks(5))
        self.assertEqual(self.a << 5,self.a - ticks(5))
        self.assertEqual(self.a.shifted("-15s"),self.line.range(0,5))
        self.assertEqual(self.a >> -5,self.a << 5)
        value = self.a.copy()
        alias = value
        value += 5
        self.assertIs(value,alias)
        self.assertEqual(alias,self.line.range(15,25))
        value -= 5
        value >>= 5
        value <<= 5
        self.assertEqual(value,self.a)
        value |= self.b
        self.assertEqual(value,self.a | self.b)
        value &= self.b
        self.assertEqual(value,self.b)
        value ^= self.b
        self.assertFalse(value)
        value |= self.a
        value -= self.a
        self.assertFalse(value)

    def test_order_and_membership(self):
        part = self.line.range(12,14)
        self.assertTrue(part <= self.a)
        self.assertTrue(part < self.a)
        self.assertTrue(self.a >= part)
        self.assertTrue(self.a > part)
        self.assertTrue(self.a <= self.a)
        self.assertFalse(self.a < self.a)
        self.assertTrue(part.is_subset_of(self.a))
        self.assertTrue(part.is_proper_subset_of(self.a))
        self.assertTrue(self.a.overlaps(self.b))
        self.assertFalse(self.a.overlaps(self.line.range(20,30)))
        self.assertNotEqual(self.a,None)
        self.assertIn(10,self.a)
        self.assertNotIn(20,self.a)
        self.assertNotIn(-1,self.a)
        self.assertTrue(self.a[10])
        self.assertTrue(self.a.contains("10s"))
        self.assertEqual(list(self.a),[(10,20)])

    def test_incompatible_timelines_and_atomic_failures(self):
        other = Timeline(length=100,tick="1ms").range(10,20)
        self.assertNotEqual(self.a,other)
        for operation in [lambda:self.a | other,lambda:self.a <= other,lambda:self.a.overlaps(other),
                          lambda:self.a.copy_from(other,10,20,40),lambda:self.a.move_from(other,10,20,40)]:
            with self.assertRaises(ValueError): operation()
        self.assertEqual(self.a,self.line.range(10,20))
        self.assertEqual(other.intervals,((10,20),))
        for operation in [lambda:self.a.move_from(self.b,10,20,95),
                          lambda:self.a.move_region(-1,20,40),
                          lambda:self.a.copy_region(10,20,40,mode="wat")]:
            with self.assertRaises(ValueError): operation()
        self.assertEqual(self.a,self.line.range(10,20))
        self.assertEqual(self.b,self.line.range(15,26))

    def test_fill_clear_assign(self):
        self.assertIs(self.a.fill(20,30),self.a)
        self.assertEqual(self.a,self.line.range(10,30))
        self.a.clear(15,25)
        self.assertEqual(self.a,self.line.parse("[10,15) | [25,30)"))
        self.a.fill(50,50,bounds="[]")
        self.assertIn(50,self.a)
        self.a.assign(0,100,False)
        self.assertFalse(self.a)
        self.a.assign(0,100,True)
        self.assertEqual(self.a,self.line.full())
        self.a.clear()
        self.assertFalse(self.a)
        with self.assertRaises(ValueError): self.a.clear(10)
        with self.assertRaises(TypeError): self.a.assign(1,2,1)

    def test_copy_replace_overlay_and_holes(self):
        original = self.line.parse("[10,15) | [47,50)")
        replaced = original.copy().copy_region(10,20,to=40)
        overlay = original.copy().copy_region(10,20,to=40,mode="overlay")
        self.assertEqual(replaced,self.line.parse("[10,15) | [40,45)"))
        self.assertEqual(overlay,self.line.parse("[10,15) | [40,45) | [47,50)"))
        replaced.copy_from(self.line.empty(),0,10,to=40)
        self.assertEqual(replaced,self.line.range(10,15))
        overlay.copy_from(self.line.empty(),0,10,to=40,mode="overlay")
        self.assertEqual(overlay,self.line.parse("[10,15) | [40,45) | [47,50)"))
        self.assertEqual(original,self.line.parse("[10,15) | [47,50)"))

    def test_overlapping_move(self):
        a = self.line.parse("[10,13) | [15,18) | [24,27)")
        a.move_region(10,20,15)
        self.assertEqual(a,self.line.parse("[15,18) | [20,23) | [25,27)"))
        b = self.line.parse("[10,13) | [15,18) | [24,27)")
        b.move_region(10,20,15,mode="overlay")
        self.assertEqual(b,self.line.parse("[15,18) | [20,23) | [24,27)"))
        source, destination = self.a.copy(), self.b.copy()
        destination.move_from(source,10,20,40)
        self.assertFalse(source)
        self.assertEqual(destination,self.line.parse("[15,26) | [40,50)"))
        a = self.a.copy()
        a.move_from(a,10,20,10)
        self.assertEqual(a,self.a)

    def test_explicit_clipping(self):
        a = self.a.copy()
        with self.assertRaises(ValueError): a.move_region(10,20,95)
        self.assertEqual(a,self.a)
        a.move_region(10,20,95,clip=True)
        self.assertEqual(a,self.line.range(95,100))
        b = self.a.copy().move_region(10,20,-5,clip=True)
        self.assertEqual(b,self.line.range(0,5))
        with self.assertRaises(TypeError): self.a.copy_region(10,20,40,clip="yes")

    def test_repeat_and_fit(self):
        self.assertEqual(self.a.repeated(15,3),self.line.parse("[10,20) | [25,35) | [40,50)"))
        self.assertEqual(self.a.repeated("15s",until=30),self.line.parse("[10,20) | [25,30)"))
        self.assertFalse(self.a.repeated(1,0))
        self.assertEqual(self.a.repeated(1,MAX_TICKS),self.line.range(10,100))
        self.assertEqual(self.a.valid_starts(4),self.line.range(10,17))
        self.assertEqual(self.a.valid_starts(10),self.line.point(10))
        self.assertFalse(self.a.valid_starts(11))
        for operation in [lambda:self.a.repeated(0,1),lambda:self.a.repeated(1,-1),lambda:self.a.repeated(1),
                          lambda:self.a.repeated(1,1,until=30),lambda:self.a.valid_starts(0),lambda:self.a.valid_starts(-1)]:
            with self.assertRaises(ValueError): operation()
        with self.assertRaises(TypeError): self.a.repeated(1,True)

    def test_calendar_nanoseconds_frames(self):
        line = Timeline(length="1d",tick="1ns",origin="2026-10-11T00:00:00.000000123")
        self.assertEqual(line.position("2026-10-11T00:00:00.000000124"),1)
        self.assertEqual(line.format(1),"2026-10-11T00:00:00.000000124")
        self.assertEqual(line.position(line.format(123456789)),123456789)
        ordinary = Timeline(length="1d",origin="2026-10-11")
        self.assertEqual(ordinary.position(datetime(2026,10,11,1)),3600)
        self.assertEqual(ordinary.position(date(2026,10,11)),0)
        frames = Timeline(length=300,tick=Fraction(1,30))
        for p in range(100): self.assertEqual(frames.position(frames.format(p)),p)
        self.assertEqual(frames.to_ticks(timedelta(milliseconds=100)),3)
        with self.assertRaises(ValueError): frames.to_ticks(timedelta(milliseconds=50))
        with self.assertRaises(ValueError): Timeline(origin=None).format(0)
        with self.assertRaises(ValueError): Timeline(origin=None).position("2000-01-01")
        self.assertEqual(Timeline(length="1ns",tick=Fraction(1,10**12)).length,1000)

    def test_64_bit_limits(self):
        line = Timeline(length=MAX_TICKS)
        last = line.point(MAX_TICKS-1)
        self.assertEqual(last.intervals,((MAX_TICKS-1,MAX_TICKS),))
        self.assertEqual(last.tick_count,1)
        self.assertEqual(line.full().tick_count,MAX_TICKS)
        self.assertFalse(last >> 1)
        self.assertFalse(last << -(1 << 63))
        self.assertFalse(last - ticks(-(1 << 63)))
        self.assertEqual(TimeSet.from_json(last.to_json()),last)
        with self.assertRaises(ValueError): line.point(MAX_TICKS)
        with self.assertRaises(OverflowError): ticks(MAX_TICKS+1)
        with self.assertRaises(TypeError): ticks(True)
        with self.assertRaises(TypeError): ticks(1.0)
        with self.assertRaises(OverflowError): Timeline(length=2,tick=f"{MAX_TICKS}s").full().duration

    def test_json_text_and_event(self):
        self.assertEqual(TimeSet.from_json(self.a.to_json()),self.a)
        self.assertEqual(json.loads(self.a.to_json())["intervals"],[[10,20]])
        self.assertEqual(self.line.parse(str(self.a)),self.a)
        self.assertEqual(self.a.to_text(),"[10,20)")
        self.assertEqual(self.a.to_text(dates=True),"[2000-01-01T00:00:10,2000-01-01T00:00:20)")
        event = Event('Focus 🧠 "quoted"',self.a,"Tabs\tand\nlines\x00included")
        self.assertEqual(Event.from_json(event.to_json()),event)
        self.assertEqual(json.loads(event.to_json())["description"],event.description)
        for text in ["", "[]", "{}", self.a.to_json()+"junk", self.a.to_json().replace('"length":100','"length":100,"length":100')]:
            with self.assertRaises(ValueError): TimeSet.from_json(text)
        with self.assertRaises(TypeError): TimeSet.from_json(b"{}")
        with self.assertRaises(TypeError): Event(1,self.a)
        with self.assertRaises(TypeError): Event("x",None)
        with self.assertRaises(TypeError): self.a.to_text(dates="yes")

    def test_ownership_and_copies(self):
        a = copy.copy(self.a)
        b = copy.deepcopy(self.a)
        self.a.clear()
        self.assertEqual(a,self.line.range(10,20))
        self.assertEqual(a,b)
        other_line = a.timeline
        del self.line
        gc.collect()
        self.assertEqual(a.timeline,other_line)
        # Repeated creation/destruction exercises capsule ownership and exceptions.
        for _ in range(2000):
            x = TimeSet.from_json(a.to_json())
            y = (x | b) - a
            self.assertFalse(y)
        gc.collect()

    def test_native_boundary_checks(self):
        from time_set import _native
        with self.assertRaises(TypeError): _native.domain(object())
        with self.assertRaises(TypeError): _native.binary(self.line._handle,self.a._handle,"union",False)
        with self.assertRaises(TypeError): _native.make(self.line._handle,[(True,2)])
        self.assertEqual(self.a,self.line.range(10,20))

    def test_random_python_set_parity(self):
        rng = random.Random(91523)
        domain = Timeline(length=47,origin=None)
        for _ in range(500):
            a = {i for i in range(47) if rng.randrange(4) == 0}
            b = {i for i in range(47) if rng.randrange(4) == 0}
            left = TimeSet(domain,((i,i+1) for i in a))
            right = TimeSet(domain,((i,i+1) for i in b))
            for result, expected in [(left|right,a|b),(left&right,a&b),(left-right,a-b),(left^right,a^b)]:
                actual = {i for lo,hi in result for i in range(lo,hi)}
                self.assertEqual(actual,expected)
            self.assertEqual(left <= right,a <= b)
            self.assertEqual(left.overlaps(right),bool(a & b))

    def test_random_exact_units_and_dates(self):
        rng = random.Random(1225)
        for _ in range(1000):
            denominator = rng.choice([1,2,3,7,30,60,1000,1000000000])
            step = Fraction(rng.randrange(1,30),denominator)
            domain = Timeline(length=1000,tick=step,origin="2026-10-11T00:00:00.001")
            p = rng.randrange(1000)
            self.assertEqual(domain.position(domain.format(p)),p)
            self.assertEqual(domain.to_ticks(step * p),p)
            self.assertEqual(domain.point(p).duration,step)


if __name__ == "__main__":
    unittest.main(verbosity=2)
