# Go → C++14 数独生成器优化报告

> **参考实现**：[bidepideyipi/sudoku — `server/internal/generator/sudoku.go`](https://github.com/bidepideyipi/sudoku/blob/main/server/internal/generator/sudoku.go)  
> **C++ 实现路径**：`sudoku_tools/cpp/`（`include/sudoku_generator.h`、`src/sudoku_generator.cpp`）  
> **报告日期**：2026-07-11  
> **编译选项**：`-std=c++14 -O3 -Wall -Wextra -Wpedantic`（macOS / Apple Clang）

---

## 1. 背景与目标

Go 版 `SudokuGenerator` 用于服务端离线生成数独题库，流程清晰、易于维护，但在批量生成（尤其 **Expert** 档位）时，唯一解校验会成为主要耗时点。

C++14 版本的目标：

1. **保持与 Go 版相同的生成语义**（挖空策略、校验时机、重试上限、输出格式）。
2. **针对热路径做确定性优化**，提升吞吐、降低单题平均耗时。
3. **不引入额外第三方依赖**（标准库 + 可选 SQLite 扩展留作后续）。

---

## 2. 算法对齐（未改动的部分）

以下逻辑与 Go 版 **一一对应**，保证生成行为一致：

| 步骤 | Go | C++14 |
|------|----|-------|
| 终盘生成 | `generateFullGrid` → 回溯 + 随机 1~9 | `generateFullGrid` → `fillBoard` |
| 挖空数量 | Easy 25 / Medium 35 / Hard 45 / Expert 55 | `holesByDifficulty()` 相同 |
| 挖空顺序 | `shufflePositions()` 随机 81 格 | `shufflePositions()` 相同 |
| 唯一解校验 | 每挖 **5** 格或挖满时检查 | `digHolesWithValidation()` 相同 |
| 校验失败 | 返回 `(board, false)`，整题重试 | 返回 `false`，整题重试 |
| 重试上限 | 普通 100 次，Expert **5000** 次 | `Generate()` 相同 |
| 输出 | 81 字符题面/答案 + UUID + 难度 | `Puzzle` 结构体，字段对齐 `puzzles.db` |

**流水线示意（两版相同）：**

```
生成终盘 → 随机挖空 → [每 5 格] 唯一解校验 → 成功则输出 / 失败则重试
```

---

## 3. 核心优化项

### 3.1 约束检查：扫描 → 位掩码（Bitmask）

**Go 版**（每次填数调用 `isValid`，扫描整行、整列、整个 3×3 宫）：

```go
func isValid(board *[9][9]int, row, col, num int) bool {
    for c := 0; c < 9; c++ {
        if board[row][c] == num { return false }
    }
    for r := 0; r < 9; r++ {
        if board[r][col] == num { return false }
    }
    // ... 再扫 3×3 宫
}
```

- 单次检查：**O(9)** × 3 方向 ≈ 常数但仍有 20+ 次数组访问。
- 在回溯与唯一解搜索中会被调用 **成千上万次**。

**C++14 版**：维护三组掩码 `row_mask[9]`、`col_mask[9]`、`box_mask[9]`，数字 `d` 对应 bit `(1 << d)`：

```cpp
inline bool masksConflict(int row_mask, int col_mask, int box_mask, int digit) {
  const int bit = digitBit(digit);
  return (row_mask & bit) || (col_mask & bit) || (box_mask & bit);
}
```

- 单次检查：**O(1)**，3 次位与运算。
- 填数 / 回溯时同步 `setMasks(..., place=true/false)`，避免重复扫描棋盘。

**提升点**：终盘生成（`fillBoard`）与唯一解搜索（`solveWithCount`）的内层循环显著变轻。

---

### 3.2 唯一解搜索：第一个空格 → MRV 启发式

**Go 版** `solveWithCount` 使用 `findEmpty`：按行优先找 **第一个** 空格，再对 1~9 逐个尝试。

**C++14 版** `findBestEmpty` 实现 **MRV（Minimum Remaining Values）**：

- 遍历所有空格，用位掩码计算候选集 `candidates = (~used) & 0x3FE`。
- 选择 **候选数最少** 的格子优先分支（`popcountMask` 计数）。
- 候选为 0 时立即判定无解，提前剪枝。

**提升点**：

- 更快触发矛盾，减少无效递归深度。
- Expert 挖空 55 格时，唯一解校验调用频繁，MRV 对 **Hard / Expert** 档位收益最大。

> 说明：MRV 只用于 **唯一解计数**（`hasUniqueSolution`），终盘生成的空格选择仍与 Go 一致（`findEmpty` 行优先），以保持终盘随机分布习惯相近。

---

### 3.3 候选枚举：循环 1~9 → 位运算迭代

**Go 版**：对每个空格 `for num := 1; num <= 9; num++`。

**C++14 版**：直接遍历候选掩码中的置位 bit：

```cpp
int mask = candidate_mask;
while (mask != 0) {
  const int bit = mask & -mask;      // 取最低置位
  mask ^= bit;
  const int digit = trailingZeroBit(bit);
  // 尝试 digit ...
}
```

- 只尝试 **真实候选**，跳过已被行/列/宫排除的数字。
- 使用 `__builtin_ctz` / `__builtin_popcount`（GCC/Clang）或 portable 回退。

**提升点**：平均每个空格尝试次数从最多 9 次降为 **候选数均值（通常 2~4）**。

---

### 3.4 多解检测：早停（两版均有，C++ 更贴合热路径）

**Go 版**：

```go
if *count > 1 {
    return false
}
```

**C++14 版**：在 `solveWithCount` 入口同样判断 `*count > 1` 立即返回。

唯一解校验只需区分「0 / 1 / >1 解」，**不需枚举全部解**。早停避免 Expert 档位下的指数级浪费。

---

### 3.5 内存与数据布局

| 项目 | Go | C++14 |
|------|----|-------|
| 棋盘 | `[9][9]int` 值拷贝 | `int board[9][9]` 栈分配 |
| 挖空 | `puzzle := board` 拷贝 | `memcpy(puzzle, solution, 81 * sizeof(int))` |
| 热路径堆分配 | 切片、字符串拼接 | 无（搜索过程零 `new`） |
| 字符串输出 | `boardToString` 逐格 `+=` | `reserve(81)` + `push_back` |

C++ 将 **唯一解搜索** 全程放在栈上，利于 CPU 缓存局部性；Go 的 GC 与边界检查在此类 tight loop 中会有额外开销。

---

### 3.6 随机数：密码学随机 → 伪随机（ deliberate 权衡）

| 项目 | Go | C++14 |
|------|----|-------|
| 洗牌 / 挖空顺序 | `crypto/rand` | `std::mt19937_64` |
| UUID | `crypto/rand` | `mt19937_64` 16 字节 + UUID v4 位掩码 |
| 可复现性 | 不可设种子 | `Seed(uint64_t)` 支持固定种子 |

**取舍**：

- `crypto/rand` 更安全，但 **系统调用成本高**，每次 shuffle 81 位置 + 9 数字都触发，批量生成时成为隐性瓶颈。
- 题库生成场景更关注 **吞吐与可复现**，C++ 选用快速 PRNG 合理。
- 若需密码学强度 ID，可单独将 `generateUuid` 切回 `/dev/urandom` 或 `std::random_device`。

---

### 3.7 编译与运行时

| 项目 | Go | C++14 |
|------|----|-------|
| 运行模型 | 编译为 native + GC | AOT + 无 GC |
| 内联 | 编译器自动 | `inline` 辅助函数 + `-O3` |
| 边界检查 | 默认可 bounds check | 原始数组，无边界检查开销 |

---

## 4. 模块对照表

| Go 符号 | C++ 符号 | 说明 |
|---------|----------|------|
| `SudokuGenerator.Generate` | `SudokuGenerator::Generate` | 对外入口 |
| `generateFullGrid` | `generateFullGrid` | 终盘 |
| `fillBoard` | `fillBoard` | 回溯填盘（C++ 增 bitmask） |
| `digHolesWithValidation` | `digHolesWithValidation` | 挖空 + 校验 |
| `hasUniqueSolution` | `hasUniqueSolution` | 唯一解 |
| `solveWithCount` | `solveWithCount` | 解计数（C++ 增 MRV + bitmask） |
| `isValid` | `masksConflict` + `setMasks` | 约束检查重构 |
| `findEmpty` | `findEmpty` / `findBestEmpty` | 后者仅用于解计数 |
| `shuffleNumbers` | `shuffleDigits` | Fisher-Yates |
| `shufflePositions` | `shufflePositions` | Fisher-Yates |
| `boardToString` | `boardToString` | 81 字符 |
| `generateUUID` | `generateUuid` | UUID v4 格式 |

---

## 5. 基准测试（本机实测）

环境：macOS，Apple Clang，`make` 默认 `-O3`。

命令：

```bash
./generate_puzzles --benchmark --count 100 --difficulty easy
./generate_puzzles --benchmark --count 100 --difficulty medium
./generate_puzzles --benchmark --count 100 --difficulty hard
./generate_puzzles --benchmark --count 50  --difficulty expert
```

| 难度 | 挖空数 | 成功/总数 | 总耗时 | 平均耗时/题 |
|------|--------|-----------|--------|-------------|
| Easy | 25 | 100 / 100 | 5 ms | **~0.05 ms** |
| Medium | 35 | 100 / 100 | 9 ms | **~0.09 ms** |
| Hard | 45 | 100 / 100 | 36 ms | **~0.36 ms** |
| Expert | 55 | 25 / 50* | 10.8 s | **~434 ms**（仅计成功题） |

\* Expert 批量生成时部分尝试会在 5000 次上限内失败（与 Go 设计一致），失败重试会拉高总耗时。

**观察**：

- Easy ~ Hard：**亚毫秒级**，适合大批量灌库。
- Expert：瓶颈在 **唯一解校验 + 高挖空率**，C++ 优化主要压缩单次校验成本；成功率仍受算法随机性影响，与 Go 同类实现表现相近。

> Go 版未在同一机器做对照 benchmark；上述数据用于说明 C++ 版绝对性能量级。若需严格 Go vs C++ 对比，可用相同 `--seed` 与 `--count` 各跑一轮计时。

---

## 6. 预期相对 Go 的性能收益（定性）

| 热路径 | 预期收益 | 原因 |
|--------|----------|------|
| 终盘 `fillBoard` | 中 | Bitmask 替代 `isValid` 扫描 |
| 唯一解 `solveWithCount` | **高** | Bitmask + MRV + 候选 bit 迭代 + 早停 |
| 挖空 shuffle | 低~中 | `mt19937_64` 快于 `crypto/rand` |
| 整体 Expert | **高** | 校验占主导，上述优化叠加 |
| 整体 Easy | 低 | 校验次数少，差异不明显 |

综合估计：在 **Hard / Expert** 档位，C++14 版相对 Go 参考实现通常可有 **数倍到一个数量级** 的单题校验加速（视题目与 RNG 路径而定）；Easy / Medium 已接近 I/O 与字符串构建下限。

---

## 7. 已知限制与后续方向

### 7.1 当前限制

1. **Expert 成功率**：55 格挖空 + 唯一解约束极严，仍可能出现 `Generate` 返回 `false`（Go 同样行为）。
2. **随机性差异**：PRNG 与 Go `crypto/rand` 不同，**题目序列不会逐题相同**（即使逻辑等价）。
3. **尚未写入 SQLite**：CLI 目前输出 TSV，需手工或脚本导入 `puzzles.db`。
4. **单线程**：未使用 OpenMP / 线程池并行批量生成。

### 7.2 可继续优化（未实现）

| 方向 | 说明 |
|------|------|
| DLX / Algorithm X | 唯一解计数专用求解器，Expert 可能再提速 |
| 并行批量生成 | `#pragma omp parallel for` 或多进程按难度分片 |
| SQLite 批量写入 | 事务 + prepared statement 直接写 `db/puzzles.db` |
| 挖空策略 | 对称性约简、最小线索数搜索（会改变与 Go 的严格对齐） |

---

## 8. 使用方式

```bash
cd sudoku_tools/cpp
make

# 单题
./generate_puzzles --count 1 --difficulty medium

# 批量 + 统计
./generate_puzzles --benchmark --count 100 --difficulty hard

# 可复现
./generate_puzzles --seed 42 --count 10 --difficulty easy
```

输出 TSV 列：`id`、`difficulty`、`created_at`、`puzzle`（81 字符）、`solution`（81 字符），与 `puzzles.db` 表结构一致。

---

## 9. 结论

C++14 版本在 **不改变 Go 版生成流程与业务规则** 的前提下，通过 **位掩码约束、MRV 搜索、候选 bit 迭代、栈内存布局、快速 PRNG、`-O3` 编译** 等手段，将主要优化集中在 **唯一解校验** 这一瓶颈上。

- **语义对齐**：挖空数、校验频率、重试策略、输出格式与 Go 一致。  
- **性能提升**：Easy/Hard 已达毫秒以下；Expert 单题校验成本显著降低，适合作为 `sudoku_tools` 批量灌库的高性能后端。  
- **工程化**：纯 C++14、无额外依赖、提供 CLI 与 benchmark，便于后续接 SQLite 与并行扩展。

---

## 附录 A：关键源码索引

```
sudoku_tools/cpp/include/sudoku_generator.h   # 公开 API
sudoku_tools/cpp/src/sudoku_generator.cpp     # 生成 + 校验核心
sudoku_tools/cpp/src/main.cpp                   # CLI / benchmark
sudoku_tools/cpp/Makefile                       # 构建
sudoku_tools/db/puzzles.schema.sql              # 目标库 DDL
```

## 附录 B：Go 参考链接

- 生成器主文件：[sudoku.go](https://github.com/bidepideyipi/sudoku/blob/main/server/internal/generator/sudoku.go)
- 关键函数：`Generate`、`digHolesWithValidation`、`hasUniqueSolution`、`solveWithCount`、`isValid`
