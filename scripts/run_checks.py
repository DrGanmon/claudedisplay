"""Run the firmware's native regression checks without attached hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="claudedisplay-tests-") as directory:
    for source in sorted((root / "tests").glob("*_test.cpp")):
        binary = Path(directory) / source.stem
        subprocess.run([
            "g++", "-std=c++11", "-Wall", "-Wextra", "-pedantic",
            "-I", str(root / ".pio/libdeps/d1_mini/ArduinoJson/src"),
            str(source), "-o", str(binary),
        ], check=True, cwd=root)
        subprocess.run([str(binary)], check=True, cwd=root)
        print(f"OK: {source.name}", flush=True)
for source in sorted((root / "tests").glob("*_test.py")):
    subprocess.run([sys.executable, str(source)], check=True, cwd=root)
