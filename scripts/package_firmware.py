"""Package only explicitly selected build outputs, never device configuration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def package(output):
    root = Path(__file__).resolve().parents[1]
    match = re.search(r'constexpr char FIRMWARE_VERSION\[\] = "([^"]+)";',
                      (root / "src/main.cpp").read_text())
    if not match:
        raise SystemExit("FIRMWARE_VERSION fehlt")
    version = match.group(1)
    tag = os.environ.get("GITHUB_REF_NAME") if os.environ.get("GITHUB_REF_TYPE") == "tag" else None
    if tag and tag.startswith("v") and tag != f"v{version}":
        raise SystemExit(f"Release-Tag {tag} passt nicht zur Firmware-Version v{version}")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    files = {
        "firmware-d1-mini.bin": root / ".pio/build/d1_mini/firmware.bin",
        "firmware-d1-mini.elf": root / ".pio/build/d1_mini/firmware.elf",
    }
    for path in files.values():
        if not path.is_file() or not path.stat().st_size:
            raise SystemExit(f"Build-Ausgabe fehlt oder ist leer: {path}")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise SystemExit("Paket-Ziel muss leer sein, damit keine fremden Dateien veröffentlicht werden")
    hashes = {}
    for name, path in files.items():
        shutil.copyfile(path, output / name)
        hashes[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
    manifest = {
        "version": version,
        "commit": commit,
        "tag": tag,
        "environment": "d1_mini",
        "board": "WeMos D1 mini / ESP8266",
        "sha256": hashes,
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    hashes["manifest.json"] = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    (output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    print(f"Firmware {version} ({commit[:8]}) bereitgestellt in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("dist"))
    package(parser.parse_args().output)
