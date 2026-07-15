#ifndef SUDOKU_TOOLS_MINI_SUDOKU_GENERATOR_H
#define SUDOKU_TOOLS_MINI_SUDOKU_GENERATOR_H

#include <cstdint>
#include <string>

namespace sudoku {

/// 6×6 数独（宫 2×3，数字 1~6），供副本第 1 章使用。
struct MiniPuzzle {
  std::string id;
  std::string puzzle;    // 36 chars
  std::string solution;  // 36 chars
  int holes = 0;
  std::int64_t created_at_unix = 0;
};

struct MiniGenerateStats {
  int max_attempts = 0;
  int attempts_used = 0;
  int holes = 0;
};

class MiniSudokuGenerator {
 public:
  MiniSudokuGenerator();
  ~MiniSudokuGenerator();

  MiniSudokuGenerator(const MiniSudokuGenerator&) = delete;
  MiniSudokuGenerator& operator=(const MiniSudokuGenerator&) = delete;

  /// 生成一题 6×6；默认挖空 14（36 格中约 39%）。
  bool Generate(MiniPuzzle* out, MiniGenerateStats* stats = nullptr, int holes = 14);

  void SetVerbose(bool verbose);
  void Seed(std::uint64_t seed);

 private:
  struct Rng;

  void generateFullGrid(int board[6][6]);
  bool fillBoard(int board[6][6], int row_mask[6], int col_mask[6], int box_mask[6]);
  bool digHolesWithValidation(const int solution[6][6], int holes, int puzzle[6][6]);
  bool hasUniqueSolution(const int board[6][6]) const;
  void solveWithCount(int board[6][6], int row_mask[6], int col_mask[6], int box_mask[6],
                      int* count) const;

  static std::string boardToString(const int board[6][6]);
  static std::string generateUuid(Rng* rng);
  static std::int64_t nowUnix();

  Rng* rng_;
  bool verbose_;
};

}  // namespace sudoku

#endif  // SUDOKU_TOOLS_MINI_SUDOKU_GENERATOR_H
