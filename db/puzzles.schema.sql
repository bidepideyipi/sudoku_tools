-- Reverse-engineered DDL from SQLite database
-- Source: /Users/anthony/Documents/github/figma-flow-learn/sudoku_tools/db/puzzles.db
-- Generated: 2026-07-11T12:42:20Z

-- TABLE
CREATE TABLE puzzles (
		id TEXT PRIMARY KEY,
		puzzle TEXT NOT NULL,
		solution TEXT NOT NULL,
		difficulty INTEGER NOT NULL,
		created_at INTEGER NOT NULL
	);

-- INDEX
CREATE INDEX idx_difficulty ON puzzles(difficulty);
