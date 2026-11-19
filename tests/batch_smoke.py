"""Exercise the release executable and check all file I/O using temporary paths."""
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="sqlscan-csv-") as temp:
    folder = Path(temp)
    src, out = folder / "вход.csv", folder / "результат.csv"
    def run(*args):
        return subprocess.run([exe, *args], capture_output=True)
    rows = [('текст, "кавычки"\r\nобычный текст', '0'), ('item;--', '1'), ('', '0')]
    with src.open('w', newline='', encoding='utf-8-sig') as f:
        w = csv.writer(f)
        w.writerow(['text', 'label'])
        w.writerows(rows)
    assert run('--batch', src, '--out', out).returncode == 0
    with out.open(newline='', encoding='utf-8-sig') as f:
        assert [r['predicted'] for r in csv.DictReader(f)] == ['0', '1', '0']
    original = src.read_bytes()
    assert run('--batch', src, '--out', src).returncode != 0 and src.read_bytes() == original
    good = out.read_bytes()
    for invalid in ['label\n0\n', 'text,label\n"unterminated\n', 'text,label\na,b,c\n']:
        src.write_text(invalid, encoding='utf-8')
        assert run('--batch', src, '--out', out).returncode != 0 and out.read_bytes() == good
    assert run('--unknown').returncode != 0
print('Batch passed: Unicode paths, BOM, quoted multiline fields, empty input, validation and atomic output')
