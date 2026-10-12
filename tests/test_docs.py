"""Compile/run the marked, self-contained README examples verbatim."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
blocks = re.findall(r"<!-- verify: (\w+) -->\s*```[^\n]*\n(.*?)\n```", (root / "README.md").read_text(), re.S)
if len(blocks) != 3:
    raise SystemExit("Expected the C++, Python, and shell README examples")
cli = Path(os.environ["TIME_SET_CLI"])
compiler = os.environ["TIME_SET_CXX"]
env = os.environ.copy()
env["PATH"] = str(cli.parent) + os.pathsep + env.get("PATH", "")
has_python = env.get("TIME_SET_HAS_PYTHON") == "ON"
count = 0
with tempfile.TemporaryDirectory(prefix="time_set_readme_") as temporary:
    directory = Path(temporary)
    for language, code in blocks:
        if language == "python" and not has_python:
            print("README Python example skipped in C++-only build")
            continue
        if language == "cpp":
            source = directory / "example.cpp"
            executable = directory / ("example.exe" if os.name == "nt" else "example")
            source.write_text(code)
            if Path(compiler).name.lower() in {"cl", "cl.exe"}:
                command = [compiler,"/std:c++20","/EHsc",f"/I{root / 'include'}",str(source),f"/Fe{executable}"]
            else:
                command = [compiler,"-std=c++20","-Wall","-Wextra","-Wpedantic","-Werror",
                           "-I",str(root / "include"),str(source),"-o",str(executable)]
            subprocess.run(command,check=True,cwd=directory,env=env,timeout=60)
            result = subprocess.run([str(executable)],check=True,capture_output=True,text=True,env=env,timeout=15)
            assert result.stdout.strip() == "[10,17)", result.stdout
        elif language == "python":
            subprocess.run([sys.executable,"-c",code],check=True,cwd=directory,env=env,timeout=15)
        elif language == "bash":
            bash = shutil.which("bash")
            if not bash:
                print("README shell example skipped: bash is not installed")
                continue
            result = subprocess.run([bash,"-c","set -euo pipefail\n"+code],check=True,
                                    capture_output=True,text=True,cwd=directory,env=env,timeout=15)
            assert result.stdout.strip() == "[15,22)", result.stdout
        else:
            raise AssertionError(f"Unsupported documentation language: {language}")
        print(f"README {language} example passed")
        count += 1
print(f"{count} README examples passed verbatim")
