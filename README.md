# C++ 数独题库生成器

`sudoku_tools` 的**主生成工具**：纯 **C++14** 实现，不依赖 Python 运行时。

参考 [Go 版 SudokuGenerator](https://github.com/bidepideyipi/sudoku/blob/main/server/internal/generator/sudoku.go) 的生成流程，并对唯一解校验做了位掩码、MRV 等优化。详细对比见 **[Go → C++14 优化报告](docs/go-to-cpp14-optimization-report.md)**。

## 在 sudoku_tools 中的位置

```
sudoku_tools/
├── cpp/                 ← generate_puzzles + generate_dungeon_puzzles
├── db/puzzles.db        ← 两套表共存
├── db/puzzles.schema.sql
└── script/export_ddl.py
```

生成完成后，将 `db/puzzles.db` **手工复制**到 `sudoku_app/assets/data/puzzles.db` 供 Flutter 打包使用。

## 环境要求

- C++14 编译器（Clang / GCC）
- `make` 或 CMake ≥ 3.10
- 系统 **SQLite3** 开发库（macOS 通常已自带，链接 `-lsqlite3`）

## 构建

```bash
cd sudoku_tools/cpp
make            # 产出 generate_puzzles 与 generate_dungeon_puzzles
# 或
cmake -S . -B build && cmake --build build -j
```

---

## Free / Daily：`generate_puzzles`

```bash
./generate_puzzles --count 1 --difficulty medium
./generate_puzzles --count 100 --difficulty easy --out ../db/puzzles.db
./generate_puzzles --count 400 --difficulty all --out ../db/puzzles.db
./generate_puzzles --verbose --count 40 --difficulty expert --out ../db/puzzles.db
./generate_puzzles --benchmark --count 100 --difficulty expert
./generate_puzzles --seed 42 --count 5 --difficulty hard
```

| 参数 | 说明 |
|------|------|
| `--count N` | 目标成功题数 |
| `--difficulty` | `easy` \| `medium` \| `hard` \| `expert` \| `all` |
| `--out PATH` | 写入表 `puzzles` |
| `--seed N` | 固定 PRNG 种子 |
| `--verbose` / `-v` | stderr 调试日志 |
| `--benchmark` | 不输出 TSV，仅耗时统计 |

写入表：`puzzles`（某难度首次写入前删除该难度旧数据）。

---

## 副本：`generate_dungeon_puzzles`

与 `generate_puzzles` **独立可执行文件**，只写副本表，**不改** `puzzles`。

默认：按 **topology** 题池写入（`mini_6x6`：`--per-pool`；`classic_9x9`：`4×per-pool` 轮转挖空以保多样性）。题表**无** `difficulty`；`readme` 在 **`dungeon_chapters`**（生成器留空）。

```bash
# 重建副本表并写入题池（不改 free puzzles）
./generate_dungeon_puzzles --verbose --per-pool 10 --out ../db/puzzles.db

./generate_dungeon_puzzles --seed 42 -v
make run-dungeon
```

写入表（见 `db/puzzles.schema.sql`）：

| 表 | 说明 |
|----|------|
| `dungeon_puzzles` | 题池（仅 `topology` / 盘面；无 difficulty） |
| `dungeon_chapters` | 12 章元数据（含 `difficulty` + `readme`） |

开局：`WHERE topology=? ORDER BY RANDOM() LIMIT 1`。

| 参数 | 说明 |
|------|------|
| `--per-pool N` | 每 topology 基数（默认 10；9×9 为 `4N`；`--per-chapter` 为别名） |
| `--out PATH` | 默认 `../db/puzzles.db` |
| `--seed N` | 固定种子 |
| `--verbose` / `-v` | 进度日志 |

---

## 生成算法

**9×9（free + 副本章 2~12）**

1. 回溯随机终盘 → 按难度挖空 → 唯一解校验  
2. 挖空数（与源码一致）：Easy **27** / Medium **36** / Hard **45** / Expert **54**  
3. 单批最多 5000 attempt  

**6×6（副本章 1）**

- 宫 **2×3**，数字 1~6，默认挖空 **14** / 36，唯一解校验  

## 输出格式（Free）

| 字段 | 说明 |
|------|------|
| `id` | UUID v4 |
| `puzzle` / `solution` | 81 字符，行优先，`0` = 空格 |
| `difficulty` | 1..4 |
| `created_at` | UTC 秒 |

## 目录结构

```
cpp/
├── include/
│   ├── sudoku_generator.h
│   ├── mini_sudoku_generator.h
│   ├── puzzle_db_writer.h
│   └── dungeon_db_writer.h
├── src/
│   ├── sudoku_generator.cpp
│   ├── mini_sudoku_generator.cpp
│   ├── puzzle_db_writer.cpp
│   ├── dungeon_db_writer.cpp
│   ├── main.cpp                 → generate_puzzles
│   └── main_dungeon.cpp         → generate_dungeon_puzzles
├── docs/
├── Makefile
└── CMakeLists.txt
```

## 相对 Go 的性能优化（摘要）

| 点 | C++ 实现 |
|----|----------|
| 约束检查 | 行/列/宫 **位掩码** O(1) |
| 唯一解搜索 | **MRV** + 候选 bit 迭代 |
| 多解检测 | 解数 >1 **早停** |
| 内存 | 栈上棋盘，热路径无堆分配 |
