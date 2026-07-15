#ifndef SUDOKU_TOOLS_DUNGEON_DB_WRITER_H
#define SUDOKU_TOOLS_DUNGEON_DB_WRITER_H

#include <cstdint>
#include <string>

namespace sudoku {

struct DungeonPuzzleRow {
  std::string id;
  std::string topology;  // mini_6x6 | classic_9x9
  std::string puzzle;
  std::string solution;
  int holes = 0;
  std::int64_t created_at_unix = 0;
};

struct DungeonChapterMeta {
  int chapter_id = 0;
  std::string topology;
  int difficulty = 1;
  std::string modifiers;  // '' | combo_5 | trio_pad
  std::string title;
  std::string readme;  // leave empty for manual fill
};

/// 写入副本题库（不改动 free `puzzles`）。
class DungeonDbWriter {
 public:
  explicit DungeonDbWriter(std::string db_path);
  ~DungeonDbWriter();

  DungeonDbWriter(const DungeonDbWriter&) = delete;
  DungeonDbWriter& operator=(const DungeonDbWriter&) = delete;

  bool Open(std::string* error);
  bool ResetDungeonSchema(std::string* error);
  bool UpsertChapter(const DungeonChapterMeta& meta, std::string* error);
  bool InsertPuzzle(const DungeonPuzzleRow& row, std::string* error);
  bool Commit(std::string* error);

 private:
  bool EnsureSchema(std::string* error);
  bool ResetPrepare(std::string* error);
  bool BeginTransaction(std::string* error);
  bool CommitTransaction(std::string* error);

  std::string db_path_;
  void* db_;
  void* insert_puzzle_stmt_;
  void* insert_chapter_stmt_;
  bool in_transaction_;
};

}  // namespace sudoku

#endif  // SUDOKU_TOOLS_DUNGEON_DB_WRITER_H
