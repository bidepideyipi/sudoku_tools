# sudoku_tools

Python 辅助工程，用于**生成** Sudoku Flutter 原型所需的题库数据。

本工程与 `sudoku_app/` **完全独立**：不产生任何代码调用、依赖或运行时关联。生成 `puzzles.db` 后，由开发者**手工复制**到 Flutter 工程中替换资源文件即可。

## 目的

为 `sudoku_app` 提供可打包进应用的 SQLite 题库文件，供游戏内按难度随机抽题。

| 项目 | 说明 |
|------|------|
| 本工程（`sudoku_tools`） | 离线生成 / 维护题库数据 |
| Flutter 原型（`sudoku_app`） | 读取已打包的 `assets/data/puzzles.db`，不负责出题 |

## 输出产物

生成目标路径（相对于仓库根目录）：

```
sudoku_app/assets/data/puzzles.db
```

**使用方式**：在本工程运行生成脚本后，将产出的 `puzzles.db` 覆盖复制到上述路径，再在 `sudoku_app` 中验证 / 打包。Flutter 工程不会自动拉取或执行本目录下的任何脚本。

## 与 Flutter 的约定

Flutter 侧通过 `PuzzleRepository` 读取该数据库，期望的表结构为：

```sql
CREATE TABLE puzzles (
  id TEXT PRIMARY KEY,
  puzzle TEXT NOT NULL,      -- 81 字符，0 表示空格，行优先
  solution TEXT NOT NULL,  -- 81 字符，完整答案，行优先
  difficulty INTEGER NOT NULL,  -- 1=easy, 2=medium, 3=hard, 4=expert
  created_at INTEGER NOT NULL
);
```

难度字段与 `sudoku_app/lib/models/difficulty.dart` 中 `dbLevel` 一致。

---

## 题库生成与难度分级

### 设计原则

- **唯一解**：入库题面必须且仅能有一个合法解（生成后校验）。
- **可解释**：难度由可复现的度量指标决定，不依赖主观标注。
- **多指标综合**：单一「挖空数」不足以区分难度，采用加权评分 + 技巧门槛双重约束。
- **与玩家技巧对齐**：评分基于**人类逻辑求解器**（按技巧等级递进），而非回溯暴力搜索步数。

### 难度档位（写入 `difficulty` 字段）

| dbLevel | 标签 | 目标玩家体验 | 硬性约束（须同时满足） |
|--------:|------|-------------|------------------------|
| 1 | Easy | 直观填数为主 | 仅需 Singles；初期平均候选数 ≤ 3.0；综合分 < 30 |
| 2 | Medium | 需系统排除 | 最高技巧 ≤ 区块排除（Pointing / Claiming）；综合分 30~55 |
| 3 | Hard | 需链式推理 | 最高技巧 ≤ X-Wing / Swordfish 级；综合分 55~80 |
| 4 | Expert | 需高级模式 | 允许 XY-Chain、简单着色等；综合分 ≥ 80 |

> 硬性约束优先：若某题综合分偏低但求解器用到了超出该档允许的最高技巧，则**升档**至允许该技巧的最低档位。

---

### 度量指标

#### 1. 挖空数量（Clue Count / Givens）

| 项 | 说明 |
|----|------|
| **定义** | 题面非零格数量 `givens`（范围通常 17~64） |
| **基础分值** | `clue_score = (81 - givens) × 1.0` |
| **作用** | 反映信息稀疏程度；挖空越多，基础分越高 |

典型参考：Easy 约 36~46 给定；Medium 约 28~35；Hard 约 22~27；Expert 可至 17~21（须保证唯一解）。

#### 2. 求解步数（Solver Steps）

| 项 | 说明 |
|----|------|
| **定义** | 标准逻辑求解器从题面到终盘所需的**有效填数步数**（每步确定一格） |
| **计分** | `step_score = steps × 0.8` |
| **求解器要求** | 按技巧等级从低到高尝试；每步记录所用最高技巧；无法用当前允许技巧推进则判定「该档不可解」 |

#### 3. 候选数复杂度（Candidate Complexity）

| 项 | 说明 |
|----|------|
| **定义** | 题面初始化后，所有空格候选集大小的**算术平均** |
| **计分** | `candidate_score = max(0, avg_candidates - 1) × 12` |
| **解读** | 平均候选越少，题面越「直观」；≥ 4 通常意味着需系统排除 |

示例：81 格中 40 个空格，候选总数 120 → 平均 3.0 → `candidate_score = (3 - 1) × 12 = 24`。

#### 4. 逻辑技巧需求（Technique Demand）

求解器维护**技巧等级表**，解题过程中取**单次最高技巧等级** `max_technique_level`：

| 等级 | 技巧名称 | 说明 | 技巧附加分 |
|-----:|----------|------|----------:|
| 1 | Naked Single | 格内仅一个候选 | 0 |
| 2 | Hidden Single | 行/列/宫内仅一处可填 | 2 |
| 3 | Naked Pair | 两格两候选形成对 | 6 |
| 4 | Hidden Pair | 行/列/宫内两数仅两处 | 8 |
| 5 | Pointing / Claiming | 区块排除（指向/区块声明） | 14 |
| 6 | X-Wing | 行列鱼（2×2 对齐） | 22 |
| 7 | Swordfish | 3×3 鱼 | 30 |
| 8 | XY-Wing / XYZ-Wing | 翼式链 | 38 |
| 9 | Simple Coloring | 简单着色 | 45 |
| 10 | XY-Chain / 更强链 | 链式推理 | 55 |

| 项 | 说明 |
|----|------|
| **计分** | `technique_score = 技巧附加分[max_technique_level]` |
| **用途** | 区分「挖空多但只靠直观填数」与「挖空少但需高级技巧」 |

---

### 综合评分公式

```
total_score = clue_score + step_score + candidate_score + technique_score
```

各项在入库前写入元数据（可选扩展表或生成日志），便于抽检与调参。

**分档流程（伪代码）：**

```
score = 计算综合分(puzzle)
max_tech = 求解器最高技巧(puzzle)

if max_tech 需要 Expert 技巧:
    difficulty = 4
elif max_tech 需要 Hard 技巧 或 score >= 80:
    difficulty = 3
elif max_tech 需要 Medium 技巧 或 score >= 30:
    difficulty = 2
else:
    difficulty = 1

# 升档校正：技巧超出该档上限则上调
difficulty = max(difficulty, min_level_for(max_tech))
```

---

### 生成流水线（实现逻辑）

```
┌─────────────────────────────────────────────────────────────┐
│ 1. 生成完整终盘 (solution)                                   │
│    随机合法 9×9 终盘，满足数独约束                              │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. 挖空 (puzzle)                                             │
│    从终盘随机移除数字，每移除一步校验「仍唯一解」                  │
│    目标挖空数按候选档位采样（或挖到评分落入目标区间）              │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. 唯一解校验                                                 │
│    回溯 / DLX 计数解个数，≠ 1 则丢弃                           │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. 逻辑求解器评分                                             │
│    a. 初始化候选（行/列/宫排除）                                │
│    b. 记录 avg_candidates                                    │
│    c. 按技巧等级 1→10 循环求解，累计 steps 与 max_technique    │
│    d. 无法继续且未终盘 → 标记为「超出当前技巧」或丢弃             │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. 综合评分 + 分档                                            │
│    计算 total_score，应用硬性约束，得到 difficulty (1~4)        │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. 入库 puzzles.db                                           │
│    id, puzzle(81 字符串), solution(81 字符串),               │
│    difficulty, created_at                                    │
│    按档位配额抽样，避免某一档过少                               │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. 手工替换到 sudoku_app/assets/data/puzzles.db              │
└─────────────────────────────────────────────────────────────┘
```

---

### 计划模块划分（Python）

| 模块 | 职责 |
|------|------|
| `generator.py` | 终盘生成、挖空、唯一解校验 |
| `solver.py` | 候选维护、技巧检测、分步求解 |
| `techniques/` | 各技巧实现（Single → Chain） |
| `scorer.py` | 四指标计算、`total_score`、分档 |
| `db_writer.py` | 写入 SQLite，配额与去重 |
| `generate_puzzles.py` | CLI 入口：批量生成并输出 `puzzles.db` |

### 质量与配额建议

- 每档至少入库 **200** 题，重复题面（规范化后）去重。
- 生成日志记录：`givens`、`steps`、`avg_candidates`、`max_technique`、`total_score`，便于调阈值。
- 替换 Flutter 资源前，用 `sudoku_app` 的 `puzzle_repository_test` 做冒烟验证。

---

## 环境要求

- Python 3（版本以项目后续 `requirements.txt` 为准）
- 虚拟环境目录：本文件夹下的 **`.venv`**

## 虚拟环境

所有 Python 命令均应在 `.venv` 虚拟环境中执行，勿污染系统 Python。

### 首次创建（若尚未初始化）

```bash
cd sudoku_tools
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt   # 待项目补充依赖清单后执行
```

### 日常使用

```bash
cd sudoku_tools
source .venv/bin/activate
# 运行生成脚本（示例，以实际脚本为准）
# python generate_puzzles.py
```

## 目录关系（仓库内）

```
figma-flow-learn/
├── sudoku_tools/          ← 本工程（Python，.venv）
│   ├── .venv/
│   └── README.md
└── sudoku_app/            ← Flutter 原型（独立工程）
    └── assets/data/
        └── puzzles.db     ← 手工替换的目标文件
```

## 注意事项

- `.venv` 仅服务于本 Python 工程，**不要**在 `sudoku_app` 的构建流程中引用 `sudoku_tools`。
- 替换 `puzzles.db` 后，建议在 `sudoku_app` 下执行 `flutter test` 确认题库可读。
- 若需调整表结构或字段含义，须同时更新本工程的生成逻辑与 Flutter 侧的 `PuzzleRepository` / 测试。
