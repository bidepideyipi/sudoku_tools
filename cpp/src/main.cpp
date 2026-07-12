#include "sudoku_generator.h"

#include "puzzle_db_writer.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

const char* kUsage =
    "Usage: generate_puzzles [--count N] [--seed S] [--benchmark] [--verbose]\n"
    "                        [--difficulty easy|medium|hard|expert|all]\n"
    "                        [--out PATH]\n"
    "\n"
    "Examples:\n"
    "  ./generate_puzzles --count 10 --difficulty medium\n"
    "  ./generate_puzzles --count 100 --difficulty easy --out ../db/puzzles.db\n"
    "  ./generate_puzzles --verbose --count 40 --difficulty all --out ../db/puzzles.db\n"
    "  ./generate_puzzles --benchmark --count 100 --difficulty expert\n";

bool parseDifficulty(const std::string& name, sudoku::Difficulty* out) {
  if (name == "easy") {
    *out = sudoku::Difficulty::Easy;
  } else if (name == "medium") {
    *out = sudoku::Difficulty::Medium;
  } else if (name == "hard") {
    *out = sudoku::Difficulty::Hard;
  } else if (name == "expert") {
    *out = sudoku::Difficulty::Expert;
  } else {
    return false;
  }
  return true;
}

const char* difficultyName(sudoku::Difficulty difficulty) {
  switch (difficulty) {
    case sudoku::Difficulty::Easy:
      return "easy";
    case sudoku::Difficulty::Medium:
      return "medium";
    case sudoku::Difficulty::Hard:
      return "hard";
    case sudoku::Difficulty::Expert:
      return "expert";
    default:
      return "unknown";
  }
}

void printPuzzleTsv(const sudoku::Puzzle& puzzle) {
  std::cout << puzzle.id << '\t' << difficultyName(puzzle.difficulty) << '\t'
            << puzzle.created_at_unix << '\t' << puzzle.puzzle << '\t' << puzzle.solution
            << '\n';
}

void logMain(bool verbose, const std::string& message) {
  if (!verbose) {
    return;
  }
  std::cerr << "[main] " << message << '\n';
}

}  // namespace

// 示例：
//   ./generate_puzzles --verbose --count 400 --difficulty all --out ../db/puzzles.db
//
// 参数说明：
//   --verbose      向 stderr 输出调试日志（单批进度、失败重试、写入数据库等）
//   --count 400    目标成功题数；单批达 maxAttempts 失败后自动开新一批，直到凑满 400 题
//   --difficulty all
//                  难度：easy | medium | hard | expert | all
//                  all 按 easy→medium→hard→expert 循环分配（400 题约各 100 题）
//   --out PATH     写入 SQLite；写入某难度前会先 DELETE 该难度的旧数据
//   --benchmark    性能测试模式：不向 stdout 打印题目 TSV，仅在 stderr 输出
//                  generated / batch_failures / elapsed_ms / avg_ms 统计
//
int main(int argc, char* argv[]) {
  int count = 1;
  bool benchmark = false;
  bool verbose = false;
  bool use_seed = false;
  std::uint64_t seed = 0;
  std::string difficulty_arg = "medium";
  std::string out_path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--count" && i + 1 < argc) {
      count = std::atoi(argv[++i]);
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = static_cast<std::uint64_t>(std::strtoull(argv[++i], nullptr, 10));
      use_seed = true;
    } else if (arg == "--benchmark") {
      benchmark = true;
    } else if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    } else if (arg == "--difficulty" && i + 1 < argc) {
      difficulty_arg = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << kUsage;
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n' << kUsage;
      return 1;
    }
  }

  if (count <= 0) {
    std::cerr << "--count must be positive\n";
    return 1;
  }

  if (difficulty_arg != "easy" && difficulty_arg != "medium" && difficulty_arg != "hard" &&
      difficulty_arg != "expert" && difficulty_arg != "all") {
    std::cerr << "Invalid --difficulty: " << difficulty_arg << '\n';
    return 1;
  }

  sudoku::SudokuGenerator generator;
  generator.SetVerbose(verbose);
  if (use_seed) {
    generator.Seed(seed);
    logMain(verbose, "seed=" + std::to_string(seed));
  }

  logMain(verbose, "target_count=" + std::to_string(count) + " difficulty=" + difficulty_arg +
                        (out_path.empty() ? "" : " out=" + out_path));

  std::unique_ptr<sudoku::PuzzleDbWriter> db_writer;
  if (!out_path.empty()) {
    db_writer.reset(new sudoku::PuzzleDbWriter(out_path));
    std::string db_error;
    if (!db_writer->Open(&db_error)) {
      std::cerr << "failed to open database: " << db_error << '\n';
      return 1;
    }
    logMain(verbose, "database opened: " + out_path);

    if (difficulty_arg != "all") {
      sudoku::Difficulty difficulty;
      if (!parseDifficulty(difficulty_arg, &difficulty)) {
        std::cerr << "Invalid --difficulty: " << difficulty_arg << '\n';
        return 1;
      }
      if (!db_writer->ClearDifficulty(difficulty, &db_error)) {
        std::cerr << "failed to clear difficulty: " << db_error << '\n';
        return 1;
      }
      logMain(verbose, std::string("cleared difficulty=") + difficultyName(difficulty));
    }
  }

  std::array<sudoku::Difficulty, 4> levels = {{
      sudoku::Difficulty::Easy,
      sudoku::Difficulty::Medium,
      sudoku::Difficulty::Hard,
      sudoku::Difficulty::Expert,
  }};

  const auto start = std::chrono::steady_clock::now();
  int generated = 0;
  int batch_failures = 0;

  sudoku::Difficulty single_difficulty = sudoku::Difficulty::Medium;
  if (difficulty_arg != "all") {
    if (!parseDifficulty(difficulty_arg, &single_difficulty)) {
      std::cerr << "Invalid --difficulty: " << difficulty_arg << '\n';
      return 1;
    }
  }

  while (generated < count) {
    sudoku::Difficulty difficulty;
    if (difficulty_arg == "all") {
      difficulty = levels[static_cast<std::size_t>(generated % levels.size())];
    } else {
      difficulty = single_difficulty;
    }

    sudoku::Puzzle puzzle;
    sudoku::GenerateStats stats;
    const auto batch_start = std::chrono::steady_clock::now();
    if (!generator.Generate(difficulty, &puzzle, &stats)) {
      ++batch_failures;
      const auto batch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - batch_start);
      if (verbose) {
        std::cerr << "[main] batch-fail slot=" << (generated + 1) << "/"
                  << count << " difficulty=" << difficultyName(difficulty)
                  << " used_attempts=" << stats.attempts_used << "/"
                  << stats.max_attempts << " holes=" << stats.holes
                  << " elapsed_ms=" << batch_ms.count() << " -> retry new batch\n";
      }
      continue;
    }

    ++generated;
    const auto batch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - batch_start);

    if (verbose) {
      std::cerr << "[main] batch-ok slot=" << generated << "/" << count
                << " difficulty=" << difficultyName(difficulty) << " id=" << puzzle.id
                << " used_attempts=" << stats.attempts_used << "/" << stats.max_attempts
                << " holes=" << stats.holes << " elapsed_ms=" << batch_ms.count() << '\n';
    }

    if (db_writer != nullptr) {
      std::string db_error;
      if (!db_writer->Insert(puzzle, &db_error)) {
        std::cerr << "database insert failed: " << db_error << '\n';
        return 1;
      }
    }

    if (!benchmark && out_path.empty()) {
      printPuzzleTsv(puzzle);
    }
  }

  if (db_writer != nullptr) {
    std::string db_error;
    if (!db_writer->Commit(&db_error)) {
      std::cerr << "database commit failed: " << db_error << '\n';
      return 1;
    }
    std::cerr << "wrote " << generated << " puzzle(s) to " << out_path << '\n';
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  if (benchmark || count > 1 || verbose) {
    std::cerr << "generated=" << generated << " batch_failures=" << batch_failures
              << " elapsed_ms=" << elapsed.count() << " avg_ms="
              << (generated > 0 ? static_cast<double>(elapsed.count()) / generated : 0.0)
              << '\n';
  }

  return 0;
}
