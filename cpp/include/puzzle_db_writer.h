#ifndef SUDOKU_TOOLS_PUZZLE_DB_WRITER_H
#define SUDOKU_TOOLS_PUZZLE_DB_WRITER_H

#include <set>
#include <string>

#include "sudoku_generator.h"

namespace sudoku {

/// 将 [Puzzle] 写入 SQLite，写入某难度前删除该难度旧数据。
class PuzzleDbWriter {
 public:
  explicit PuzzleDbWriter(std::string db_path);
  ~PuzzleDbWriter();

  PuzzleDbWriter(const PuzzleDbWriter&) = delete;
  PuzzleDbWriter& operator=(const PuzzleDbWriter&) = delete;

  bool Open(std::string* error);

  /// 删除指定难度的全部题目（同一会话内重复调用安全）。
  bool ClearDifficulty(Difficulty difficulty, std::string* error);

  bool Insert(const Puzzle& puzzle, std::string* error);
  bool Commit(std::string* error);

 private:
  bool EnsureSchema(std::string* error);
  bool ClearDifficultyIfNeeded(Difficulty difficulty, std::string* error);
  bool BeginTransaction(std::string* error);
  bool CommitTransaction(std::string* error);

  std::string db_path_;
  void* db_;           // sqlite3*
  void* insert_stmt_;  // sqlite3_stmt*
  std::set<int> cleared_difficulties_;
  bool in_transaction_;
};

}  // namespace sudoku

#endif  // SUDOKU_TOOLS_PUZZLE_DB_WRITER_H
