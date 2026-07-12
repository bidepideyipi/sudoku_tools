# C++ 数独生成器

参考 [Go 版 SudokuGenerator](https://github.com/bidepideyipi/sudoku/blob/main/server/internal/generator/sudoku.go)，用 **C++14** 实现同等生成流程，并对唯一解校验做性能优化。

详细优化说明见 **[Go → C++14 优化报告](docs/go-to-cpp14-optimization-report.md)**。

## 算法（与 Go 一致）

1. 回溯 + 随机数字顺序 → 生成完整终盘
2. 按难度挖空：Easy 25 / Medium 35 / Hard 45 / Expert 55
3. 每挖 5 格或挖满时校验唯一解；失败则整题重试
4. Expert 最多 5000 次尝试，其余难度 100 次

## 相对 Go 的性能优化

| 点 | Go 参考实现 | C++ 实现 |
|----|-------------|----------|
| 约束检查 | 每次 `isValid` 扫描行/列/宫 | 行/列/宫 **位掩码**，O(1) |
| 唯一解搜索 | 找第一个空格 | **MRV**（候选最少格优先） |
| 多解检测 | 计数到 >1 才停 | 同样早停，减少无效分支 |
| 内存 | 值拷贝 `[9][9]int` | 栈上数组 + `memcpy`，热路径无堆分配 |
| 随机 | `crypto/rand` | `std::mt19937_64`（生成更快；ID 仍 UUID v4 格式） |

## 构建

```bash
cd sudoku_tools/cpp
make
# 或
cmake -S . -B build && cmake --build build -j
```

## 运行

```bash
# 生成 1 题，TSV 输出：id  difficulty  created_at  puzzle  solution
./generate_puzzles --count 1 --difficulty medium

# 批量 + 性能统计（stderr）
./generate_puzzles --benchmark --count 100 --difficulty expert

# 写入 SQLite（写入前删除该难度旧数据）
./generate_puzzles --count 100 --difficulty easy --out ../db/puzzles.db

# 固定种子（可复现）
./generate_puzzles --seed 42 --count 5 --difficulty hard

# 调试日志（stderr）：单批进度、失败重试、写入数据库
./generate_puzzles --verbose --count 40 --difficulty all --out ../db/puzzles.db
```

## 输出格式

与 `puzzles.db` 字段对应：

- `puzzle` / `solution`：81 字符，行优先，`0` 表示空格
- `difficulty`：1=easy, 2=medium, 3=hard, 4=expert

后续可在 `main.cpp` 中接入 SQLite 写入 `../db/puzzles.db`。
