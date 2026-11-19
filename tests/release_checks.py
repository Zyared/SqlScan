"""Exercise the published desktop executable in the disposable test database prepared by Recheck.ps1."""
import argparse
import csv
import os
import re
import struct
import subprocess
import tempfile
import time
from pathlib import Path
import win32con
import win32gui
from pywinauto import Application

def regress(kit, src):
    files = sorted((src/"test_samples").glob("sum_weight_*.txt"))
    spec = (src/"docs/TZ_SqlScan_2.0.md").read_text(encoding="utf-8")
    line = next(s for s in spec.splitlines() if s.startswith("- Обходы:"))
    examples = re.findall(chr(96) + "([^" + chr(96) + "]+)" + chr(96), line)
    assert len(files) == 5 and len(examples) == 3
    texts = [p.read_text(encoding="utf-8-sig") for p in files] + examples
    raw = [s.encode("utf-8") for s in texts]
    reference = subprocess.run([kit/"validation/baseline.exe"],
        input=b"".join(struct.pack("<I",len(b))+b for b in raw), stdout=subprocess.PIPE,check=True)
    expected = [tuple(map(int,s.split(b","))) for s in reference.stdout.splitlines()]
    with tempfile.TemporaryDirectory() as path:
        batch, result = Path(path)/"input.csv", Path(path)/"output.csv"
        with batch.open("w",encoding="utf-8",newline="") as f:
            w=csv.writer(f); w.writerow(["text"]); w.writerows([s] for s in texts)
        subprocess.run([kit/"SqlScan.exe","--batch",batch,"--out",result],check=True)
        with result.open(encoding="utf-8-sig",newline="") as f:
            actual=[(int(r["predicted"]),int(r["score"])) for r in csv.DictReader(f)]
    assert actual[:5] == expected[:5] and all(y for y,_ in actual[5:])
    print("Five v1 score fixtures and three normalization fixtures passed", flush=True)

def desktop(kit, src, data, port):
    ctl = kit/"runtime/pgsql/bin/pg_ctl.exe"
    proc = subprocess.Popen([kit/"SqlScan.exe"])
    try:
        w=Application(backend="win32").connect(process=proc.pid,timeout=12).window(title="SqlScan 2.0")
        w.wait("visible",timeout=12)
        box=lambda cid: w.child_window(control_id=cid).wrapper_object()
        assert "PostgreSQL подключена" in box(1011).window_text()
        for _ in range(3):
            box(1010).select(0)
            start=time.perf_counter()
            box(1010).select(1)
            win32gui.RedrawWindow(w.handle,None,None,win32con.RDW_UPDATENOW|win32con.RDW_ALLCHILDREN)
            grid=box(1012)
            assert grid.is_visible() and grid.item_count()==50 and grid.get_item(0,0).text().isdigit()
            ms=(time.perf_counter()-start)*1000
            print(f"10000-row journal visible in {ms:.1f} ms",flush=True)
            assert ms <= 1000
        box(1010).select(0)
        spec=(src/"docs/TZ_SqlScan_2.0.md").read_text(encoding="utf-8")
        row=next(s for s in spec.splitlines() if s.startswith("- Безопасность:"))
        literal=re.findall(chr(96)+"([^"+chr(96)+"]+)"+chr(96),row)[0]
        box(1001).set_edit_text(literal); box(1003).click()
        status=box(1011).window_text()
        assert "Сохранено в журнале" in status
        saved=re.search(r"№\s*(\d+)",status).group(1)
        box(1010).select(1)
        assert box(1012).get_item(0,0).text()==saved
        box(1010).select(0)
        subprocess.run([ctl,"stop","-w","-m","fast","-D",data/"cluster"],check=True)
        box(1001).set_edit_text("offline check");box(1003).click()
        assert "Автономный режим" in box(1011).window_text() and "Чисто" in box(1002).window_text()
        with (data/"restart.log").open("ab") as log:
            subprocess.run([ctl,"start","-w","-D",data/"cluster","-l",data/"postgres.log","-o",
                           f"-h 127.0.0.1 -p {port}"],stdin=subprocess.DEVNULL,stdout=log,stderr=log,check=True)
        box(1001).set_edit_text("after restart");box(1003).click()
        assert "Сохранено в журнале" in box(1011).window_text()
        print("Literal stored as data; offline fallback and reconnection passed",flush=True)
        w.close();proc.wait(8)
    finally:
        if proc.poll() is None: proc.terminate();proc.wait(5)

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kit",type=Path); parser.add_argument("data",type=Path); parser.add_argument("port",type=int)
    args=parser.parse_args()
    src=args.kit/"source"
    regress(args.kit,src)
    desktop(args.kit,src,args.data,args.port)
