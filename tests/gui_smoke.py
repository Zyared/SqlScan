"""Windows GUI acceptance. Uses a disposable PostgreSQL database from SQLSCAN_TEST_CONNECTION."""
import argparse
import csv
import os
import subprocess
import tempfile
import time
from pathlib import Path

import win32con
import win32gui
from pywinauto import Application


def wait_read(read, predicate=bool, seconds=8):
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        try:
            last = read()
            if predicate(last):
                return last
        except (RuntimeError, LookupError):
            pass
        time.sleep(0.06)
    raise AssertionError(f"Condition timed out ({read!r}); last value: {last!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    connection = os.environ["SQLSCAN_TEST_CONNECTION"]
    env = dict(os.environ, SQLSCAN_CONNECTION=connection)
    with tempfile.TemporaryDirectory(prefix="sqlscan-gui-") as tmp:
        root = Path(tmp)
        marker = "qa-marker-" + str(os.getpid())
        source = root / ("пример-" + str(os.getpid()) + ".log")
        source.write_bytes(("обычная строка\r\n" + marker).encode("utf-8-sig"))
        proc = subprocess.Popen([args.exe.resolve()], env=env)
        app = Application(backend="win32").connect(process=proc.pid, timeout=10)
        mainwin = app.window(title="SqlScan 2.0")
        def box(w, cid):
            return w.child_window(control_id=cid).wrapper_object()
        def post(w, cid):
            win32gui.PostMessage(w.handle, win32con.WM_COMMAND, cid, 0)
        def modal(title):
            dialog = app.window(title=title, class_name="#32770")
            dialog.wait("visible", timeout=6)
            return dialog
        def close_modal(w, cid=1):
            hwnd = w.handle
            post(w, cid)
            wait_read(lambda: not win32gui.IsWindow(hwnd))
        def standard_dialog(parent):
            hwnd = wait_read(lambda: next((w.handle for w in app.windows()
                             if win32gui.GetWindow(w.handle, win32con.GW_OWNER) == parent.handle
                             and win32gui.IsWindowVisible(w.handle)), 0))
            return app.window(handle=hwnd)
        def choose_path(parent, path):
            dialog = standard_dialog(parent)
            edits = dialog.descendants(class_name="Edit")
            if not edits:
                raise AssertionError("File-name edit missing")
            edits[0].set_edit_text(str(path))
            close_modal(dialog)
        def select_tab(index):
            box(mainwin, 1010).select(index)
        def row_index(text):
            table = box(mainwin, 1012)
            for i in range(table.item_count()):
                if text in [table.get_item(i, j).text() for j in range(table.column_count())]:
                    return i
            raise AssertionError("Row missing: " + text)
        try:
            mainwin.wait("visible")
            assert "PostgreSQL подключена" in box(mainwin, 1011).window_text()
            select_tab(2)
            post(mainwin, 1013)
            edit = modal("Сигнатура")
            box(edit, 1031).set_edit_text(marker)
            box(edit, 1033).set_edit_text("5")
            close_modal(edit)
            wait_read(lambda: row_index(marker) >= 0)
            table = box(mainwin, 1012)
            table.select(row_index(marker))
            post(mainwin, 1014)
            edit = modal("Сигнатура")
            assert box(edit, 1031).window_text() == marker
            box(edit, 1033).set_edit_text("4")
            box(edit, 1034).check()
            close_modal(edit)
            select_tab(0)
            post(mainwin, 1004)
            choose_path(mainwin, source)
            wait_read(lambda: marker in box(mainwin, 1001).window_text())
            box(mainwin, 1003).click()
            wait_read(lambda: "Сохранено в журнале" in box(mainwin, 1011).window_text())
            shown = box(mainwin, 1002).window_text()
            assert "Строка 2" in shown and "Расчёт баллов" in shown and f"{marker}  1 × 4 = 4" in shown and "есть сигнатура высокого риска" in shown
            post(mainwin, 1003)
            duplicate = standard_dialog(mainwin)
            close_modal(duplicate, 6)  # IDYES opens the existing report.
            detail = modal("Сохранённая проверка")
            assert marker in box(detail, 1051).window_text()
            close_modal(detail)
            for suffix in ("csv", "html", "txt"):
                out = root / ("отчёт." + suffix)
                post(mainwin, 1005)
                choose_path(mainwin, out)
                wait_read(out.exists)
                assert marker in out.read_text(encoding="utf-8-sig")
            box(mainwin, 1001).set_edit_text("ручная запись")
            box(mainwin, 1003).click()
            wait_read(lambda: "Чисто" in box(mainwin, 1002).window_text())
            select_tab(1)
            post(mainwin, 1013)
            filt = modal("Фильтры журнала")
            box(filt, 1041).set_edit_text(source.name)
            close_modal(filt)
            wait_read(lambda: box(mainwin, 1012).item_count() == 1)
            table = box(mainwin, 1012)
            table.select(0)
            post(mainwin, 1014)
            detail = modal("Сохранённая проверка")
            assert source.name in box(detail, 1051).window_text() and marker in box(detail, 1051).window_text()
            post(detail, 1052)
            archived = root / "архив.html"
            choose_path(detail, archived)
            wait_read(archived.exists)
            markup = archived.read_text(encoding="utf-8-sig")
            assert marker in markup and '<tr class="elevated">' in markup
            close_modal(detail)
            post(mainwin, 1016)
            journal = root / "журнал.csv"
            choose_path(mainwin, journal)
            wait_read(journal.exists)
            with journal.open(encoding="utf-8-sig", newline="") as f:
                assert list(csv.reader(f))[1][3] == source.name
            post(mainwin, 1016)
            journal_html = root / "журнал.html"
            choose_path(mainwin, journal_html)
            wait_read(journal_html.exists)
            assert '<tr class="elevated">' in journal_html.read_text(encoding="utf-8-sig")
            box(mainwin, 1012).select(0)
            post(mainwin, 1015)
            close_modal(standard_dialog(mainwin), 6)
            wait_read(lambda: box(mainwin, 1012).item_count() == 0)
            select_tab(3)
            assert "Проверок:" in box(mainwin, 1011).window_text()
            assert box(mainwin, 1019).is_visible()
            select_tab(2)
            box(mainwin, 1012).select(row_index(marker))
            box(mainwin, 1015).click()
            select_tab(0)
            box(mainwin, 1001).set_edit_text(marker)
            box(mainwin, 1003).click()
            assert "Чисто" in box(mainwin, 1002).window_text()
            select_tab(2)
            box(mainwin, 1012).select(row_index(marker))
            post(mainwin, 1016)
            close_modal(standard_dialog(mainwin), 6)
            print("GUI passed: rule CRUD, file/manual scan, deduplication, three exports, filters, archive, delete, statistics")
        finally:
            win32gui.PostMessage(mainwin.handle, win32con.WM_CLOSE, 0, 0)
            try:
                proc.wait(3)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait()


if __name__ == "__main__":
    main()
