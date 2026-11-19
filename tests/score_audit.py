"""Independent arithmetic verifier for the shipped binary. Only local text and published weights are used."""
import argparse
import csv
import hashlib
import html
import json
import random
import re
import secrets
import subprocess
import tempfile
from pathlib import Path

def read_weights(schema):
    text = schema.read_text(encoding="utf-8").split("INSERT INTO sqlscan.rule (category_id, pattern",1)[1]
    values = text.split("FROM (VALUES",1)[1].split(") AS seed",1)[0]
    rows = re.findall(r"\('([^']+)',\s*'((?:''|[^'])*)',\s*(\d+),\s*(true|false)\)",values)
    if len(rows) != 18:
        raise ValueError("Review the independent seed-rule table")
    return [(pattern.replace("''","'"),int(weight),flag=="true") for _,pattern,weight,flag in rows]

def reference(normalized_lines,rules):
    score = hits = 0
    triggered = critical = False
    boundary = r"[a-z0-9_$\x80-\U0010ffff]"
    extra = {
        "sleep(": rf"(?<!{boundary})(?:pg_)?sleep \( ?(?=[0-9])",
        "benchmark(": rf"(?<!{boundary})benchmark \( ?(?=[0-9])",
        "union select": rf"(?<!{boundary})union (?:all|distinct) select null ?, ?null(?!{boundary})",
    }
    for line in normalized_lines:
        subtotal = 0
        high = False
        for pattern, weight, flag in rules:
            count = len(re.findall("(?=" + re.escape(pattern) + ")",line))
            if pattern in extra:
                count += len(re.findall(extra[pattern], line))
            hits += count
            subtotal += weight*count
            high |= flag and count>0
        score += subtotal
        triggered |= high or subtotal>=5
        critical |= subtotal>=10
    risk = "Критический" if critical else "Высокий" if triggered else "Средний" if score else "Низкий"
    return score,hits,int(triggered),risk

def verify(cases, rows):
    for i,((_,expect,name),r) in enumerate(zip(cases,rows,strict=True),1):
        actual=(int(r["score"]),int(r["hits"]),int(r["predicted"]),r["risk"])
        if actual != expect:
            raise ValueError(f"Row {i} ({name}): expected {expect}, observed {actual}")

def write_html(path, report):
    esc=lambda value:html.escape(str(value))
    body=[]
    for e in report["hand_results"]:
        body.append("<tr>"+"".join("<td>"+esc(v)+"</td>" for v in
                    (e["example"],e["input"] or "∅",e["score"],e["observed_score"],e["hits"],e["observed_hits"],
                     "да" if e["observed_verdict"] else "нет",e["observed_risk"]))+"</tr>")
    page = ('<!doctype html><html lang="ru"><meta charset="utf-8"><title>Проверка баллов SqlScan</title>'
            '<style>body{font:16px system-ui;max-width:70rem;margin:3rem auto;color:#1e2930}td,th{padding:.5rem;'
            'border-bottom:1px solid #ccc;text-align:left;white-space:pre-wrap}table{border-collapse:collapse}'
            'code{overflow-wrap:anywhere}</style><h1>Проверка баллов SqlScan</h1>'
            f'<p>Число для воспроизведения выборки: <code>{report["seed"]}</code>.</p>'
            f'<p>На {report["cases"]} входах независимо сопоставлены балл, число совпадений, вердикт и уровень риска. '
            f'Расхождений: {report["mismatches"]}. Контроль проверки обнаружил все четыре намеренно изменённых значения.</p>'
            '<table><tr><th>Пример</th><th>Вход</th><th>Ожидаемый балл</th><th>Балл EXE</th><th>Ожидалось совпадений</th>'
            '<th>Совпадения EXE</th><th>Подозрительно</th><th>Риск</th></tr>' + ''.join(body) + '</table>'
            '<p>Порог применяется к каждой строке отдельно. Одно совпадение высокого риска также даёт положительный вердикт. '
            'Балл выражает сумму весов. Качество обнаружения по внешним меткам приведено в TRUST.md и release-quality.json.</p>'
            '<p>SHA-256 проверенного EXE: <code>'+esc(report["exe_sha256"])+'</code></p></html>')
    path.write_text(page,encoding="utf-8")

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("kit",type=Path);ap.add_argument("out",type=Path)
    ap.add_argument("--html",type=Path)
    sampling = ap.add_mutually_exclusive_group()
    sampling.add_argument("--seed", type=int, default=20261119, help="Replay a particular sample")
    sampling.add_argument("--fresh", action="store_true", help="Generate a new sample and record its seed")
    args=ap.parse_args();kit=args.kit.resolve()
    seed = secrets.randbits(63) if args.fresh else args.seed
    rules=read_weights(kit/"source/db/schema.sql")
    # Inputs are non-executable log fragments. Hand values are fixed independently of all four engines.
    hand=[
        ("чистая запись","заказ 48",0,0,0),("ниже порога",";;;;",4,4,0),
        ("на пороге",";;;;;",5,5,1),("двойной порог",";;;;;;;;;;",10,10,1),
        ("разные строки",";;;\r\n;;",5,5,0),("высокий риск ниже порога","--",3,1,1),
        ("перекрывающиеся совпадения","---",6,2,1),("три разных признака",";--",8,3,1),
        ("пустое поле","",0,0,0),("UTF-8","🙂я;я",1,1,0)]
    cases=[]
    for name,s,score,hits,pred in hand:
        result=reference(re.split(r"\r\n|\r|\n",s),rules)
        if result[:3]!=(score,hits,pred): raise ValueError("Hand fixture mismatch: "+name)
        cases.append((s,result,name))
    for i,(pattern,_,_) in enumerate(rules,1):
        cases.append((pattern,reference([pattern],rules),f"seed rule {i}"))
    # The independently known decoded form is supplied explicitly; no application normalization is imported.
    for s,expected in [("a%3Bb","a;b"),("a%253Bb","a;b"),("a&#59;b","a;b"),("a%3G","a%3g"),
                       ("стоимость+10;","стоимость 10;"),("a%2Bb%2520c","a+b c")]:
        cases.append((s,reference([expected],rules),"normalization"))
    layout_cases = ["sleep ( 0", "pg_sleep (4", "benchmark ( 7", "asleep (5", "x_pg_sleep (5",
                    "sleep ( units", "union all select null , null", "union distinct select null,null",
                    "union all select city,region", "union all select null,null_name",
                    "union all select null,nullя", "reunion all select null,null"]
    for text in layout_cases:
        cases.append((text,reference([text],rules),"context and spelling boundaries"))
    # Explicit expected outcomes for a small parameter-parser fixture; '?' is unbound.
    projection = "(select case when (2 < 3) then 4 else 8 end)"
    fragment = "fixture) and ? + " + projection
    structural = [(fragment, (4, 1, 1, "Высокий")),
                  ("select " + projection, (0, 0, 0, "Низкий")),
                  ("select\n" + fragment, (0, 0, 0, "Низкий")),
                  ("fixture) and '" + projection + "'", (0, 0, 0, "Низкий")),
                  ("fixture) and ? + (select inventory from items)", (0, 0, 0, "Низкий"))]
    cases.extend((value, expected, "independent structural fixture") for value, expected in structural)
    rng=random.Random(seed)
    for i in range(1000):
        text="".join(rng.choices([";","-","а","b"," ","0","🙂","\r\n"],k=rng.randrange(1,65)))
        cases.append((text,reference(re.split(r"\r\n|\r|\n",text),rules),"deterministic boundary case"))
    with tempfile.TemporaryDirectory(prefix="sqlscan-score-") as tmp:
        source,out=Path(tmp)/"samples.csv",Path(tmp)/"predictions.csv"
        with source.open("w",encoding="utf-8-sig",newline="") as f:
            writer=csv.writer(f);writer.writerow(["text","label"])
            writer.writerows((s,"") for s,_,_ in cases)
        subprocess.run([kit/"SqlScan.exe","--batch",source,"--out",out],check=True)
        with out.open(encoding="utf-8-sig",newline="") as f: result=list(csv.DictReader(f))
        verify(cases,result)
        for key,wrong in (("score","1"),("hits","1"),("predicted","1"),("risk","Высокий")):
            corrupt=[dict(result[0], **{key:wrong})]
            try:
                verify(cases[:1],corrupt)
            except ValueError:
                continue
            raise RuntimeError("Self-test failed to reject a corrupted " + key)
    report={"seed":seed,"cases":len(cases),"hand_cases":len(hand),"seed_rules":len(rules),"generated_benign_cases":1000,"layout_cases":len(layout_cases),"structural_cases":len(structural),
            "exe_sha256":hashlib.sha256((kit/"SqlScan.exe").read_bytes()).hexdigest(),
            "checks":["score","number of hits","per-line verdict","risk level"],
            "hand_results":[dict(example=n,input=s,score=sc,hits=h,verdict=y,observed_score=int(r["score"]),
                                 observed_hits=int(r["hits"]),observed_verdict=int(r["predicted"]),observed_risk=r["risk"])
                            for (n,s,sc,h,y),r in zip(hand,result[:len(hand)],strict=True)],
            "intentional_mismatches_rejected":4,
            "mismatches":0}
    args.out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    if args.html: write_html(args.html,report)
    print(f"Score audit: {len(cases)} inputs, all four output fields matched",flush=True)
if __name__=="__main__":
    main()
