"""Локально создать тяжёлые файлы для стресс-теста (не хранятся в репозитории)."""
from pathlib import Path

here = Path(__file__).resolve().parent

# Ровно 10 МБ - 1 байт (граница лимита загрузки)
(here / "stress_ten_mb_minus_one.txt").write_bytes(b"Z" * (10 * 1024 * 1024 - 1))
print("OK:", here / "stress_ten_mb_minus_one.txt")
