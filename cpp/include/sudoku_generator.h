#ifndef SUDOKU_TOOLS_SUDOKU_GENERATOR_H
#define SUDOKU_TOOLS_SUDOKU_GENERATOR_H

#include <cstdint>
#include <string>

namespace sudoku {

enum class Difficulty : int {
  Easy = 1,
  Medium = 2,
  Hard = 3,
  Expert = 4,
};

struct Puzzle {
  std::string id;
  std::string puzzle;    // 81 chars, row-major, '0' = empty
  std::string solution;  // 81 chars, row-major
  Difficulty difficulty;
  std::int64_t created_at_unix;
};

/// 单批 [Generate] 的统计信息（供调试日志使用）。
struct GenerateStats {
  int max_attempts = 0;
  int attempts_used = 0;
  int holes = 0;
};

/// 数独生成器，算法对齐 Go 版 SudokuGenerator：
/// https://github.com/bidepideyipi/sudoku/blob/main/server/internal/generator/sudoku.go
///
/// 性能优化：位掩码约束、栈上数组、解计数早停（>1 即退出）。
class SudokuGenerator {
 public:
  SudokuGenerator();
  ~SudokuGenerator();

  SudokuGenerator(const SudokuGenerator&) = delete;
  SudokuGenerator& operator=(const SudokuGenerator&) = delete;

  /// 生成一题；单批最多 maxAttempts 次（Expert 5000，其余 100），失败返回 false。
  bool Generate(Difficulty difficulty, Puzzle* out, GenerateStats* stats = nullptr);

  /// 开启后向 stderr 输出单批内部进度（默认关闭）。
  void SetVerbose(bool verbose);

  /// 设置随机种子（默认 std::random_device）。
  void Seed(std::uint64_t seed);

 private:
  struct Rng;
  void logBatch(const char* event, Difficulty difficulty, int attempt, int max_attempts,
                int holes) const;

  void generateFullGrid(int board[9][9]);
  bool fillBoard(int board[9][9], int row_mask[9], int col_mask[9], int box_mask[9]);
  bool digHolesWithValidation(const int solution[9][9], int holes, int puzzle[9][9]);
  bool hasUniqueSolution(const int board[9][9]) const;
  void solveWithCount(int board[9][9], int row_mask[9], int col_mask[9], int box_mask[9],
                      int* count) const;

  static int holesByDifficulty(Difficulty difficulty);
  static std::string boardToString(const int board[9][9]);
  static std::string generateUuid(Rng* rng);
  static std::int64_t nowUnix();

  Rng* rng_;
  bool verbose_;
};

}  // namespace sudoku

#endif  // SUDOKU_TOOLS_SUDOKU_GENERATOR_H
