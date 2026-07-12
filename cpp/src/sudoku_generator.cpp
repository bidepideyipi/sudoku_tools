#include "sudoku_generator.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <utility>

namespace sudoku {
namespace {

// --- 位掩码工具：数字 d 对应 bit (1 << d)，行/列/宫各用一个 int 标记已占用的数字 ---

/// 3×3 宫格索引（0~8）。
inline int boxIndex(int row, int col) { return (row / 3) * 3 + (col / 3); }

/// 数字 1~9 对应的掩码 bit（bit 0 未使用）。
inline int digitBit(int digit) { return 1 << digit; }

/// 统计掩码中置位个数（候选数量）。
inline int popcountMask(int mask) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcount(static_cast<unsigned>(mask));
#else
  int count = 0;
  while (mask != 0) {
    mask &= mask - 1;
    ++count;
  }
  return count;
#endif
}

/// 取掩码最低置位对应的数字（1~9）。
inline int trailingZeroBit(int mask) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctz(static_cast<unsigned>(mask));
#else
  int bit = 0;
  while (((mask >> bit) & 1) == 0) {
    ++bit;
  }
  return bit;
#endif
}

/// 数字 digit 是否与行/列/宫已有数字冲突。
inline bool masksConflict(int row_mask, int col_mask, int box_mask, int digit) {
  const int bit = digitBit(digit);
  return (row_mask & bit) || (col_mask & bit) || (box_mask & bit);
}

/// 填数/回溯时同步更新行、列、宫位掩码；place=false 表示撤销。
inline void setMasks(int row_mask[9], int col_mask[9], int box_mask[9], int row, int col,
                     int digit, bool place) {
  const int bit = digitBit(digit);
  const int bi = boxIndex(row, col);
  if (place) {
    row_mask[row] |= bit;
    col_mask[col] |= bit;
    box_mask[bi] |= bit;
  } else {
    row_mask[row] &= ~bit;
    col_mask[col] &= ~bit;
    box_mask[bi] &= ~bit;
  }
}

/// 从盘面重建行/列/宫位掩码；盘面非法（重复数字）时返回 false。
bool buildMasksFromBoard(const int board[9][9], int row_mask[9], int col_mask[9],
                         int box_mask[9]) {
  std::memset(row_mask, 0, 9 * sizeof(int));
  std::memset(col_mask, 0, 9 * sizeof(int));
  std::memset(box_mask, 0, 9 * sizeof(int));

  for (int row = 0; row < 9; ++row) {
    for (int col = 0; col < 9; ++col) {
      const int value = board[row][col];
      if (value == 0) {
        continue;
      }
      if (value < 1 || value > 9) {
        return false;
      }
      if (masksConflict(row_mask[row], col_mask[col], box_mask[boxIndex(row, col)], value)) {
        return false;
      }
      setMasks(row_mask, col_mask, box_mask, row, col, value, true);
    }
  }
  return true;
}

/// 按行优先找第一个空格，用于终盘生成（fillBoard）。
bool findEmpty(const int board[9][9], int* row, int* col) {
  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      if (board[r][c] == 0) {
        *row = r;
        *col = c;
        return true;
      }
    }
  }
  return false;
}

/// MRV：选候选最少的空格，加速唯一解计数。
bool findBestEmpty(const int board[9][9], const int row_mask[9], const int col_mask[9],
                   const int box_mask[9], int* row, int* col, int* candidate_mask) {
  int best_count = 10;
  int best_row = -1;
  int best_col = -1;
  int best_mask = 0;

  for (int r = 0; r < 9; ++r) {
    for (int c = 0; c < 9; ++c) {
      if (board[r][c] != 0) {
        continue;
      }
      const int used =
          row_mask[r] | col_mask[c] | box_mask[boxIndex(r, c)];
      const int candidates = (~used) & 0x3FE;  // bits 1..9
      const int count = popcountMask(candidates);
      if (count == 0) {
        *row = r;
        *col = c;
        *candidate_mask = 0;
        return true;
      }
      if (count < best_count) {
        best_count = count;
        best_row = r;
        best_col = c;
        best_mask = candidates;
        if (best_count == 1) {
          break;
        }
      }
    }
    if (best_count == 1) {
      break;
    }
  }

  if (best_row < 0) {
    return false;
  }
  *row = best_row;
  *col = best_col;
  *candidate_mask = best_mask;
  return true;
}

/// Fisher-Yates 洗牌，打乱 1~9 的尝试顺序。
void shuffleDigits(std::array<int, 9>& nums, std::mt19937_64* rng) {
  for (int i = 8; i > 0; --i) {
    std::uniform_int_distribution<int> dist(0, i);
    const int j = dist(*rng);
    std::swap(nums[i], nums[j]);
  }
}

/// Fisher-Yates 洗牌，打乱 81 个格子的挖空顺序。
void shufflePositions(std::array<std::pair<int, int>, 81>& positions, std::mt19937_64* rng) {
  int idx = 0;
  for (int row = 0; row < 9; row++) {
    for (int col = 0; col < 9; col++) {
      positions[idx++] = std::make_pair(row, col);
    }
  }
  for (int i = 80; i > 0; --i) {
    std::uniform_int_distribution<int> dist(0, i);
    const int j = dist(*rng);
    std::swap(positions[i], positions[j]);
  }
}

}  // namespace

// SudokuGenerator 的私有随机数源（pimpl：声明在头文件，定义在此 .cpp）。
//
// 封装 std::mt19937_64，供以下场景使用：
//   - 终盘回溯时洗牌 1~9 尝试顺序（fillBoard）
//   - 挖空时洗牌 81 个格子顺序（digHolesWithValidation）
//   - UUID v4 的 128 bit 随机填充（generateUuid）
//
// 默认用 std::random_device 播种；调用 Seed() 可固定种子以便复现同一批题目。
struct SudokuGenerator::Rng {
  explicit Rng(std::uint64_t seed) : engine(seed) {}
  std::mt19937_64 engine;
};

// 构造时以 random_device 播种 Rng；verbose 默认关闭。
SudokuGenerator::SudokuGenerator() : rng_(nullptr), verbose_(false) {
  std::random_device rd;
  std::uint64_t seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
  rng_ = new Rng(seed);
}

void SudokuGenerator::SetVerbose(bool verbose) { verbose_ = verbose; }

/// --verbose 时向 stderr 输出单批事件（batch-start / batch-exhausted 等）。
void SudokuGenerator::logBatch(const char* event, Difficulty difficulty, int attempt,
                               int max_attempts, int holes) const {
  if (!verbose_) {
    return;
  }
  const char* name = "unknown";
  switch (difficulty) {
    case Difficulty::Easy:
      name = "easy";
      break;
    case Difficulty::Medium:
      name = "medium";
      break;
    case Difficulty::Hard:
      name = "hard";
      break;
    case Difficulty::Expert:
      name = "expert";
      break;
  }
  std::cerr << "[generate] " << event << " difficulty=" << name << " holes=" << holes
            << " attempt=" << attempt << "/" << max_attempts << '\n';
}

SudokuGenerator::~SudokuGenerator() { delete rng_; }

/// 重置随机数引擎，使后续生成可复现。
void SudokuGenerator::Seed(std::uint64_t seed) {
  delete rng_;
  rng_ = new Rng(seed);
}

/// 各难度目标挖空数（81 - givens）；写入 puzzles.difficulty 对应档位。
int SudokuGenerator::holesByDifficulty(Difficulty difficulty) {
  switch (difficulty) {
    case Difficulty::Easy:
      return 30;
    case Difficulty::Medium:
      return 38;
    case Difficulty::Hard:
      return 46;
    case Difficulty::Expert:
      return 54;
    default:
      return 30;
  }
}

/// 当前 UTC 秒级时间戳，写入 puzzles.created_at。
std::int64_t SudokuGenerator::nowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

/// 9×9 盘面 → 81 字符行优先串（'0' 表示空格），对应 puzzles.puzzle / solution。
std::string SudokuGenerator::boardToString(const int board[9][9]) {
  std::string result;
  result.reserve(81);
  for (int row = 0; row < 9; ++row) {
    for (int col = 0; col < 9; ++col) {
      result.push_back(static_cast<char>('0' + board[row][col]));
    }
  }
  return result;
}

// 生成 UUID v4，作为 puzzles 表主键 id。
//
// 步骤：
//   1. 用 mt19937_64 填充 16 字节（128 bit）
//   2. 按 RFC 4122 写入 version=4、variant 固定位，其余 bit 保持随机
//   3. 格式化为 8-4-4-4-12 小写十六进制字符串
//
// 为何不会重复：
//   v4 的有效随机空间约 2^122（≈ 5.3×10^36）。
//   本工具单次批量通常在 10^2~10^3 量级；按生日悖论估算，
//   要到约 2^61 条记录才有 ~50% 碰撞概率。
//   因此题库规模下可视为唯一；数据库 PRIMARY KEY 仍作为最终兜底。
std::string SudokuGenerator::generateUuid(Rng* rng) {
  std::array<unsigned char, 16> bytes{};
  for (int i = 0; i < 16; i += 8) {
    const std::uint64_t chunk = rng->engine();
    std::memcpy(bytes.data() + i, &chunk, 8);
  }
  // version 4：第 7 字节高 4 bit 置为 0100
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
  // variant：第 9 字节高 2 bit 置为 10
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

  char buf[37];
  std::snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14],
                bytes[15]);
  return std::string(buf);
}

/// 生成合法完整终盘：清空棋盘后调用 fillBoard 回溯填数。
void SudokuGenerator::generateFullGrid(int board[9][9]) {
  std::memset(board, 0, 9 * 9 * sizeof(int));
  int row_mask[9] = {};
  int col_mask[9] = {};
  int box_mask[9] = {};
  fillBoard(board, row_mask, col_mask, box_mask);
}

/// 回溯生成终盘：找空格 → 随机顺序试 1~9 → 位掩码 O(1) 判冲突。
bool SudokuGenerator::fillBoard(int board[9][9], int row_mask[9], int col_mask[9],
                                int box_mask[9]) {
  int row = 0;
  int col = 0;
  if (!findEmpty(board, &row, &col)) {
    return true;
  }

  std::array<int, 9> nums = {{1, 2, 3, 4, 5, 6, 7, 8, 9}};
  shuffleDigits(nums, &rng_->engine);

  const int bi = boxIndex(row, col);
  for (int i = 0; i < 9; ++i) {
    const int num = nums[i];
    if (masksConflict(row_mask[row], col_mask[col], box_mask[bi], num)) {
      continue;
    }
    board[row][col] = num;
    setMasks(row_mask, col_mask, box_mask, row, col, num, true);
    if (fillBoard(board, row_mask, col_mask, box_mask)) {
      return true;
    }
    setMasks(row_mask, col_mask, box_mask, row, col, num, false);
    board[row][col] = 0;
  }
  return false;
}

/// 从终盘随机挖空 holes 格；每挖 5 格或挖满时校验唯一解，失败则整批作废。
bool SudokuGenerator::digHolesWithValidation(const int solution[9][9], int holes,
                                             int puzzle[9][9]) {
  std::memcpy(puzzle, solution, 9 * 9 * sizeof(int));

  std::array<std::pair<int, int>, 81> positions;
  shufflePositions(positions, &rng_->engine);

  int count = 0;
  for (int i = 0; i < 81; ++i) {
    if (count >= holes) {
      break;
    }
    const int row = positions[i].first;
    const int col = positions[i].second;
    if (puzzle[row][col] == 0) {
      continue;
    }
    puzzle[row][col] = 0;
    ++count;

    if (count % 5 == 0 || count == holes) {
      if (!hasUniqueSolution(puzzle)) {
        return false;
      }
    }
  }
  return true;
}

/// 回溯计数解个数；MRV 选格 + 候选 bit 迭代；count>1 时早停。
void SudokuGenerator::solveWithCount(int board[9][9], int row_mask[9], int col_mask[9],
                                     int box_mask[9], int* count) const {
  if (*count > 1) {
    return;
  }

  int row = 0;
  int col = 0;
  int candidate_mask = 0;
  if (!findBestEmpty(board, row_mask, col_mask, box_mask, &row, &col, &candidate_mask)) {
    ++(*count);
    return;
  }
  if (candidate_mask == 0) {
    return;
  }

  int mask = candidate_mask;
  while (mask != 0) {
    const int bit = mask & -mask;
    mask ^= bit;
    const int digit = trailingZeroBit(bit);
    board[row][col] = digit;
    setMasks(row_mask, col_mask, box_mask, row, col, digit, true);
    solveWithCount(board, row_mask, col_mask, box_mask, count);
    setMasks(row_mask, col_mask, box_mask, row, col, digit, false);
    board[row][col] = 0;
    if (*count > 1) {
      return;
    }
  }
}

/// 盘面是否有且仅有唯一解（解个数 == 1）。
bool SudokuGenerator::hasUniqueSolution(const int board[9][9]) const {
  int work[9][9];
  std::memcpy(work, board, 9 * 9 * sizeof(int));

  int row_mask[9] = {};
  int col_mask[9] = {};
  int box_mask[9] = {};
  if (!buildMasksFromBoard(work, row_mask, col_mask, box_mask)) {
    return false;
  }

  int count = 0;
  solveWithCount(work, row_mask, col_mask, box_mask, &count);
  return count == 1;
}

// 单批生成一题：新终盘 → 挖空+唯一解校验，最多 max_attempts 次；失败由 main 开新一批重试。
bool SudokuGenerator::Generate(Difficulty difficulty, Puzzle* out, GenerateStats* stats) {
  if (out == nullptr) {
    return false;
  }

  const int max_attempts = 5000;
  const int holes = holesByDifficulty(difficulty);
  int solution[9][9];
  int puzzle[9][9];

  if (stats != nullptr) {
    stats->max_attempts = max_attempts;
    stats->attempts_used = 0;
    stats->holes = holes;
  }

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    generateFullGrid(solution);
    if (digHolesWithValidation(solution, holes, puzzle)) {
      if (stats != nullptr) {
        stats->attempts_used = attempt + 1;
      }
      out->id = generateUuid(rng_);
      out->puzzle = boardToString(puzzle);
      out->solution = boardToString(solution);
      out->difficulty = difficulty;
      out->created_at_unix = nowUnix();
      return true;
    }
  }

  if (stats != nullptr) {
    stats->attempts_used = max_attempts;
  }
  if (verbose_) {
    logBatch("batch-exhausted", difficulty, max_attempts, max_attempts, holes);
  }
  return false;
}

}  // namespace sudoku
