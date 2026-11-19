"""Exercise the actual ZIP in a fresh, non-ASCII installation and data directory on Windows."""
import hashlib
import os
import subprocess
import sys
import tempfile
import time
import winreg
import zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

import psutil
from pywinauto import Application


def check(archive, root):
    dist = root / "Распакованный комплект"
    data = root / "Данные журнала"
    with zipfile.ZipFile(archive) as bundle:
        assert bundle.testzip() is None
        bundle.extractall(dist)
    for line in (dist / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected, name = line.split("  ", 1)
        with (dist / name).open("rb") as f:
            assert hashlib.file_digest(f, "sha256").hexdigest() == expected, name

    def launch(*options):
        with (root / "launch.log").open("ab") as log:
            run = subprocess.run([os.environ["COMSPEC"], "/d", "/c", "SqlScan-Local.cmd",
                                  "-DataRoot", str(data), *options], cwd=dist, stdin=subprocess.DEVNULL,
                                 stdout=log, stderr=log, timeout=100)
        if run.returncode:
            raise RuntimeError(f"Launcher exited with {run.returncode}")

    def process():
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            for proc in psutil.process_iter(["name", "exe"]):
                if proc.info["name"] == "SqlScan.exe" and proc.info["exe"] and Path(proc.info["exe"]) == dist / "SqlScan.exe":
                    return proc
            time.sleep(.1)
        raise RuntimeError("Packaged application did not start")

    try:
        for first in (True, False):
            launch()
            proc = process()
            try:
                app = Application(backend="win32").connect(process=proc.pid, timeout=8)
                w = app.window(title="SqlScan 2.0")
                w.wait("visible", timeout=15)
                assert "PostgreSQL подключена" in w.child_window(control_id=1011).window_text()
                if first:
                    w.child_window(control_id=1001).set_edit_text("Проверка сохранения после перезапуска")
                    w.child_window(control_id=1003).click()
                    assert "Сохранено в журнале" in w.child_window(control_id=1011).window_text()
                else:
                    w.child_window(control_id=1010).select(1)
                    assert w.child_window(control_id=1012).item_count() == 1
                w.close()
                proc.wait(5)
            finally:
                if proc.is_running():
                    proc.terminate()
                    proc.wait(5)
            launch("-Stop")
    finally:
        launch("-Stop")
        config = data / "instance.xml"
        if config.exists():
            props = ET.parse(config).getroot().find(".//{*}I32[@N='Port']")
            if props is not None:
                keyname = rf"Software\ODBC\ODBC.INI\SqlScan2-Local-{int(props.text)}"
                try:
                    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, keyname) as key:
                        dll = Path(winreg.QueryValueEx(key, "Driver")[0])
                    if dll.is_relative_to(data):
                        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, keyname)
                except FileNotFoundError:
                    pass


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="sqlscan-package-") as folder:
        check(Path(sys.argv[1]).resolve(), Path(folder))
    print("ZIP CRC, file hashes, Unicode paths, startup, database persistence and restart passed")
