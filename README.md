# C++ 数独题库生成器

`sudoku_tools` 的**主生成工具**：纯 **C++14** 实现，不依赖 Python 运行时。

参考 [Go 版 SudokuGenerator](https://github.com/bidepideyipi/sudoku/blob/main/server/internal/generator/sudoku.go) 的生成流程，并对唯一解校验做了位掩码、MRV 等优化。详细对比见 **[Go → C++14 优化报告](docs/go-to-cpp14-optimization-report.md)**。

## 在 sudoku_tools 中的位置

```
sudoku_tools/
├── cpp/                 ← 本目录：编译生成 generate_puzzles
├── db/puzzles.db        ← --out 默认写入目标
├── db/puzzles.schema.sql
└── script/export_ddl.py ← 可选：从 db 反向导出 DDL（与生成无关）
```

生成完成后，将 `db/puzzles.db` **手工复制**到 `sudoku_app/assets/data/puzzles.db` 供 Flutter 打包使用。

## 环境要求

- C++14 编译器（Clang / GCC）
- `make` 或 CMake ≥ 3.10
- 系统 **SQLite3** 开发库（macOS 通常已自带，链接 `-lsqlite3`）

## 构建

```bash
cd sudoku_tools/cpp
make
# 或
cmake -S . -B build && cmake --build build -j
# CMake 产物：build/generate_puzzles
```

## 运行

```bash
# 生成 1 题，TSV 输出到 stdout
./generate_puzzles --count 1 --difficulty medium

# 写入 SQLite（写入前删除该难度旧数据）
./generate_puzzles --count 100 --difficulty easy --out ../db/puzzles.db

# 四档各 100 题（共 400）
./generate_puzzles --count 400 --difficulty all --out ../db/puzzles.db

# 调试日志（stderr）
./generate_puzzles --verbose --count 40 --difficulty expert --out ../db/puzzles.db

# 性能测试（不打印 TSV，仅 stderr 统计）
./generate_puzzles --benchmark --count 100 --difficulty expert

# 固定随机种子（可复现）
./generate_puzzles --seed 42 --count 5 --difficulty hard
```

### CLI 参数

| 参数 | 说明 |
|------|------|
| `--count N` | 目标**成功**题数；单批失败会开新一批重试，直到凑满 N 题 |
| `--difficulty` | `easy` \| `medium` \| `hard` \| `expert` \| `all` |
| `--out PATH` | 写入 SQLite；某难度首次写入前 `DELETE` 该难度旧数据 |
| `--seed N` | 固定 PRNG 种子 |
| `--verbose` / `-v` | stderr 调试日志（单批进度、失败重试等） |
| `--benchmark` | 不输出题目 TSV，仅打印耗时统计 |

## 生成算法

1. 回溯 + 随机 1~9 顺序 → 完整终盘
2. 按难度挖空（当前配置）：

   | 难度 | 挖空数 | 给定数 |
   |------|--------|--------|
   | Easy | 30 | 51 |
   | Medium | 38 | 43 |
   | Hard | 46 | 35 |
   | Expert | 54 | 27 |

3. 每挖 **5** 格或挖满时校验唯一解；本批失败则换新终盘重试
4. 单批最多 **5000** 次 attempt，耗尽后由外层再开新一批（见 `--count`）

## 输出格式

与 `puzzles.db` 表结构一致（`script/export_ddl.py` 可导出 DDL）：

| 字段 | 说明 |
|------|------|
| `id` | UUID v4 字符串 |
| `puzzle` | 81 字符，行优先，`0` = 空格 |
| `solution` | 81 字符完整答案 |
| `difficulty` | 1=easy, 2=medium, 3=hard, 4=expert |
| `created_at` | UTC 秒级时间戳 |

## 目录结构

```
cpp/
├── include/
│   ├── sudoku_generator.h
│   └── puzzle_db_writer.h
├── src/
│   ├── sudoku_generator.cpp
│   ├── puzzle_db_writer.cpp
│   └── main.cpp
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
| 内存 | 栈上 `int[9][9]`，热路径无堆分配 |
