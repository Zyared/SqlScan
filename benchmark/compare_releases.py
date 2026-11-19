"""Compare both shipped executables on identical unlabelled inputs; publish aggregate counts only."""
import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # Embedded Python runs with an isolated path.
from verify_quality import HERE, dataset, fetch, scores

PREVIOUS_SHA = "842cca258cfb3e0585ed36f346e69ed144dea1072fe82dd8f05dc3cbcddb4fd9"


def partition(text):
    shape = re.sub(r"[0-9]+", "0", " ".join(text.casefold().split()))
    key = hashlib.sha256(shape.encode("utf-8")).digest()[:8]
    return "held_out" if int.from_bytes(key, "big") % 5 == 0 else "development"


def predict(executable, source, result, size):
    subprocess.run([executable, "--batch", source, "--out", result], check=True)
    with result.open(encoding="utf-8-sig", newline="") as file:
        rows = list(csv.DictReader(file))
    if len(rows) != size or any(r["row"] != str(i) or r["label"] or r["predicted"] not in ("0", "1")
                                for i, r in enumerate(rows, 1)):
        raise ValueError("Invalid or unaligned predictions")
    return [(int(r["predicted"]), int(r["score"])) for r in rows]


def paired(labels, old, new):
    counts = Counter((y, a, b) for y, a, b in zip(labels, old, new, strict=True))
    return {
        "before": scores(labels, old), "after": scores(labels, new),
        "new_TP": counts[1, 0, 1], "lost_TP": counts[1, 1, 0],
        "new_FP": counts[0, 0, 1], "removed_FP": counts[0, 1, 0],
        "new_unlabelled_alerts": counts[None, 0, 1], "removed_unlabelled_alerts": counts[None, 1, 0],
    }


# Descriptive groups for previously missed Kaggle rows. These categories do not set the verdict.
GROUPS = [
    ("constant comparison", r"""\b(?:or|and)\s*\(*\s*(?:[0-9]+|['"][a-z0-9 _]*['"])\s*(?:=|<>|!=|<|>)\s*(?:[0-9]+|['"][a-z0-9 _]*['"])"""),
    ("timing expressions", r"\b(?:(?:pg_)?sleep|benchmark|dbms_pipe\.receive_message)\s*\(|\bwaitfor\s+delay\b"),
    ("set operations", r"\bunion\s+(?:(?:all|distinct)\s+)?select\b"),
    ("diagnostic functions", r"\b(?:updatexml|extractvalue)\s*\("),
    ("catalog references", r"\b(?:information_schema|sysobjects|sqlite_master)\b"),
    ("nested query", r"\(\s*select\b"),
    ("comment markers", r"--|/\*|\*/"),
    ("other SQL vocabulary", r"\b(?:select|insert|update|delete|where|from)\b"),
    ("other logical operators", r"\b(?:or|and)\b"),
]
GROUPS = [(name, re.compile(expr, re.I)) for name, expr in GROUPS]


def compare(rows, previous, current, work, kaggle):
    source = work / "inputs.csv"
    with source.open("w", encoding="utf-8", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["text"])
        writer.writerows((text,) for text, _, _ in rows)
    before = predict(previous, source, work / "previous.csv", len(rows))
    after = predict(current, source, work / "current.csv", len(rows))
    labels = [label for _, label, _ in rows]
    old = [flag for flag, _ in before]
    new = [flag for flag, _ in after]
    result = paired(labels, old, new)
    if kaggle:
        splits = [partition(text) for text, _, _ in rows]
        for group in ("development", "held_out"):
            idx = [i for i, part in enumerate(splits) if part == group]
            result[group] = paired([labels[i] for i in idx], [old[i] for i in idx], [new[i] for i in idx])
        missed = {}
        score_hist = Counter()
        cohort = {}
        nested_normals = [i for i, (text, label, _) in enumerate(rows)
                          if label == 0 and re.search(r"\(\s*select\b", text, re.I)]
        result["labelled_normal_with_subquery"] = paired([labels[i] for i in nested_normals],
                                                         [old[i] for i in nested_normals],
                                                         [new[i] for i in nested_normals])
        for (text, label, _), (pred, score), (now, _) in zip(rows, before, after, strict=True):
            if label == 1 and pred == 0:
                group = next((name for name, expr in GROUPS if expr.search(text)), "other")
                bucket = missed.setdefault(group, {"before_FN": 0, "recovered": 0, "remaining": 0})
                bucket["before_FN"] += 1
                bucket["recovered" if now else "remaining"] += 1
                score_hist[score] += 1
                if group == "nested query":
                    if re.search(r"\bcase\s+when\b", text, re.I):
                        form = "conditional expression"
                    elif re.search(r"\b(?:count|sum|max|min|avg)\s*\(", text, re.I):
                        form = "aggregate expression"
                    elif re.search(r"\b(?:chr|char|concat|concat_ws)\s*\(", text, re.I):
                        form = "string expression"
                    else:
                        form = "other subquery"
                    for key in (form, partition(text), "all"):
                        tally = cohort.setdefault(key, {"previous_misses": 0, "recovered": 0, "remaining": 0})
                        tally["previous_misses"] += 1
                        tally["recovered" if now else "remaining"] += 1
        if cohort.get("all", {}).get("previous_misses") != 1740:
            raise ValueError("Nested baseline cohort differs from the 2.0.2 report")
        result["nested_miss_cohort"] = cohort
        result["previous_misses_by_first_matching_group"] = missed
        result["previous_misses_by_total_score"] = dict(sorted(score_hist.items()))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, required=True)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--offline", action="store_true", help="Require hash-verified local data")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    kit = args.kit.resolve()
    previous = kit / "validation/SqlScan-2.0.2.exe"
    current = kit / "SqlScan.exe"
    hashes = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in (previous, current)}
    if hashes[previous.name] != PREVIOUS_SHA:
        raise ValueError("Expected the published 2.0.2 binary")
    result = {"sha256": hashes, "datasets": {}}
    sources = json.loads((HERE / "sources.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        cache = args.cache or work
        cache.mkdir(exist_ok=True, parents=True)
        for key, source in sources.items():
            rows = dataset(key, fetch(key, source, cache, args.offline))
            result["datasets"][key] = compare(rows, previous, current, work, key == "kaggle")
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if any(r["new_FP"] or r["lost_TP"] for r in result["datasets"].values()):
        raise SystemExit("Paired regression: new false alarm or lost labelled detection")
    if not result["datasets"]["kaggle"]["nested_miss_cohort"]["held_out"]["recovered"]:
        raise SystemExit("No improvement on reserved nested cases")
    if not result["datasets"]["kaggle"]["held_out"]["new_TP"]:
        raise SystemExit("No improvement on the reserved Kaggle partition")
    print("Paired release comparison passed")


if __name__ == "__main__":
    main()
