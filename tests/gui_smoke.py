"""Windows GUI acceptance. Uses a disposable PostgreSQL database from SQLSCAN_TEST_CONNECTION."""
import argparse
import codecs
import csv
import ctypes
import os
import subprocess
import tempfile
import time
from pathlib import Path

import win32con
import win32gui
from pywinauto import Application
from pywinauto.remote_memory_block import RemoteMemoryBlock


class CharFormat(ctypes.Structure):
    # CHARFORMATW from richedit.h. EM_GETCHARFORMAT runs in the tested Rich Edit process.
    _fields_ = [("size", ctypes.c_uint32), ("mask", ctypes.c_uint32), ("effects", ctypes.c_uint32),
                ("height", ctypes.c_int32), ("offset", ctypes.c_int32), ("color", ctypes.c_uint32),
                ("charset", ctypes.c_ubyte), ("family", ctypes.c_ubyte), ("face", ctypes.c_wchar * 32)]


def score_color(control, score):
    label = "Баллы: "
    shown = control.window_text().replace("\r\n", "\r")
    start = shown.index(label + str(score) + " ") + len(label)
    control.send_message(win32con.EM_SETSEL, start, start + len(str(score)))
    fmt = CharFormat()
    fmt.size = ctypes.sizeof(fmt)
    remote = RemoteMemoryBlock(control, fmt.size)
    try:
        remote.Write(fmt)
        control.send_message(win32con.WM_USER + 58, 1, remote)  # EM_GETCHARFORMAT, SCF_SELECTION
        remote.Read(fmt)
        assert fmt.mask & 0x40000000  # CFM_COLOR
        return tuple((fmt.color >> shift) & 255 for shift in (0, 8, 16))
    finally:
        remote.CleanUp()


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
            post(mainwin, 40001)
            settings = modal("Подключение к PostgreSQL")
            post(settings, 1022)
            def connection_notice():
                for popup in app.windows(class_name="#32770"):
                    if popup.handle in (settings.handle, mainwin.handle) or not popup.is_visible():
                        continue
                    if any("Подключение успешно. Активных правил: 18" in c.window_text()
                           for c in popup.children(class_name="Static")):
                        return app.window(handle=popup.handle)
            close_modal(wait_read(connection_notice))
            close_modal(settings, 2)
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
            post(mainwin, 1003)
            wait_read(lambda: box(mainwin, 1011).window_text(), lambda status: "Сохранено в журнале" in status)
            shown = box(mainwin, 1002).window_text()
            assert "Строка 2" in shown and "Расчёт баллов" in shown and f"{marker}  1 × 4 = 4" in shown and "есть сигнатура высокого риска" in shown
            post(mainwin, 1003)
            duplicate = standard_dialog(mainwin)
            close_modal(duplicate, 6)  # IDYES opens the existing report.
            detail = modal("Сохранённая проверка")
            archived_text = box(detail, 1051).window_text()
            assert marker in archived_text
            assert "Строка 1 · 0 · Чисто" in archived_text and "Строка 2 · 4 · Подозрительно" in archived_text
            close_modal(detail)
            for suffix in ("csv", "html", "txt"):
                out = root / ("отчёт." + suffix)
                post(mainwin, 1005)
                choose_path(mainwin, out)
                wait_read(out.exists)
                payload = out.read_bytes()
                assert payload.startswith(codecs.BOM_UTF8)
                assert marker in payload[len(codecs.BOM_UTF8):].decode("utf-8")
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
            assert "<td>1</td><td>0</td><td>Чисто</td>" in markup
            assert "<td>2</td><td>4</td><td>Подозрительно</td>" in markup
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
            select_tab(0)
            box(mainwin, 1001).set_edit_text("разбор журнала: sleep ( 0")
            box(mainwin, 1003).click()
            shown = box(mainwin, 1002).window_text()
            assert "1 × 4 = 4" in shown and "есть сигнатура высокого риска" in shown
            assert "Сохранено в журнале" in box(mainwin, 1011).window_text()
            box(mainwin, 1001).set_edit_text("fixture) and ? + (select case when (2 < 3) then 4 else 8 end)")
            box(mainwin, 1003).click()
            shown = box(mainwin, 1002).window_text()
            assert "Вложенное условие" in shown and "1 × 4 = 4" in shown
            assert "Сохранено в журнале" in box(mainwin, 1011).window_text()
            for score, color in [(0, (32, 112, 45)), (50, (145, 111, 0)), (100, (173, 44, 39))]:
                box(mainwin, 1001).set_edit_text("control" + ";" * score)
                box(mainwin, 1003).click()
                assert score_color(box(mainwin, 1002), score) == color

            # The marker is outside the visible preview but within the analysis limit.
            long_file = root / "большой.log"
            long_file.write_bytes((("normal line\n" * 25_000) + ";;;;;").encode("ascii"))
            post(mainwin, 1004)
            choose_path(mainwin, long_file)
            wait_read(lambda: box(mainwin, 1008).is_visible())
            editor = box(mainwin, 1001)
            assert win32gui.GetWindowLong(editor.handle, win32con.GWL_STYLE) & win32con.ES_READONLY
            assert ";;;;;" not in editor.window_text()
            box(mainwin, 1003).click()
            assert "Строка 25001" in box(mainwin, 1002).window_text()
            box(mainwin, 1008).click()
            assert not (win32gui.GetWindowLong(editor.handle, win32con.GWL_STYLE) & win32con.ES_READONLY)
            assert editor.window_text() == "" and not box(mainwin, 1005).is_enabled()
            # Choose a non-ASCII word that round-trips on the host's active Windows ANSI code page.
            word = next(w for w in ("пример", "café", "例", "한", "ش", "ก", "א", "α")
                        if w.encode("mbcs", errors="replace").decode("mbcs") == w)
            encoded_text = f"{word} {os.getpid()}\r\n;;;;;"
            acp = ctypes.windll.kernel32.GetACP()
            if acp != 65001:
                assert encoded_text.encode("mbcs") != encoded_text.encode("utf-8")
            reports = []
            for encoding in ("mbcs", "utf-8-sig"):
                sample = root / ("текст-" + encoding + ".log")
                sample.write_bytes(encoded_text.encode(encoding))
                post(mainwin, 1004)
                choose_path(mainwin, sample)
                assert editor.window_text() == encoded_text
                box(mainwin, 1003).click()
                reports.append(box(mainwin, 1002).window_text())
            assert reports[0] == reports[1] and "Подозрительно" in reports[0] and "Баллы: 5 " in reports[0]
            print(f"GUI decoded Windows ACP {acp} and UTF-8 BOM to the same text and score")

            limit = 2 * 1024 * 1024
            boundary = root / "ровно-2МиБ.log"
            block = b"a" * 127 + b"\n"
            boundary.write_bytes(block * (limit // len(block)))
            oversized = root / "выше-лимита.log"
            oversized.write_bytes(boundary.read_bytes() + b"x")
            post(mainwin, 1004)
            choose_path(mainwin, boundary)
            wait_read(lambda: boundary.name in box(mainwin, 1011).window_text())
            box(mainwin, 1003).click()
            old_report = box(mainwin, 1002).window_text()
            assert f"524288 из {limit} байт" in old_report
            old_status = box(mainwin, 1011).window_text()
            assert "Сохранено в журнале" in old_status
            old_preview = editor.window_text()
            post(mainwin, 1004)
            choose_path(mainwin, oversized)
            error = standard_dialog(mainwin)
            assert any("Превышен лимит размера файла" in item.window_text()
                       for item in error.descendants(class_name="Static"))
            close_modal(error)
            assert box(mainwin, 1011).window_text() == old_status and editor.window_text() == old_preview
            assert box(mainwin, 1002).window_text() == old_report and box(mainwin, 1005).is_enabled()
            post(mainwin, 1003)
            close_modal(standard_dialog(mainwin), 6)
            detail = modal("Сохранённая проверка")
            assert boundary.name in box(detail, 1051).window_text()
            close_modal(detail)
            print("GUI passed: rule CRUD, file/manual scan, deduplication, UTF-8 BOM exports, archived scores, "
                  "large-file preview, 2 MiB boundary and rejection, score gradient, filters, delete, statistics")
        except Exception:
            for popup in app.windows(class_name="#32770"):
                if popup.handle != mainwin.handle and popup.is_visible():
                    print("Open dialog:", popup.window_text(),
                          [c.window_text() for c in popup.children(class_name="Static")], flush=True)
            raise
        finally:
            win32gui.PostMessage(mainwin.handle, win32con.WM_CLOSE, 0, 0)
            try:
                proc.wait(3)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait()


if __name__ == "__main__":
    main()
