#include "puzzle_db_writer.h"

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

PuzzleDbWriter::PuzzleDbWriter(std::string db_path)
    : db_path_(std::move(db_path)),
      db_(nullptr),
      insert_stmt_(nullptr),
      in_transaction_(false) {}

PuzzleDbWriter::~PuzzleDbWriter() {
  if (insert_stmt_ != nullptr) {
    sqlite3_finalize(static_cast<sqlite3_stmt*>(insert_stmt_));
    insert_stmt_ = nullptr;
  }
  if (db_ != nullptr) {
    if (in_transaction_) {
      sqlite3_exec(static_cast<sqlite3*>(db_), "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    sqlite3_close(static_cast<sqlite3*>(db_));
    db_ = nullptr;
  }
}

bool PuzzleDbWriter::Open(std::string* error) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open(db_path_.c_str(), &db);
  if (rc != SQLITE_OK) {
    if (error != nullptr) {
      *error = db != nullptr ? SqliteErrorMessage(db) : "failed to open sqlite database";
    }
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return false;
  }

  db_ = db;
  if (!EnsureSchema(error)) {
    return false;
  }

  const char* insert_sql =
      "INSERT INTO puzzles (id, puzzle, solution, difficulty, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5);";
  sqlite3_stmt* stmt = nullptr;
  const int prep_rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr);
  if (prep_rc != SQLITE_OK) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(db);
    }
    return false;
  }
  insert_stmt_ = stmt;
  return true;
}

bool PuzzleDbWriter::EnsureSchema(std::string* error) {
  static const char* kSchemaSql =
      "CREATE TABLE IF NOT EXISTS puzzles ("
      "  id TEXT PRIMARY KEY,"
      "  puzzle TEXT NOT NULL,"
      "  solution TEXT NOT NULL,"
      "  difficulty INTEGER NOT NULL,"
      "  created_at INTEGER NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_difficulty ON puzzles(difficulty);";

  return ExecSql(static_cast<sqlite3*>(db_), kSchemaSql, error);
}

bool PuzzleDbWriter::BeginTransaction(std::string* error) {
  if (in_transaction_) {
    return true;
  }
  if (!ExecSql(static_cast<sqlite3*>(db_), "BEGIN IMMEDIATE;", error)) {
    return false;
  }
  in_transaction_ = true;
  return true;
}

bool PuzzleDbWriter::CommitTransaction(std::string* error) {
  if (!in_transaction_) {
    return true;
  }
  if (!ExecSql(static_cast<sqlite3*>(db_), "COMMIT;", error)) {
    return false;
  }
  in_transaction_ = false;
  return true;
}

bool PuzzleDbWriter::ClearDifficultyIfNeeded(Difficulty difficulty, std::string* error) {
  const int level = static_cast<int>(difficulty);
  if (cleared_difficulties_.count(level) != 0) {
    return true;
  }

  if (!BeginTransaction(error)) {
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  sqlite3* db = static_cast<sqlite3*>(db_);
  const char* delete_sql = "DELETE FROM puzzles WHERE difficulty = ?1;";
  const int prep_rc = sqlite3_prepare_v2(db, delete_sql, -1, &stmt, nullptr);
  if (prep_rc != SQLITE_OK) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(db);
    }
    return false;
  }

  sqlite3_bind_int(stmt, 1, level);
  const int step_rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (step_rc != SQLITE_DONE) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(db);
    }
    return false;
  }

  cleared_difficulties_.insert(level);
  return true;
}

bool PuzzleDbWriter::ClearDifficulty(Difficulty difficulty, std::string* error) {
  return ClearDifficultyIfNeeded(difficulty, error);
}

bool PuzzleDbWriter::Commit(std::string* error) {
  return CommitTransaction(error);
}

bool PuzzleDbWriter::Insert(const Puzzle& puzzle, std::string* error) {
  if (db_ == nullptr || insert_stmt_ == nullptr) {
    if (error != nullptr) {
      *error = "database is not open";
    }
    return false;
  }

  if (!ClearDifficultyIfNeeded(puzzle.difficulty, error)) {
    return false;
  }
  if (!BeginTransaction(error)) {
    return false;
  }

  sqlite3_stmt* stmt = static_cast<sqlite3_stmt*>(insert_stmt_);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);

  sqlite3_bind_text(stmt, 1, puzzle.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, puzzle.puzzle.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, puzzle.solution.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, static_cast<int>(puzzle.difficulty));
  sqlite3_bind_int64(stmt, 5, puzzle.created_at_unix);

  const int step_rc = sqlite3_step(stmt);
  if (step_rc != SQLITE_DONE) {
    if (error != nullptr) {
      *error = SqliteErrorMessage(static_cast<sqlite3*>(db_));
    }
    return false;
  }
  return true;
}

}  // namespace sudoku
