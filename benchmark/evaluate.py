"""Score a labeled dataset offline. Only aggregate results are retained."""
import argparse
import csv
import ctypes
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

SOURCES = {
    "d8015e256ce4499c8ac7a8be8dd25de6487cf018601edf1a03a408a2c8f0e39d":
        "HttpParamsDataset / payload_test.csv @ 926670a710283f87c05b554680facf3f9530548c",
    "60af1d160dcc5c3f61440dc8f0c019e8898243951e283402857c10294c6f5cc5":
        "Kaggle sajid576/sql-injection-dataset v1 / Modified_SQL_Dataset.csv",
}


def metrics(labels, predictions):
    counts = dict.fromkeys(("TP", "FP", "TN", "FN"), 0)
    for expected, predicted in zip(labels, predictions, strict=True):
        counts[{(1, 1): "TP", (0, 1): "FP", (0, 0): "TN", (1, 0): "FN"}[expected, predicted]] += 1
    tp, fp, tn, fn = (counts[key] for key in ("TP", "FP", "TN", "FN"))
    def ratio(numerator, denominator):
        return round(numerator / denominator, 6) if denominator else None
    return dict(**counts, precision=ratio(tp, tp + fp), recall=ratio(tp, tp + fn),
                FPR=ratio(fp, fp + tn), F1=ratio(2 * tp, 2 * tp + fp + fn))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True, help="HttpParamsDataset or Kaggle SQL Injection Dataset")
    parser.add_argument("--reference", type=Path, required=True, help="x64 libinjection DLL")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    with args.dataset.open(encoding="utf-8-sig", newline="") as src:
        reader = csv.DictReader(src)
        if {"payload", "attack_type"}.issubset(reader.fieldnames or []):
            data = [(r["payload"], int(r["attack_type"] == "sqli"))
                    for r in reader if r["attack_type"] in {"sqli", "norm"}]
        else:
            data = [(r["Query"], int(r["Label"])) for r in reader]
    if not data:
        raise ValueError("The test split has no labeled SQLi or benign records")
    labels = [label for _, label in data]
    digest = hashlib.sha256(args.dataset.read_bytes()).hexdigest()
    report = {"source": SOURCES.get(digest, args.dataset.name),
              "reference_sha256": hashlib.sha256(args.reference.read_bytes()).hexdigest(),
              "dataset_sha256": digest,
              "exe_sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest(), "rows": len(data)}
    with tempfile.TemporaryDirectory(prefix="sqlscan-metrics-") as folder:
        batch = Path(folder) / "input.csv"
        with batch.open("w", newline="", encoding="utf-8") as dest:
            writer = csv.writer(dest)
            writer.writerow(["text", "label"])
            writer.writerows(data)
        for name, flags in [("SqlScan 1.0", ["--legacy"]), ("SqlScan 2.0", [])]:
            result = Path(folder) / "result.csv"
            subprocess.run([args.exe.resolve(), "--batch", batch, "--out", result, *flags], check=True)
            with result.open(encoding="utf-8-sig", newline="") as output:
                rows = list(csv.DictReader(output))
                predictions = [int(r["predicted"]) for r in rows]
            report[name] = metrics(labels, predictions)
            missed = [r for label, prediction, r in zip(labels, predictions, rows, strict=True)
                      if label and not prediction]
            report[name]["FN_without_hits"] = sum(r["hits"] == "0" for r in missed)
            report[name]["FN_below_threshold"] = sum(r["hits"] != "0" for r in missed)
    lib = ctypes.CDLL(str(args.reference.resolve()))
    detect = lib.libinjection_sqli
    detect.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
    detect.restype = ctypes.c_int
    predictions = []
    for text, _ in data:
        raw = text.encode("utf-8")
        verdict = detect(raw, len(raw), ctypes.create_string_buffer(8))
        if verdict not in (0, 1):
            raise RuntimeError("Reference detector returned an error")
        predictions.append(verdict)
    report["libinjection"] = metrics(labels, predictions)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
