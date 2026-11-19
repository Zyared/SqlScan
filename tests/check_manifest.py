"""Verify every listed file without loading application or test code from the kit."""
import hashlib
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
seen = set()
for line in (root / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
    expected, name = line.split("  ", 1)
    path = (root / name).resolve()
    if not path.is_relative_to(root) or name in seen:
        raise ValueError("Invalid manifest path")
    seen.add(name)
    with path.open("rb") as file:
        if hashlib.file_digest(file, "sha256").hexdigest() != expected:
            raise ValueError(f"Checksum mismatch: {name}")
if not {"SqlScan.exe","source/docs/TZ_SqlScan_2.0.md","validation/Acceptance.exe"}.issubset(seen):
    raise ValueError("Incomplete package")
print(f"Verified {len(seen)} files")
