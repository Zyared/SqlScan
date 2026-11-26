"""Check row-for-row parity with the published SqlScan 2.0.3 on hash-verified offline inputs."""
import argparse
import csv
import hashlib
import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / "tests")]  # Support the embedded Python runtime.
from verify_quality import dataset, fetch
from benign_subqueries import QUERIES

PREVIOUS_SHA256 = "83e1fa755f386656651b821ddd7348045dfe4df9c8d579d4b88f0f6033c5b9e0"
FIELDS = ("predicted", "score", "risk", "hits", "truncated")
COLUMNS = ["row", "label", "predicted", "score", "risk", "hits", "truncated"]


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def compare(previous, current, rows, work):
    source = work / "samples.csv"
    with source.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["text", "label"])
        writer.writerows((text, "") for text, _, _ in rows)

    old_csv, new_csv = work / "old.csv", work / "new.csv"
    for executable, output in ((previous, old_csv), (current, new_csv)):
        subprocess.run([executable, "--batch", source, "--out", output], check=True)

    field_changes = dict.fromkeys(FIELDS, 0)
    changed = previous_alerts = current_alerts = previous_fp = current_fp = 0
    with old_csv.open(encoding="utf-8-sig", newline="") as old, new_csv.open(encoding="utf-8-sig", newline="") as new:
        before, after = csv.DictReader(old), csv.DictReader(new)
        if before.fieldnames != COLUMNS or after.fieldnames != COLUMNS:
            raise ValueError("Unexpected batch output columns")
        for number, ((_, label, _), left, right) in enumerate(zip(rows, before, after, strict=True), 1):
            for row in (left, right):
                if (row["row"] != str(number) or row["label"] != "" or row["predicted"] not in ("0", "1")
                        or None in row or any(value is None for value in row.values())):
                    raise ValueError(f"Malformed or unaligned batch output at row {number}")
            for field in FIELDS:
                field_changes[field] += left[field] != right[field]
            changed += left != right
            old_flag = left["predicted"] == "1"
            new_flag = right["predicted"] == "1"
            previous_alerts += old_flag
            current_alerts += new_flag
            previous_fp += old_flag and label == 0
            current_fp += new_flag and label == 0
    return {
        "batch_input_sha256": sha256(source), "rows": len(rows), "changed_rows": changed,
        "field_changes": field_changes, "previous_alerts": previous_alerts, "current_alerts": current_alerts,
        "previous_FP": previous_fp, "current_FP": current_fp,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous", type=Path, required=True, help="Published 2.0.3 executable")
    parser.add_argument("--kit", type=Path, required=True, help="Directory containing SqlScan.exe under test")
    parser.add_argument("--cache", type=Path, required=True, help="Previously populated benchmark cache")
    parser.add_argument("--out", type=Path, required=True, help="Aggregate JSON report")
    args = parser.parse_args()
    previous, current = args.previous.resolve(), (args.kit / "SqlScan.exe").resolve()
    manifest = HERE / "sources.json"
    hashes = {"SqlScan-2.0.3.exe": sha256(previous), "SqlScan.exe": sha256(current), "sources.json": sha256(manifest)}
    if hashes["SqlScan-2.0.3.exe"] != PREVIOUS_SHA256:
        raise ValueError("Wrong reference executable: expected the published SqlScan 2.0.3")

    sources = json.loads(manifest.read_text(encoding="utf-8"))
    report = {"sha256": hashes, "datasets": {}, "controls": {}}
    with tempfile.TemporaryDirectory(prefix="sqlscan-parity-") as temp:
        work = Path(temp)
        for name, spec in sources.items():
            raw = fetch(name, spec, args.cache, offline=True)
            rows = dataset(name, raw)
            result = compare(previous, current, rows, work)
            report["datasets"][name] = {"source_sha256": spec["sha256"], **result}
            if name == "http":
                ordered = sorted(enumerate(rows), key=lambda item: hashlib.sha256(str(item[0]).encode("ascii")).digest())
                balanced = [record for label in (0, 1) for record in [row for _, row in ordered if row[1] == label][:100]]
                report["controls"]["http_balanced_200"] = compare(previous, current, balanced, work)

        ordinary = [(variant, 0, family) for family, query in QUERIES.items()
                    for variant in (query, query.replace(" ", "\n  ", 1), query + "\n;")]
        report["controls"]["ordinary_sql"] = compare(previous, current, ordinary, work)

    if sha256(previous) != hashes["SqlScan-2.0.3.exe"] or sha256(current) != hashes["SqlScan.exe"]:
        raise ValueError("Executable changed during comparison")
    datasets = report["datasets"].values()
    field_totals = Counter()
    for result in datasets:
        field_totals.update(result["field_changes"])
    report["corpus_totals"] = {
        key: sum(result[key] for result in datasets)
        for key in ("rows", "changed_rows", "previous_alerts", "current_alerts", "previous_FP", "current_FP")
    }
    report["corpus_totals"]["field_changes"] = dict(field_totals)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    total = report["corpus_totals"]
    control_changes = sum(result["changed_rows"] for result in report["controls"].values())
    print(f"Compared {total['rows']} corpus rows: {total['changed_rows']} changed; additional controls: {control_changes} changed.")
    if total["changed_rows"] or control_changes:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
