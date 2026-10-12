"""The same integration tests run against the executable and Python CLI."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class CLITest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="time_set_tests_")
        cls.directory = Path(cls.temp.name)
        cls.command = [sys.executable,"-m","time_set"] if os.environ.get("TIME_SET_PYTHON_CLI") else [os.environ["TIME_SET_CLI"]]
        cls.a = cls.directory / "a with spaces.json"
        cls.b = cls.directory / "b.json"
        cls.a.write_text(cls.document([[10,20]]),encoding="utf-8")
        cls.b.write_text(cls.document([[15,26]]),encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def document(intervals, length=100, tick="1s", origin="2000-01-01T00:00:00"):
        return json.dumps({"timeline":{"origin":origin,"tick":tick,"length":length},"intervals":intervals})

    def run_cli(self, *args, data="", error=False):
        result = subprocess.run(self.command + [str(x) for x in args], input=data,
                                text=True,capture_output=True,timeout=15)
        self.assertEqual(result.returncode,2 if error else 0,(args,result.stdout,result.stderr))
        if error:
            self.assertTrue(result.stderr.startswith("time_set:"),result.stderr)
            self.assertEqual(result.stdout, "", result.stdout)
        else:
            self.assertEqual(result.stderr, "", result.stderr)
        return result.stdout.strip()

    def intervals(self, *args, data=""):
        return json.loads(self.run_cli(*args,data=data))["intervals"]

    def test_help(self):
        self.assertIn("No --format option", self.run_cli("--help"))
        self.assertIn("snapshot", self.run_cli("move","--help").lower())
        self.assertIn("count", self.run_cli("help","repeat"))
        self.assertIn("time_set", self.run_cli())

    def test_make(self):
        output = json.loads(self.run_cli("make","--length",100,"[10,20)","[20,25]","{50}"))
        self.assertEqual(output["intervals"],[[10,26],[50,51]])
        self.assertEqual(output["timeline"]["length"],100)
        self.assertEqual(self.intervals("make"),[])
        self.assertEqual(self.intervals("make","--length",10,"--full"),[[0,10]])
        self.assertEqual(self.intervals("make","--length",0,"--full"),[])

    def test_custom_timeline(self):
        value = json.loads(self.run_cli("make","--length","10s","--tick","1ms","--origin","2026-10-11","[1s,2s)"))
        self.assertEqual(value["timeline"],{"origin":"2026-10-11T00:00:00","tick":"1/1000s","length":10000})
        self.assertEqual(value["intervals"],[[1000,2000]])
        indexed = json.loads(self.run_cli("make","--origin","none","--length=10","{1}"))
        self.assertIsNone(indexed["timeline"]["origin"])

    def test_all_boundaries(self):
        for spec, expected in {"[10,13)":"[10,13)","[10,13]":"[10,14)","(10,13)":"[11,13)","(10,13]":"[11,14)","{10}":"[10,11)","[10,10)":"empty"}.items():
            with self.subTest(spec=spec):
                self.assertEqual(self.run_cli("make","--length",20,spec,"--text"),expected)

    def test_algebra(self):
        expected = {"union":[[10,26]],"intersect":[[15,20]],"subtract":[[10,15]],"xor":[[10,15],[20,26]]}
        for command,result in expected.items():
            self.assertEqual(self.intervals(command,self.a,self.b),result)
        self.assertEqual(self.intervals("subtract",self.a,self.b,self.a),[])
        self.assertEqual(self.intervals("complement",self.a),[[0,10],[20,100]])

    def test_stdin_and_text(self):
        self.assertEqual(self.run_cli("show",data=self.a.read_text()),"[10,20)")
        self.assertEqual(self.intervals("intersect","-",self.b,data=self.a.read_text()),[[15,20]])
        self.assertEqual(self.run_cli("shift","--by","5t","--text",data=self.a.read_text()),"[15,25)")
        shifted = self.run_cli("shift",self.a,"--by","5t")
        self.assertEqual(self.run_cli("starts","--duration","4t","--text",data=shifted),"[15,22)")
        self.run_cli("union","-","-",data=self.a.read_text(),error=True)

    def test_shift_clips(self):
        self.assertEqual(self.intervals("shift",self.a,"--by=-15t"),[[0,5]])
        self.assertEqual(self.intervals("shift",self.a,"--by",95),[])
        self.assertEqual(self.intervals("shift",self.a,"--by",-(1<<63)),[])
        self.assertEqual(self.intervals("shift",self.a,"--by",(1<<63)-1),[])

    def test_fill_clear(self):
        self.assertEqual(self.intervals("fill",self.a,"[20,30)"),[[10,30]])
        self.assertEqual(self.intervals("fill","[20,30)",data=self.a.read_text()),[[10,30]])
        self.assertEqual(self.intervals("clear",self.a,"[12,18)"),[[10,12],[18,20]])
        self.assertEqual(self.intervals("clear","[12,18)",data=self.a.read_text()),[[10,12],[18,20]])
        self.assertEqual(self.intervals("clear",self.a),[])
        self.assertEqual(self.intervals("clear",data=self.a.read_text()),[])
        self.assertEqual(self.intervals("fill",self.a,"[25,25]"),[[10,20],[25,26]])

    def test_replacement_overlay_and_empty_sector(self):
        value = self.document([[10,15],[47,50]])
        self.assertEqual(self.intervals("copy","[10,20)","--to",40,data=value),[[10,15],[40,45]])
        self.assertEqual(self.intervals("copy","[10,20)","--to",40,"--overlay",data=value),[[10,15],[40,45],[47,50]])
        self.assertEqual(self.intervals("copy","[0,10)","--to",40,data=value),[[10,15]])
        self.assertEqual(self.intervals("copy","[0,10)","--to",40,"--overlay",data=value),[[10,15],[47,50]])

    def test_moves_and_clipping(self):
        value = self.document([[10,13],[15,18],[24,27]])
        self.assertEqual(self.intervals("move","[10,20)","--to",15,data=value),[[15,18],[20,23],[25,27]])
        self.assertEqual(self.intervals("move",self.a,"[10,20)","--to",95,"--clip"),[[95,100]])
        self.run_cli("move",self.a,"[10,20)","--to",95,error=True)
        self.assertEqual(self.intervals("move",self.a,"[10,20)","--to",-5,"--clip"),[[0,5]])

    def test_cross_set_moves(self):
        before = self.a.read_bytes(), self.b.read_bytes()
        copied = self.intervals("copy",self.a,"[10,20)","--to",40,"--into",self.b)
        self.assertEqual(copied,[[15,26],[40,50]])
        bundle = self.run_cli("move",self.a,"[10,20)","--to",40,"--into",self.b)
        decoded = json.loads(bundle)
        self.assertEqual(decoded["source"]["intervals"],[])
        self.assertEqual(decoded["destination"]["intervals"],[[15,26],[40,50]])
        self.assertEqual(self.intervals("select","source",data=bundle),[])
        self.assertEqual(self.intervals("select","destination",data=bundle),copied)
        self.assertEqual(before,(self.a.read_bytes(),self.b.read_bytes()))

    def test_repeats(self):
        self.assertEqual(self.intervals("repeat",self.a,"--every",15,"--count",3),[[10,20],[25,35],[40,50]])
        self.assertEqual(self.intervals("repeat",self.a,"--every",5,"--count",3),[[10,30]])
        self.assertEqual(self.intervals("repeat",self.a,"--every",15,"--count",0),[])
        self.assertEqual(self.intervals("repeat",self.a,"--every",15,"--until",30),[[10,20],[25,30]])
        self.assertEqual(self.intervals("repeat",self.a,"--every",1,"--count",(1<<63)-1),[[10,100]])
        for options in [("--count",-1),("--count",1,"--until",30),(),("--count","2s")]:
            self.run_cli("repeat",self.a,"--every",1,*options,error=True)

    def test_starts(self):
        self.assertEqual(self.intervals("starts",self.a,"--duration",4),[[10,17]])
        self.assertEqual(self.intervals("starts",self.a,"--duration",10),[[10,11]])
        self.assertEqual(self.intervals("starts",self.a,"--duration",11),[])
        self.run_cli("starts",self.a,"--duration",0,error=True)

    def test_queries(self):
        self.assertEqual(self.run_cli("contains",self.a,10),"true")
        self.assertEqual(self.run_cli("contains",self.a,20),"false")
        self.assertEqual(self.run_cli("contains",20,data=self.a.read_text()),"false")
        self.assertEqual(self.run_cli("subset",self.a,self.a),"true")
        self.assertEqual(self.run_cli("subset",self.a,self.a,"--proper"),"false")
        self.assertEqual(self.run_cli("equal",self.a,self.a),"true")
        self.assertEqual(self.run_cli("overlaps",self.a,self.b),"true")
        self.assertEqual(self.run_cli("subset",self.a,self.b),"false")

    def test_dates(self):
        encoded = self.run_cli("make","--length","1d","--tick","1min","--origin","2026-10-12","[9h,17h)")
        self.assertEqual(self.run_cli("show","--dates",data=encoded),"[2026-10-12T09:00:00,2026-10-12T17:00:00)")
        self.assertEqual(self.intervals("make","--length","1d","--origin","2026-10-12","[2026-10-12T09:00:00,2026-10-12T10:00:00)"),[[32400,36000]])
        self.run_cli("show","--dates",data=self.document([],origin=None),error=True)

    def test_statistics(self):
        stats = json.loads(self.run_cli("stats",self.a))
        self.assertEqual(stats,{"tick_count":10,"interval_count":1,"duration":"10s","duration_overflow":False,"empty":False})
        self.assertIn("covered: 10 ticks",self.run_cli("stats",self.a,"--text"))
        huge = self.document([[0,2]],length=2,tick=f"{(1<<63)-1}s")
        stats = json.loads(self.run_cli("stats",data=huge))
        self.assertTrue(stats["duration_overflow"])
        self.assertIsNone(stats["duration"])
        result = self.run_cli("stats","--text",data=huge)
        self.assertEqual(result.count("duration:"),1)

    def test_64_bit_exactness(self):
        n = (1<<63)-1
        self.assertEqual(self.intervals("make","--length",n,f"[{n-1},{n-1}]"),[[n-1,n]])
        self.run_cli("make","--length",n,f"[0,{n}]",error=True)
        self.run_cli("make","--length",n+1,error=True)

    def test_bad_inputs(self):
        for args in [("wat",),("make","--format","text"),("make","--length",10,"--length",10),
                     ("make","--length",10,"--full","[0,1)"),("make","--tick","0s"),
                     ("make","--length","0.5s"),("make","--overlay"),("shift",self.a),
                     ("show",self.a,"extra"),("union",self.a),("make","[2,1)"),
                     ("make","--text","--text"),("make","--text=yes"),
                     ("show",self.directory / "missing.json")]:
            with self.subTest(args=args): self.run_cli(*args,error=True)
        for value in ["", "[]", self.a.read_text()+"x", self.document([[1.0,2]]), self.document([[True,2]])]:
            self.run_cli("show",data=value,error=True)
        mismatch = self.directory / "mismatch.json"
        mismatch.write_text(self.document([],tick="1ms"))
        self.run_cli("union",self.a,mismatch,error=True)
        self.assertEqual(self.run_cli("equal",self.a,mismatch),"false")


if __name__ == "__main__":
    unittest.main(verbosity=2)
