"""Read and verify the SHA256SUMS file shared by portable and MSI packages."""
import hashlib
from pathlib import Path, PurePosixPath


def verified_files(root: Path) -> list[str]:
    root = root.resolve()
    files = []
    seen = set()
    for entry in (root / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected, name = entry.split("  ", 1)
        relative = PurePosixPath(name)
        path = (root / relative).resolve()
        if (relative.as_posix() != name or "\\" in name or ":" in name or ".." in relative.parts
                or not path.is_relative_to(root) or path in seen):
            raise ValueError(f"Invalid or duplicate package path: {name}")
        with path.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
                raise ValueError(f"Checksum mismatch: {name}")
        seen.add(path)
        files.append(name)
    required = {"SqlScan.exe", "source/docs/TZ_SqlScan_2.0.md", "validation/Acceptance.exe"}
    if not required.issubset(files):
        raise ValueError("Incomplete package")
    return files
