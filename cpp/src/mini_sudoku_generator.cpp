#include "mini_sudoku_generator.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <utility>

namespace sudoku {
namespace {

constexpr int kSize = 6;
constexpr int kDigitMax = 6;
constexpr int kBoxRows = 2;
constexpr int kBoxCols = 3;
constexpr int kBoxesAcross = kSize / kBoxCols;  // 2
constexpr int kBoxCount = (kSize * kSize) / (kBoxRows * kBoxCols);  // 6
constexpr int kCellCount = kSize * kSize;  // 36
constexpr int kDigitBits = 0x7E;  // bits 1..6

inline int boxIndex(int row, int col) {
  return (row / kBoxRows) * kBoxesAcross + (col / kBoxCols);
}

inline int digitBit(int digit) { return 1 << digit; }

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

inline bool masksConflict(int row_mask, int col_mask, int box_mask, int digit) {
  const int bit = digitBit(digit);
  return (row_mask & bit) || (col_mask & bit) || (box_mask & bit);
}

inline void setMasks(int row_mask[6], int col_mask[6], int box_mask[6], int row, int col,
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

bool buildMasksFromBoard(const int board[6][6], int row_mask[6], int col_mask[6],
                         int box_mask[6]) {
  std::memset(row_mask, 0, kSize * sizeof(int));
  std::memset(col_mask, 0, kSize * sizeof(int));
  std::memset(box_mask, 0, kBoxCount * sizeof(int));

  for (int row = 0; row < kSize; ++row) {
    for (int col = 0; col < kSize; ++col) {
      const int value = board[row][col];
      if (value == 0) {
        continue;
      }
      if (value < 1 || value > kDigitMax) {
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

bool findEmpty(const int board[6][6], int* row, int* col) {
  for (int r = 0; r < kSize; ++r) {
    for (int c = 0; c < kSize; ++c) {
      if (board[r][c] == 0) {
        *row = r;
        *col = c;
        return true;
      }
    }
  }
  return false;
}

bool findBestEmpty(const int board[6][6], const int row_mask[6], const int col_mask[6],
                   const int box_mask[6], int* row, int* col, int* candidate_mask) {
  int best_count = kDigitMax + 1;
  int best_row = -1;
  int best_col = -1;
  int best_mask = 0;

  for (int r = 0; r < kSize; ++r) {
    for (int c = 0; c < kSize; ++c) {
      if (board[r][c] != 0) {
        continue;
      }
      const int used = row_mask[r] | col_mask[c] | box_mask[boxIndex(r, c)];
      const int candidates = (~used) & kDigitBits;
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

void shuffleDigits(std::array<int, 6>& nums, std::mt19937_64* rng) {
  for (int i = 5; i > 0; --i) {
    std::uniform_int_distribution<int> dist(0, i);
    const int j = dist(*rng);
    std::swap(nums[i], nums[j]);
  }
}

void shufflePositions(std::array<std::pair<int, int>, 36>& positions, std::mt19937_64* rng) {
  int idx = 0;
  for (int row = 0; row < kSize; ++row) {
    for (int col = 0; col < kSize; ++col) {
      positions[idx++] = std::make_pair(row, col);
    }
  }
  for (int i = kCellCount - 1; i > 0; --i) {
    std::uniform_int_distribution<int> dist(0, i);
    const int j = dist(*rng);
    std::swap(positions[i], positions[j]);
  }
}

}  // namespace

struct MiniSudokuGenerator::Rng {
  explicit Rng(std::uint64_t seed) : engine(seed) {}
  std::mt19937_64 engine;
};

MiniSudokuGenerator::MiniSudokuGenerator() : rng_(nullptr), verbose_(false) {
  std::random_device rd;
  std::uint64_t seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
  rng_ = new Rng(seed);
}

MiniSudokuGenerator::~MiniSudokuGenerator() { delete rng_; }

void MiniSudokuGenerator::SetVerbose(bool verbose) { verbose_ = verbose; }

void MiniSudokuGenerator::Seed(std::uint64_t seed) {
  delete rng_;
  rng_ = new Rng(seed);
}

std::int64_t MiniSudokuGenerator::nowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string MiniSudokuGenerator::boardToString(const int board[6][6]) {
  std::string result;
  result.reserve(kCellCount);
  for (int row = 0; row < kSize; ++row) {
    for (int col = 0; col < kSize; ++col) {
      result.push_back(static_cast<char>('0' + board[row][col]));
    }
  }
  return result;
}

std::string MiniSudokuGenerator::generateUuid(Rng* rng) {
  std::array<unsigned char, 16> bytes{};
  for (int i = 0; i < 16; i += 8) {
    const std::uint64_t chunk = rng->engine();
    std::memcpy(bytes.data() + i, &chunk, 8);
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

  char buf[37];
  std::snprintf(buf, sizeof(buf),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14],
                bytes[15]);
  return std::string(buf);
}

void MiniSudokuGenerator::generateFullGrid(int board[6][6]) {
  std::memset(board, 0, kSize * kSize * sizeof(int));
  int row_mask[6] = {};
  int col_mask[6] = {};
  int box_mask[6] = {};
  fillBoard(board, row_mask, col_mask, box_mask);
}

bool MiniSudokuGenerator::fillBoard(int board[6][6], int row_mask[6], int col_mask[6],
                                    int box_mask[6]) {
  int row = 0;
  int col = 0;
  if (!findEmpty(board, &row, &col)) {
    return true;
  }

  std::array<int, 6> nums = {{1, 2, 3, 4, 5, 6}};
  shuffleDigits(nums, &rng_->engine);

  const int bi = boxIndex(row, col);
  for (int i = 0; i < kDigitMax; ++i) {
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

bool MiniSudokuGenerator::digHolesWithValidation(const int solution[6][6], int holes,
                                                 int puzzle[6][6]) {
  std::memcpy(puzzle, solution, kSize * kSize * sizeof(int));

  std::array<std::pair<int, int>, 36> positions;
  shufflePositions(positions, &rng_->engine);

  int count = 0;
  for (int i = 0; i < kCellCount; ++i) {
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

    if (count % 2 == 0 || count == holes) {
      if (!hasUniqueSolution(puzzle)) {
        return false;
      }
    }
  }
  return true;
}

void MiniSudokuGenerator::solveWithCount(int board[6][6], int row_mask[6], int col_mask[6],
                                         int box_mask[6], int* count) const {
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

bool MiniSudokuGenerator::hasUniqueSolution(const int board[6][6]) const {
  int work[6][6];
  std::memcpy(work, board, kSize * kSize * sizeof(int));

  int row_mask[6] = {};
  int col_mask[6] = {};
  int box_mask[6] = {};
  if (!buildMasksFromBoard(work, row_mask, col_mask, box_mask)) {
    return false;
  }

  int count = 0;
  solveWithCount(work, row_mask, col_mask, box_mask, &count);
  return count == 1;
}

bool MiniSudokuGenerator::Generate(MiniPuzzle* out, MiniGenerateStats* stats, int holes) {
  if (out == nullptr) {
    return false;
  }
  if (holes < 1 || holes >= kCellCount) {
    return false;
  }

  const int max_attempts = 5000;
  int solution[6][6];
  int puzzle[6][6];

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
      out->holes = holes;
      out->created_at_unix = nowUnix();
      return true;
    }
  }

  if (stats != nullptr) {
    stats->attempts_used = max_attempts;
  }
  if (verbose_) {
    std::cerr << "[mini6] batch-exhausted holes=" << holes << " attempts=" << max_attempts
              << '\n';
  }
  return false;
}

}  // namespace sudoku
