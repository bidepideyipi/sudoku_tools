#include "dungeon_db_writer.h"
#include "mini_sudoku_generator.h"
#include "sudoku_generator.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

const char* kUsage =
    "Usage: generate_dungeon_puzzles [options]\n"
    "\n"
    "Writes dungeon_puzzles + dungeon_chapters (no difficulty on puzzles).\n"
    "Load filter: WHERE topology=? ORDER BY RANDOM() LIMIT 1\n"
    "readme lives on dungeon_chapters (left empty for manual fill).\n"
    "\n"
    "Options:\n"
    "  --per-pool N   Puzzles per topology (default: 10)\n"
    "                 classic_9x9 also gets 4x N variety (cycles Easy..Expert dig holes)\n"
    "  --out PATH     default ../db/puzzles.db\n"
    "  --seed S\n"
    "  --verbose / -v\n"
    "  --help / -h\n";

struct ChapterSpec {
  int chapter_id;
  const char* topology;
  int difficulty;
  const char* modifiers;
  const char* title;
};

const ChapterSpec kChapters[12] = {
    {1, "mini_6x6", 1, "", "Chapter 1 · 6×6"},
    {2, "classic_9x9", 1, "", "Chapter 2 · Easy"},
    {3, "classic_9x9", 1, "combo_5", "Chapter 3 · Easy · Combo"},
    {4, "classic_9x9", 2, "", "Chapter 4 · Medium"},
    {5, "classic_9x9", 2, "combo_5", "Chapter 5 · Medium · Combo"},
    {6, "classic_9x9", 2, "trio_pad", "Chapter 6 · Medium · Trio"},
    {7, "classic_9x9", 3, "", "Chapter 7 · Hard"},
    {8, "classic_9x9", 3, "combo_5", "Chapter 8 · Hard · Combo"},
    {9, "classic_9x9", 3, "trio_pad", "Chapter 9 · Hard · Trio"},
    {10, "classic_9x9", 4, "", "Chapter 10 · Expert"},
    {11, "classic_9x9", 4, "combo_5", "Chapter 11 · Expert · Combo"},
    {12, "classic_9x9", 4, "trio_pad", "Chapter 12 · Expert · Trio"},
};

int holesForDifficulty(int level) {
  switch (level) {
    case 1:
      return 27;
    case 2:
      return 36;
    case 3:
      return 45;
    case 4:
      return 54;
    default:
      return 27;
  }
}

sudoku::Difficulty toDifficulty(int level) {
  switch (level) {
    case 1:
      return sudoku::Difficulty::Easy;
    case 2:
      return sudoku::Difficulty::Medium;
    case 3:
      return sudoku::Difficulty::Hard;
    case 4:
      return sudoku::Difficulty::Expert;
    default:
      return sudoku::Difficulty::Easy;
  }
}

void logv(bool verbose, const std::string& message) {
  if (verbose) {
    std::cerr << "[dungeon] " << message << '\n';
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  int per_pool = 10;
  std::string out_path = "../db/puzzles.db";
  bool verbose = false;
  bool use_seed = false;
  std::uint64_t seed = 0;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--per-pool" || arg == "--per-chapter") && i + 1 < argc) {
      per_pool = std::atoi(argv[++i]);
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = static_cast<std::uint64_t>(std::strtoull(argv[++i], nullptr, 10));
      use_seed = true;
    } else if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << kUsage;
      return 0;
    } else if (arg == "--chapters") {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        ++i;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n' << kUsage;
      return 1;
    }
  }

  if (per_pool <= 0) {
    std::cerr << "--per-pool must be positive\n";
    return 1;
  }

  sudoku::SudokuGenerator classic;
  sudoku::MiniSudokuGenerator mini;
  classic.SetVerbose(verbose);
  mini.SetVerbose(verbose);
  if (use_seed) {
    classic.Seed(seed);
    mini.Seed(seed ^ 0x6D696E693636ULL);
    logv(verbose, "seed=" + std::to_string(seed));
  }

  sudoku::DungeonDbWriter db(out_path);
  std::string db_error;
  if (!db.Open(&db_error)) {
    std::cerr << "failed to open database: " << db_error << '\n';
    return 1;
  }
  if (!db.ResetDungeonSchema(&db_error)) {
    std::cerr << "failed to reset dungeon schema: " << db_error << '\n';
    return 1;
  }
  logv(verbose, "reset dungeon tables");

  for (const ChapterSpec& spec : kChapters) {
    sudoku::DungeonChapterMeta meta;
    meta.chapter_id = spec.chapter_id;
    meta.topology = spec.topology;
    meta.difficulty = spec.difficulty;
    meta.modifiers = spec.modifiers;
    meta.title = spec.title;
    meta.readme = "";  // manual fill later
    if (!db.UpsertChapter(meta, &db_error)) {
      std::cerr << "upsert chapter failed: " << db_error << '\n';
      return 1;
    }
  }

  std::set<std::string> topologies;
  for (const ChapterSpec& spec : kChapters) {
    topologies.insert(spec.topology);
  }

  const auto start = std::chrono::steady_clock::now();
  int written = 0;

  for (const std::string& topology : topologies) {
    // Keep classic pool richer: 4x per_pool cycling dig-hole tiers.
    const int count = (topology == "classic_9x9") ? per_pool * 4 : per_pool;
    logv(verbose, "pool " + topology + " count=" + std::to_string(count));

    for (int i = 0; i < count; ++i) {
      sudoku::DungeonPuzzleRow row;
      row.topology = topology;

      if (topology == "mini_6x6") {
        sudoku::MiniPuzzle mini_puzzle;
        sudoku::MiniGenerateStats stats;
        while (!mini.Generate(&mini_puzzle, &stats, 14)) {
          logv(verbose, "mini6 batch exhausted, retry");
        }
        row.id = mini_puzzle.id;
        row.puzzle = mini_puzzle.puzzle;
        row.solution = mini_puzzle.solution;
        row.holes = mini_puzzle.holes;
        row.created_at_unix = mini_puzzle.created_at_unix;
      } else {
        const int level = (i % 4) + 1;
        sudoku::Puzzle classic_puzzle;
        sudoku::GenerateStats stats;
        while (!classic.Generate(toDifficulty(level), &classic_puzzle, &stats)) {
          logv(verbose, "classic batch exhausted, retry");
        }
        row.id = classic_puzzle.id;
        row.puzzle = classic_puzzle.puzzle;
        row.solution = classic_puzzle.solution;
        row.holes = holesForDifficulty(level);
        row.created_at_unix = classic_puzzle.created_at_unix;
      }

      if (!db.InsertPuzzle(row, &db_error)) {
        std::cerr << "insert puzzle failed: " << db_error << '\n';
        return 1;
      }
      ++written;
      if (verbose) {
        std::cerr << "[dungeon] ok " << topology << " #" << (i + 1) << " id=" << row.id
                  << " holes=" << row.holes << '\n';
      }
    }
  }

  if (!db.Commit(&db_error)) {
    std::cerr << "commit failed: " << db_error << '\n';
    return 1;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  std::cerr << "dungeon_wrote=" << written << " topologies=" << topologies.size()
            << " per_pool=" << per_pool << " elapsed_ms=" << elapsed.count() << " out=" << out_path
            << '\n';
  return 0;
}
