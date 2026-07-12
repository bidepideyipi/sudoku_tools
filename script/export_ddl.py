#!/usr/bin/env python3
"""从 SQLite 数据库反向导出 DDL（CREATE TABLE / INDEX / TRIGGER / VIEW）。

默认读取 ../db/puzzles.db，输出到 ../db/puzzles.schema.sql。

用法:
    python export_ddl.py
    python export_ddl.py --db ../db/puzzles.db --out ../db/puzzles.schema.sql
    python export_ddl.py --stdout
"""

from __future__ import annotations

import argparse
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_DB = SCRIPT_DIR.parent / "db" / "puzzles.db"
DEFAULT_OUT = SCRIPT_DIR.parent / "db" / "puzzles.schema.sql"

# sqlite_master.type -> 导出顺序
_TYPE_ORDER = {
    "table": 0,
    "index": 1,
    "trigger": 2,
    "view": 3,
}

# 不导出的系统 / 自动对象
_SKIP_NAMES = frozenset({"sqlite_sequence"})


def _is_auto_index(name: str, sql: str | None) -> bool:
    return name.startswith("sqlite_autoindex_") or sql is None


def export_ddl(db_path: Path) -> str:
    if not db_path.is_file():
        raise FileNotFoundError(f"database not found: {db_path}")

    conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    try:
        rows = conn.execute(
            """
            SELECT type, name, sql
            FROM sqlite_master
            WHERE sql IS NOT NULL
            ORDER BY type, name
            """
        ).fetchall()
    finally:
        conn.close()

    statements: list[str] = []
    for obj_type, name, sql in rows:
        if name in _SKIP_NAMES or _is_auto_index(name, sql):
            continue
        statements.append((obj_type, name, sql.strip().rstrip(";")))

    statements.sort(key=lambda item: (_TYPE_ORDER.get(item[0], 99), item[1]))

    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines = [
        "-- Reverse-engineered DDL from SQLite database",
        f"-- Source: {db_path}",
        f"-- Generated: {generated_at}",
        "",
    ]

    current_type: str | None = None
    for obj_type, _name, sql in statements:
        if obj_type != current_type:
            if current_type is not None:
                lines.append("")
            lines.append(f"-- {obj_type.upper()}")
            current_type = obj_type
        lines.append(f"{sql};")

    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export DDL statements from a SQLite database."
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=DEFAULT_DB,
        help=f"path to SQLite file (default: {DEFAULT_DB})",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help=f"output .sql file (default: {DEFAULT_OUT})",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="print DDL to stdout instead of writing a file",
    )
    args = parser.parse_args()

    try:
        ddl = export_ddl(args.db.resolve())
    except (FileNotFoundError, sqlite3.Error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.stdout:
        print(ddl, end="")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(ddl, encoding="utf-8")
    print(f"Wrote DDL to {args.out.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
