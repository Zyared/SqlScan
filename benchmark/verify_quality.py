"""Reproduce the release measurements. All engines classify local strings. Python 3.11+ x64."""
import argparse
import csv
import ctypes
import hashlib
import io
import json
import re
import struct
import subprocess
import tempfile
import urllib.request
import zipfile
from collections import defaultdict
from pathlib import Path
from urllib.parse import urlsplit
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from evaluate import metrics

HERE = Path(__file__).resolve().parent
ENGINES = ("SqlScan 1.0", "SqlScan 2.0", "libinjection", "OWASP CRS 942")
CATEGORIES = {"patt_union": "UNION", "patt_time": "time-based", "patt_error": "error-based",
              "secs_boolean_blind": "boolean-blind", "secs_error_based": "error-based",
              "secs_time_blind": "time-based", "secs_stacked_queries": "stacked", "secs_inline_query": "inline"}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def fetch(key, source, cache, offline=False):
    path = cache / key
    if not path.exists():
        if offline:
            raise FileNotFoundError(f"Offline cache is missing {key}")
        with urllib.request.urlopen(source["url"], timeout=180) as response:
            data = response.read()
        if "member" in source:
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                data = archive.read(source["member"])
        if digest(data) != source["sha256"]:
            raise ValueError(f"Unexpected upstream SHA-256 for {key}")
        path.write_bytes(data)
    data = path.read_bytes()
    if digest(data) != source["sha256"]:
        raise ValueError(f"Unexpected cached SHA-256 for {key}")
    return data

def benign_group(text):
    if re.search(r"\b(select|insert|update|delete|from|where|table|order by)\b", text, re.I):
        return "SQL vocabulary"
    if re.search(r"[^\w\s.,-]", text):
        return "punctuation or markup"
    if re.fullmatch(r"[\d., +:-]+", text):
        return "numeric"
    return "other benign text"

def dataset(key, data):
    if key == "http" or key == "kaggle":
        records = []
        for row in csv.DictReader(io.StringIO(data.decode("utf-8-sig"), newline="")):
            if key == "http":
                if row["attack_type"] not in {"norm", "sqli"}:
                    continue
                text, label = row["payload"], int(row["attack_type"] == "sqli")
            else:
                text, label = row["Query"], int(row["Label"])
            records.append((text, label, "SQLi – unspecified" if label else benign_group(text)))
        return records
    if key in CATEGORIES:
        return [(s, 1, CATEGORIES[key]) for s in data.decode("utf-8-sig").splitlines()
                if s.strip() and not s.lstrip().startswith("#")]
    # CSIC source labels abnormal traffic across multiple attack types. Keep that split unlabelled for SQLi.
    raw = data.decode("latin-1").replace("\r\n", "\n")
    starts = list(re.finditer(r"^(GET|POST|PUT) (\S+) HTTP/1\.[01]$", raw, re.M))
    expected = 36000 if key == "csic_normal" else 25065
    if len(starts) != expected:
        raise ValueError(f"Unexpected CSIC record count {len(starts)}")
    records = []
    for i, m in enumerate(starts):
        uri = urlsplit(m[2])
        request = raw[m.end():starts[i+1].start() if i+1 < len(starts) else len(raw)]
        _, _, body = request.partition("\n\n")
        target = uri.path + ("?" + uri.query if uri.query else "")
        records.append((target + (" " + body.strip() if body.strip() else ""),
                        0 if key == "csic_normal" else None, m[1]))
    return records

def scores(labels, predicted):
    result = {"rows": len(labels), "flagged": sum(predicted)}
    if None not in labels:
        result.update(metrics(labels, predicted))
        if not any(labels):
            result["F1"] = None
        if all(labels):
            result["FPR"] = None
    return result

def evaluate(rows, temp, exe, baseline, crs, detect):
    src, dest = temp / "samples.csv", temp / "calls.csv"
    with src.open("w", encoding="utf-8", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(["text","label"])
        # Both CSV readers get the text and a blank second field. Keep truth labels with the evaluator.
        writer.writerows((s, "") for s,_,_ in rows)
    predictions = {}
    buffers = [s.encode("utf-8") for s,_,_ in rows]
    baseline_input = b"".join(struct.pack("<I",len(b)) + b for b in buffers)
    exact = subprocess.run([baseline], input=baseline_input, stdout=subprocess.PIPE, check=True).stdout.splitlines()
    for name, flags in zip(ENGINES[:2], (["--legacy"], [])):
        subprocess.run([exe,"--batch",src,"--out",dest,*flags], check=True)
        with dest.open(encoding="utf-8-sig",newline="") as f:
            parsed = list(csv.DictReader(f))
        if any(r["row"] != str(i) or r["label"] != "" for i,r in enumerate(parsed,1)):
            raise ValueError("Unexpected row order or nonempty prediction label")
        if flags and exact != [(r["predicted"] + "," + r["score"]).encode() for r in parsed]:
            raise RuntimeError("Legacy result differs from independently built v1.0")
        predictions[name] = [int(r["predicted"]) for r in parsed]
    predictions[ENGINES[2]] = [detect(b,len(b),ctypes.create_string_buffer(8)) for b in buffers]
    with src.open("rb") as f:
        values = subprocess.run([crs], stdin=f, stdout=subprocess.PIPE, check=True).stdout.splitlines()
    predictions[ENGINES[3]] = list(map(int,values))
    labels = [y for _,y,_ in rows]
    groups = defaultdict(list)
    for i,(_,_,group) in enumerate(rows):
        groups[group].append(i)
    output = {}
    for name, pred in predictions.items():
        if len(pred) != len(rows) or not set(pred) <= {0,1}:
            raise ValueError("Invalid predictions from " + name)
        output[name] = scores(labels,pred)
        output[name]["groups"] = {g:scores([labels[i] for i in ix],[pred[i] for i in ix]) for g,ix in groups.items()}
    return output

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", required=True, type=Path)
    parser.add_argument("--cache", type=Path, help="Reusable hash-verified corpus cache")
    parser.add_argument("--offline", action="store_true", help="Require local data and forbid downloads")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    kit = args.kit.resolve()
    exe, baseline = kit/"SqlScan.exe",kit/"validation"/"baseline.exe"
    crs, dll = kit/"validation"/"crs942.exe",kit/"benchmark"/"libinjection.dll"
    native = ctypes.CDLL(str(dll))
    detect = native.libinjection_sqli
    detect.argtypes = [ctypes.c_char_p,ctypes.c_size_t,ctypes.c_void_p]
    detect.restype = ctypes.c_int
    sources = json.loads((HERE/"sources.json").read_text(encoding="utf-8"))
    report = {"sha256":{f.name:digest(f.read_bytes()) for f in (exe,baseline,crs,dll)}, "datasets":{}}
    with tempfile.TemporaryDirectory(prefix="sqlscan-evaluation-") as work:
        temp = Path(work)
        cache = args.cache or temp
        cache.mkdir(exist_ok=True,parents=True)
        for key, source in sources.items():
            data = fetch(key,source,cache,args.offline)
            rows = dataset(key,data)
            report["datasets"][key] = evaluate(rows,temp,exe,baseline,crs,detect)
            print(key, len(rows), flush=True)
            if key == "http":
                ordered = sorted(enumerate(rows), key=lambda pair: digest(str(pair[0]).encode("ascii")))
                balanced = [row for label in (0,1) for row in [r for _,r in ordered if r[1]==label][:100]]
                report["datasets"]["http_balanced_200"] = evaluate(balanced,temp,exe,baseline,crs,detect)
    args.out.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    for key in ("http","kaggle"):
        results = report["datasets"][key]
        if results["SqlScan 2.0"]["F1"] < results["SqlScan 1.0"]["F1"]:
            raise SystemExit(f"F1 acceptance failed on {key}")

if __name__ == "__main__":
    main()
