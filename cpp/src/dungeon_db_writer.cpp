#include "dungeon_db_writer.h"

#include <sqlite3.h>

namespace sudoku {
namespace {

std::string SqliteErrorMessage(sqlite3* db) {
  if (db == nullptr) {
    return "sqlite3 error (null db)";
  }
  const char* message = sqlite3_errmsg(db);
  return message != nullptr ? std::string(message) : "unknown sqlite3 error";
}

bool ExecSql(sqlite3* db, const char* sql, std::string* error) {
  char* err_msg = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    if (error != nullptr) {
      if (err_msg != nullptr) {
        *error = err_msg;
        sqlite3_free(err_msg);
      } else {
        *error = SqliteErrorMessage(db);
      }
    } else if (err_msg != nullptr) {
      sqlite3_free(err_msg);
    }
    return false;
  }
  return true;
}

}  // namespace

DungeonDbWriter::DungeonDbWriter(std::string db_path)
    : db_path_(std::move(db_path)),
      db_(nullptr),
      insert_puzzle_stmt_(nullptr),
      insert_chapter_stmt_(nullptr),
      in_transaction_(false) {}

DungeonDbWriter::~DungeonDbWriter() {
  if (insert_puzzle_stmt_ != nullptr) {
    sqlite3_finalize(static_cast<sqlite3_stmt*>(insert_puzzle_stmt_));
  }
  if (insert_chapter_stmt_ != nullptr) {
    sqlite3_finalize(static_cast<sqlite3_stmt*>(insert_chapter_stmt_));
  }
  if (db_ != nullptr) {
    if (in_transaction_) {
      sqlite3_exec(static_cast<sqlite3*>(db_), "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    sqlite3_close(static_cast<sqlite3*>(db_));
  }
}

bool DungeonDbWriter::Open(std::string* error) {
  sqlite3* db = nullptr;
  if (sqlite3_open(db_path_.c_str(), &db) != SQLITE_OK) {
    if (error != nullptr) {
      *error = db != nullptr ? SqliteErrorMessage(db) : "failed to open sqlite database";
    }
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return false;
  }
  db_ = db;
  // Schema + prepared statements come from ResetDungeonSchema (or EnsureSchema after
  // migration). Preparing here would fail against an older on-disk DDL.
  return true;
}

bool DungeonDbWriter::EnsureSchema(std::string* error) {
  if (!ExecSql(static_cast<sqlite3*>(db_), "DROP TABLE IF EXISTS dungeon_chapter_puzzles;",
               error)) {
    return false;
  }

  static const char* kSchemaSql =
      "CREATE TABLE IF NOT EXISTS dungeon_puzzles ("
      "  id TEXT PRIMARY KEY,"
      "  topology TEXT NOT NULL,"
      "  puzzle TEXT NOT NULL,"
      "  solution TEXT NOT NULL,"
      "  holes INTEGER NOT NULL,"
      "  created_at INTEGER NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_dungeon_topology ON dungeon_puzzles(topology);"
      "CREATE TABLE IF NOT EXISTS dungeon_chapters ("
      "  chapter_id INTEGER PRIMARY KEY,"
      "  topology TEXT NOT NULL,"
      "  difficulty INTEGER NOT NULL,"
      "  modifiers TEXT NOT NULL DEFAULT '',"
      "  title TEXT NOT NULL,"
      "  readme TEXT NOT NULL DEFAULT ''"
      ");";

  return ExecSql(static_cast<sqlite3*>(db_), kSchemaSql, error);
}

bool DungeonDbWriter::ResetPrepare(std::string* error) {
  if (insert_puzzle_stmt_ != nullptr) {
    sqlite3_finalize(static_cast<sqlite3_stmt*>(insert_puzzle_stmt_));
    insert_puzzle_stmt_ = nullptr;
  }
  if (insert_chapter_stmt_ != nullptr) {
    sqlite3_finalize(static_cast<sqlite3_stmt*>(insert_chapter_stmt_));
    insert_chapter_stmt_ = nullptr;
  }

  sqlite3* db = static_cast<sqlite3*>(db_);
  sqlite3_stmt* puzzle_stmt = nullptr;
  const char* puzzle_sql =
      "INSERT INTO dungeon_puzzles "
      "(id, topology, puzzle, solution, holes, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
  if (sqlite3_prepare_v2(db, puzzle_sql, -1, &puzzle_stmt, nullptr) != SQLITE_OK) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(db);
    }
    return false;
  }
  insert_puzzle_stmt_ = puzzle_stmt;

  sqlite3_stmt* chapter_stmt = nullptr;
  const char* chapter_sql =
      "INSERT INTO dungeon_chapters "
      "(chapter_id, topology, difficulty, modifiers, title, readme) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
      "ON CONFLICT(chapter_id) DO UPDATE SET "
      "topology=excluded.topology, difficulty=excluded.difficulty, "
      "modifiers=excluded.modifiers, title=excluded.title, "
      "readme=excluded.readme;";
  if (sqlite3_prepare_v2(db, chapter_sql, -1, &chapter_stmt, nullptr) != SQLITE_OK) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(db);
    }
    return false;
  }
  insert_chapter_stmt_ = chapter_stmt;
  return true;
}

bool DungeonDbWriter::BeginTransaction(std::string* error) {
  if (in_transaction_) {
    return true;
  }
  if (!ExecSql(static_cast<sqlite3*>(db_), "BEGIN IMMEDIATE;", error)) {
    return false;
  }
  in_transaction_ = true;
  return true;
}

bool DungeonDbWriter::CommitTransaction(std::string* error) {
  if (!in_transaction_) {
    return true;
  }
  if (!ExecSql(static_cast<sqlite3*>(db_), "COMMIT;", error)) {
    return false;
  }
  in_transaction_ = false;
  return true;
}

bool DungeonDbWriter::ResetDungeonSchema(std::string* error) {
  if (!BeginTransaction(error)) {
    return false;
  }
  if (!ExecSql(static_cast<sqlite3*>(db_), "DROP TABLE IF EXISTS dungeon_chapter_puzzles;",
               error) ||
      !ExecSql(static_cast<sqlite3*>(db_), "DROP TABLE IF EXISTS dungeon_puzzles;", error) ||
      !ExecSql(static_cast<sqlite3*>(db_), "DROP TABLE IF EXISTS dungeon_chapters;", error) ||
      !CommitTransaction(error) || !EnsureSchema(error) || !ResetPrepare(error)) {
    return false;
  }
  return true;
}

bool DungeonDbWriter::UpsertChapter(const DungeonChapterMeta& meta, std::string* error) {
  if (!BeginTransaction(error)) {
    return false;
  }
  sqlite3_stmt* stmt = static_cast<sqlite3_stmt*>(insert_chapter_stmt_);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  sqlite3_bind_int(stmt, 1, meta.chapter_id);
  sqlite3_bind_text(stmt, 2, meta.topology.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, meta.difficulty);
  sqlite3_bind_text(stmt, 4, meta.modifiers.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, meta.title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, meta.readme.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(static_cast<sqlite3*>(db_));
    }
    return false;
  }
  return true;
}

bool DungeonDbWriter::InsertPuzzle(const DungeonPuzzleRow& row, std::string* error) {
  if (!BeginTransaction(error)) {
    return false;
  }
  sqlite3_stmt* stmt = static_cast<sqlite3_stmt*>(insert_puzzle_stmt_);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  sqlite3_bind_text(stmt, 1, row.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, row.topology.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, row.puzzle.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, row.solution.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 5, row.holes);
  sqlite3_bind_int64(stmt, 6, row.created_at_unix);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(static_cast<sqlite3*>(db_));
    }
    return false;
  }
  return true;
}

bool DungeonDbWriter::Commit(std::string* error) { return CommitTransaction(error); }

}  // namespace sudoku
