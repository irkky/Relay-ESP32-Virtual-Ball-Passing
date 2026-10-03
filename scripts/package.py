"""Create a source-only handoff archive, excluding local toolchains and build caches."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

root = Path(__file__).resolve().parents[1]
destination = root / "dist"
destination.mkdir(exist_ok=True)
files = [root / name for name in ("README.md", "platformio.ini", ".gitignore")]
for folder in ("config", "firmware", "dashboard", "docs", "scripts", "tests"):
    files.extend(p for p in (root / folder).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
archive = destination / "VirtualBallProject.zip"
with ZipFile(archive, "w", ZIP_DEFLATED) as z:
    for path in sorted(files):
        z.write(path, Path("VirtualBallProject") / path.relative_to(root))
with ZipFile(archive) as z:
    assert z.testzip() is None
print(f"Created {archive.name}: {len(files)} source/documentation files, {archive.stat().st_size:,} bytes")
