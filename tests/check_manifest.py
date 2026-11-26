"""Verify the kit using the same manifest checks as the installer builder."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "deploy"))
from manifest import verified_files


if __name__ == "__main__":
    print(f"Verified {len(verified_files(Path(sys.argv[1])))} files")
