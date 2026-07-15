-- Puzzle database schema (free mode + dungeon mode)
-- Source of truth for sudoku_tools/db/puzzles.db
--
-- Free / Daily → `puzzles`
-- Dungeon     → `dungeon_puzzles` + `dungeon_chapters`（无 chapter–题槽绑定表）
--
-- 副本开局选题：按章节 topology 从 dungeon_puzzles 筛选后随机 1 题。
-- 章玩法难度 / modifiers / 游戏说明 在 dungeon_chapters（含 readme）。

-- =============================================================================
-- Free Mode / Daily
-- =============================================================================

CREATE TABLE IF NOT EXISTS puzzles (
  id TEXT PRIMARY KEY,
  puzzle TEXT NOT NULL,
  solution TEXT NOT NULL,
  difficulty INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_difficulty ON puzzles(difficulty);

-- =============================================================================
-- Dungeon Mode
-- =============================================================================

-- 副本题池（仅区分拓扑；难度信息不存此表）
CREATE TABLE IF NOT EXISTS dungeon_puzzles (
  id TEXT PRIMARY KEY,
  topology TEXT NOT NULL,                 -- 'mini_6x6' | 'classic_9x9'
  puzzle TEXT NOT NULL,
  solution TEXT NOT NULL,
  holes INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  CHECK (topology IN ('mini_6x6', 'classic_9x9')),
  CHECK (
    (topology = 'mini_6x6' AND length(puzzle) = 36 AND length(solution) = 36)
    OR
    (topology = 'classic_9x9' AND length(puzzle) = 81 AND length(solution) = 81)
  )
);

CREATE INDEX IF NOT EXISTS idx_dungeon_topology ON dungeon_puzzles(topology);

-- 章节元数据（玩法配置 + 游戏说明）
CREATE TABLE IF NOT EXISTS dungeon_chapters (
  chapter_id INTEGER PRIMARY KEY,
  topology TEXT NOT NULL,                -- 'mini_6x6' | 'classic_9x9'
  difficulty INTEGER NOT NULL,           -- 1..4（展示 / 玩法档；选题不用）
  modifiers TEXT NOT NULL DEFAULT '',   -- '' | 'combo_5' | 'trio_pad'
  title TEXT NOT NULL,
  readme TEXT NOT NULL DEFAULT '',      -- 游戏说明（人工填写，生成器留空）
  CHECK (chapter_id BETWEEN 1 AND 12),
  CHECK (topology IN ('mini_6x6', 'classic_9x9')),
  CHECK (difficulty BETWEEN 1 AND 4)
);
