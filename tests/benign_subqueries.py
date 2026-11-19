"""Exercise only ordinary read-only SQL over three tiny, offline SQLite tables."""
import argparse
import csv
import hashlib
import json
import sqlite3
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmark"))
from compare_releases import PREVIOUS_SHA, predict

# Each family is a useful query on the toy inventory. Numbers in string data are inert.
QUERIES = {
    "average price": "SELECT id FROM items WHERE price > (SELECT AVG(price) FROM items)",
    "correlated maximum": "SELECT i.id FROM items i WHERE price = (SELECT MAX(j.price) FROM items j WHERE j.dept=i.dept)",
    "group membership": "SELECT name FROM items WHERE dept IN (SELECT id FROM departments WHERE active)",
    "missing relationship": "SELECT name FROM items i WHERE NOT EXISTS (SELECT id FROM sales s WHERE s.item=i.id)",
    "correlated count": "SELECT name, (SELECT COUNT(*) FROM sales s WHERE s.item=i.id) FROM items i",
    "derived table": "SELECT MAX(total) FROM (SELECT item, SUM(qty) AS total FROM sales GROUP BY item)",
    "common table expression": "WITH totals AS (SELECT item,SUM(qty) qty FROM sales GROUP BY item) SELECT item FROM totals WHERE qty>(SELECT AVG(qty) FROM totals)",
    "conditional projection": "SELECT (SELECT CASE WHEN active THEN 10 ELSE 0 END FROM departments WHERE id=1)",
    "constant case for report": "SELECT (SELECT CASE WHEN (2 < 3) THEN 7 ELSE 0 END)",
    "boolean scalar column": "SELECT 9 AND (SELECT CASE WHEN (2 < 3) THEN 7 ELSE 0 END)",
    "numeric scalar comparison": "SELECT id FROM items WHERE 3 = (SELECT COUNT(*) FROM sales)",
    "same source joined by keys": "SELECT (SELECT COUNT(*) FROM items a,items b,items c WHERE a.id=b.id AND b.id=c.id)",
    "cardinality in tiny report": "SELECT 4 AND 7 = (SELECT COUNT(*) FROM items a,items b,items c)",
    "qualified aliases": "SELECT (SELECT COUNT(*) FROM main.items AS a,main.items AS b,main.items AS c)",
    "explicit cross join": "SELECT (SELECT COUNT(*) FROM items a CROSS JOIN items b)",
    "join on a derived table": "SELECT i.name,x.total FROM items i JOIN (SELECT item,SUM(qty) total FROM sales GROUP BY item) x ON x.item=i.id",
    "global limit": "SELECT name FROM items ORDER BY price LIMIT (SELECT COUNT(*) FROM departments)",
    "window function": "SELECT id FROM (SELECT id,DENSE_RANK() OVER (ORDER BY price) AS r FROM items) WHERE r=1",
    "string in projection": "SELECT (SELECT 'case when' || name FROM items ORDER BY id LIMIT 1)",
    "quoted column": 'SELECT "nested report" FROM (SELECT name AS "nested report" FROM items)',
    "literal text about a subquery": "SELECT 'Documentation: (SELECT COUNT(*) FROM items)'",
    "prepared parameter": "SELECT name FROM items WHERE price >= (SELECT COALESCE(?, 0))",
    "legitimate set operation": "SELECT id FROM items UNION SELECT id FROM departments",
    "ordinary SQL comment": "SELECT (SELECT COUNT(*) FROM items) -- report header",
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kit", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    previous = args.kit.resolve() / "validation/SqlScan-2.0.2.exe"
    if hashlib.sha256(previous.read_bytes()).hexdigest() != PREVIOUS_SHA:
        raise ValueError("Unexpected baseline")
    cases = []
    with sqlite3.connect(":memory:") as db:
        db.executescript("""
        CREATE TABLE departments(id INTEGER, active INTEGER);
        CREATE TABLE items(id INTEGER, name TEXT, dept INTEGER, price INTEGER);
        CREATE TABLE sales(id INTEGER, item INTEGER, qty INTEGER);
        INSERT INTO departments VALUES (1,1),(2,0);
        INSERT INTO items VALUES (1,'paper',1,7),(2,'ink',1,5),(3,'pen',2,8);
        INSERT INTO sales VALUES (1,1,2),(2,2,1);
        PRAGMA query_only=ON;
        """)
        for family, sql in QUERIES.items():
            variants = {"original": sql, "line break": sql.replace(" ", "\n  ", 1),
                        "trailing terminator": sql + "\n;"}
            for layout, query in variants.items():
                db.execute(query, (4,) if "?" in query else ()).fetchall()
                cases.append((family, layout, query))
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        src = work / "examples.csv"
        with src.open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["text"])
            w.writerows((query,) for _, _, query in cases)
        before = predict(previous, src, work / "old.csv", len(cases))
        after = predict(args.kit.resolve() / "SqlScan.exe", src, work / "new.csv", len(cases))
    changes = sum(a[0] == 0 and b[0] == 1 for a, b in zip(before, after, strict=True))
    result = {"query_families": len(QUERIES), "valid_statements": len(cases),
              "before_FP": sum(r[0] for r in before), "after_FP": sum(r[0] for r in after),
              "new_FP": changes, "rows": [
                  {"family": family, "layout": layout, "before": prior[0], "after": now[0]}
                  for (family, layout, _), prior, now in zip(cases, before, after, strict=True)]}
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if changes:
        raise SystemExit("New false alert on a valid read-only SQL statement")
    print(f"{len(cases)} executable benign SQL cases; {changes} new false alerts")

if __name__ == "__main__":
    main()
