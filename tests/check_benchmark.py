"""Compare all results with the reference and identify the exact executable under test."""
import argparse
import hashlib
import json
from pathlib import Path


def verify(kit, reference, observed):
    expected = json.loads(reference.read_text(encoding="utf-8"))
    actual = json.loads(observed.read_text(encoding="utf-8"))
    with (kit / "SqlScan.exe").open("rb") as executable:
        digest = hashlib.file_digest(executable, "sha256").hexdigest()
    manifest = json.loads((kit / "release.json").read_text(encoding="utf-8-sig"))
    if digest != manifest["sha256"]["SqlScan.exe"] or digest != actual["sha256"]["SqlScan.exe"]:
        raise ValueError("The result does not identify the tested executable")
    # A rebuild has a new binary digest. Every comparator digest and every measurement
    # must still agree with the independent reference, including individual transitions.
    expected["sha256"]["SqlScan.exe"] = digest
    if actual != expected:
        raise ValueError(f"Measurements or reference detectors differ from {reference.name}")
    print(f"{observed.name} matches {reference.name}; tested executable {digest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("kit", "reference", "observed"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    verify(args.kit, args.reference, args.observed)
