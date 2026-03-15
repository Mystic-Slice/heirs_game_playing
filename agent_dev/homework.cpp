#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int BOARD_SIZE = 12;
constexpr char EMPTY = '.';
constexpr int WIN_SCORE = 100000000;
constexpr int INF_SCORE = 1000000000;
constexpr std::array<char, BOARD_SIZE> kCols = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'j', 'k', 'm', 'n'};

struct Move {
  int sr = 0;
  int sc = 0;
  int dr = 0;
  int dc = 0;
  char captured = EMPTY;
};

struct InputState {
  bool my_is_white = true;
  double my_time = 0.0;
  double opp_time = 0.0;
  char board[BOARD_SIZE][BOARD_SIZE];
};

bool IsWhitePiece(char piece) {
  return std::isupper(static_cast<unsigned char>(piece)) != 0;
}

bool IsBlackPiece(char piece) {
  return std::islower(static_cast<unsigned char>(piece)) != 0;
}

bool IsFriendly(char piece, bool side_white) {
  if (piece == EMPTY) {
    return false;
  }
  return side_white ? IsWhitePiece(piece) : IsBlackPiece(piece);
}

bool IsEnemy(char piece, bool side_white) {
  if (piece == EMPTY) {
    return false;
  }
  return side_white ? IsBlackPiece(piece) : IsWhitePiece(piece);
}

bool InBounds(int r, int c) {
  return r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE;
}

bool SameMove(const Move& a, const Move& b) {
  return a.sr == b.sr && a.sc == b.sc && a.dr == b.dr && a.dc == b.dc;
}

int ColToIndex(char c) {
  for (int i = 0; i < BOARD_SIZE; ++i) {
    if (kCols[i] == c) {
      return i;
    }
  }
  return -1;
}

char IndexToCol(int idx) {
  if (idx < 0 || idx >= BOARD_SIZE) {
    return 'a';
  }
  return kCols[idx];
}

int BoardRowToRank(int board_row) {
  return BOARD_SIZE - board_row;
}

std::string MoveToString(const Move& move) {
  const char src_col = IndexToCol(move.sc);
  const char dst_col = IndexToCol(move.dc);
  const int src_rank = BoardRowToRank(move.sr);
  const int dst_rank = BoardRowToRank(move.dr);
  return std::string(1, src_col) + std::to_string(src_rank) + " " + std::string(1, dst_col) +
         std::to_string(dst_rank);
}

bool ReadInput(const std::string& path, InputState* state) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }

  std::string color;
  if (!(in >> color)) {
    return false;
  }
  state->my_is_white = (color == "WHITE");

  if (!(in >> state->my_time >> state->opp_time)) {
    return false;
  }

  std::string line;
  for (int r = 0; r < BOARD_SIZE; ++r) {
    if (!(in >> line) || static_cast<int>(line.size()) != BOARD_SIZE) {
      return false;
    }
    for (int c = 0; c < BOARD_SIZE; ++c) {
      state->board[r][c] = line[c];
    }
  }
  return true;
}

void WriteOutput(const std::string& path, const std::string& move_string) {
  std::ofstream out(path);
  if (!out) {
    return;
  }
  out << move_string << "\n";
}

int PieceValue(char piece_upper) {
  switch (piece_upper) {
    case 'P':
      return 100000;
    case 'X':
      return 900;
    case 'S':
      return 500;
    case 'G':
      return 450;
    case 'T':
      return 400;
    case 'N':
      return 350;
    case 'Y':
      return 300;
    case 'B':
      return 100;
    default:
      return 0;
  }
}

class TimeUpException {};

class Agent {
 public:
  explicit Agent(const InputState& state)
      : my_is_white_(state.my_is_white), my_time_(state.my_time), opp_time_(state.opp_time) {
    for (int r = 0; r < BOARD_SIZE; ++r) {
      for (int c = 0; c < BOARD_SIZE; ++c) {
        board_[r][c] = state.board[r][c];
      }
    }
  }

  bool SelectMove(Move* best_move) {
    std::vector<Move> legal = GenerateAllMoves(my_is_white_);
    if (legal.empty()) {
      return false;
    }

    OrderMoves(&legal, my_is_white_, false);
    *best_move = legal.front();

    if (my_time_ < 0.5) {
      return true;
    }

    double time_for_move = std::min(my_time_ / 40.0, 0.1);
    if (time_for_move < 0.05) {
      time_for_move = 0.05;
    }

    deadline_ = std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(time_for_move));

    has_root_hint_ = false;
    for (int depth = 1; depth <= 64; ++depth) {
      if (std::chrono::steady_clock::now() >= deadline_) {
        break;
      }
      node_count_ = 0;
      try {
        Move depth_best{};
        int depth_score = -INF_SCORE;
        if (!SearchAtDepth(depth, &depth_best, &depth_score)) {
          break;
        }
        *best_move = depth_best;
        root_hint_ = depth_best;
        has_root_hint_ = true;
        if (depth_score >= WIN_SCORE - 1000) {
          break;
        }
      } catch (const TimeUpException&) {
        break;
      }
    }

    return true;
  }

 private:
  bool my_is_white_ = true;
  double my_time_ = 0.0;
  double opp_time_ = 0.0;
  char board_[BOARD_SIZE][BOARD_SIZE];
  std::chrono::steady_clock::time_point deadline_{};
  std::uint64_t node_count_ = 0;
  bool has_root_hint_ = false;
  Move root_hint_{};

  void CheckTime() {
    ++node_count_;
    if ((node_count_ & 1023ULL) == 0ULL && std::chrono::steady_clock::now() >= deadline_) {
      throw TimeUpException();
    }
  }

  bool HasPrince(bool white) const {
    const char prince = white ? 'P' : 'p';
    for (int r = 0; r < BOARD_SIZE; ++r) {
      for (int c = 0; c < BOARD_SIZE; ++c) {
        if (board_[r][c] == prince) {
          return true;
        }
      }
    }
    return false;
  }

  int EvaluateColor(bool white) const {
    int score = 0;
    int prince_r = -1;
    int prince_c = -1;

    for (int r = 0; r < BOARD_SIZE; ++r) {
      for (int c = 0; c < BOARD_SIZE; ++c) {
        const char piece = board_[r][c];
        if (piece == EMPTY) {
          continue;
        }
        if (white != IsWhitePiece(piece)) {
          continue;
        }

        const char piece_upper = static_cast<char>(std::toupper(static_cast<unsigned char>(piece)));
        score += PieceValue(piece_upper);

        if (piece_upper == 'B') {
          int advance = white ? (10 - r) : (r - 1);
          if (advance > 0) {
            score += advance * 8;
          }
        }

        if (piece_upper != 'B' && piece_upper != 'P') {
          const int center_dist = std::abs(r - 5) + std::abs(c - 5);
          const int center_bonus = std::max(0, 6 - center_dist);
          score += center_bonus * 4;
        }

        if (piece_upper == 'P') {
          prince_r = r;
          prince_c = c;
        }
      }
    }

    if (prince_r != -1) {
      int friendly_neighbors = 0;
      for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
          if (dr == 0 && dc == 0) {
            continue;
          }
          const int nr = prince_r + dr;
          const int nc = prince_c + dc;
          if (!InBounds(nr, nc)) {
            continue;
          }
          if (IsFriendly(board_[nr][nc], white)) {
            ++friendly_neighbors;
          }
        }
      }
      score += friendly_neighbors * 18;
    }

    return score;
  }

  int Evaluate(bool side_white) const {
    const bool white_prince = HasPrince(true);
    const bool black_prince = HasPrince(false);

    if (!white_prince && !black_prince) {
      return 0;
    }
    if (!white_prince) {
      return side_white ? -WIN_SCORE : WIN_SCORE;
    }
    if (!black_prince) {
      return side_white ? WIN_SCORE : -WIN_SCORE;
    }

    const int white_score = EvaluateColor(true);
    const int black_score = EvaluateColor(false);
    return side_white ? (white_score - black_score) : (black_score - white_score);
  }

  void MakeMove(const Move& move) {
    const char moving_piece = board_[move.sr][move.sc];
    board_[move.sr][move.sc] = EMPTY;
    board_[move.dr][move.dc] = moving_piece;
  }

  void UnmakeMove(const Move& move) {
    const char moving_piece = board_[move.dr][move.dc];
    board_[move.dr][move.dc] = move.captured;
    board_[move.sr][move.sc] = moving_piece;
  }

  void TryAddMove(int sr, int sc, int dr, int dc, bool side_white, std::vector<Move>* moves) const {
    if (!InBounds(dr, dc)) {
      return;
    }
    const char target = board_[dr][dc];
    if (IsFriendly(target, side_white)) {
      return;
    }
    moves->push_back(Move{sr, sc, dr, dc, target});
  }

  void AddSlidingMoves(int sr, int sc, bool side_white, int max_steps, const int dirs[][2], int dir_count,
                       std::vector<Move>* moves) const {
    for (int i = 0; i < dir_count; ++i) {
      const int dr = dirs[i][0];
      const int dc = dirs[i][1];
      for (int step = 1; step <= max_steps; ++step) {
        const int nr = sr + dr * step;
        const int nc = sc + dc * step;
        if (!InBounds(nr, nc)) {
          break;
        }
        const char target = board_[nr][nc];
        if (IsFriendly(target, side_white)) {
          break;
        }
        moves->push_back(Move{sr, sc, nr, nc, target});
        if (target != EMPTY) {
          break;
        }
      }
    }
  }

  bool HasAdjacentFriendlyAfterSiblingMove(int sr, int sc, int dr, int dc, bool side_white) const {
    for (int rr = -1; rr <= 1; ++rr) {
      for (int cc = -1; cc <= 1; ++cc) {
        if (rr == 0 && cc == 0) {
          continue;
        }
        const int nr = dr + rr;
        const int nc = dc + cc;
        if (!InBounds(nr, nc)) {
          continue;
        }
        if (nr == sr && nc == sc) {
          continue;
        }
        if (IsFriendly(board_[nr][nc], side_white)) {
          return true;
        }
      }
    }
    return false;
  }

  void GenerateBabyMoves(int r, int c, bool side_white, std::vector<Move>* moves) const {
    const int dir = side_white ? -1 : 1;
    for (int step = 1; step <= 2; ++step) {
      const int nr = r + dir * step;
      if (!InBounds(nr, c)) {
        break;
      }
      if (step == 2 && board_[r + dir][c] != EMPTY) {
        break;
      }
      const char target = board_[nr][c];
      if (IsFriendly(target, side_white)) {
        break;
      }
      moves->push_back(Move{r, c, nr, c, target});
      if (target != EMPTY) {
        break;
      }
    }
  }

  void GenerateScoutMoves(int r, int c, bool side_white, std::vector<Move>* moves) const {
    const int dir = side_white ? -1 : 1;
    for (int forward = 1; forward <= 3; ++forward) {
      const int nr = r + dir * forward;
      if (!InBounds(nr, c)) {
        break;
      }
      for (int side = -1; side <= 1; ++side) {
        const int nc = c + side;
        if (!InBounds(nr, nc)) {
          continue;
        }
        const char target = board_[nr][nc];
        if (IsFriendly(target, side_white)) {
          continue;
        }
        moves->push_back(Move{r, c, nr, nc, target});
      }
    }
  }

  void GenerateSiblingMoves(int r, int c, bool side_white, std::vector<Move>* moves) const {
    static const int kAllDirs[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
                                       {0, 1},   {1, -1}, {1, 0},  {1, 1}};
    for (const auto& d : kAllDirs) {
      const int nr = r + d[0];
      const int nc = c + d[1];
      if (!InBounds(nr, nc)) {
        continue;
      }
      const char target = board_[nr][nc];
      if (IsFriendly(target, side_white)) {
        continue;
      }
      if (HasAdjacentFriendlyAfterSiblingMove(r, c, nr, nc, side_white)) {
        moves->push_back(Move{r, c, nr, nc, target});
      }
    }
  }

  void GeneratePieceMoves(int r, int c, bool side_white, std::vector<Move>* moves) const {
    const char piece = board_[r][c];
    const char piece_upper = static_cast<char>(std::toupper(static_cast<unsigned char>(piece)));

    static const int kDiagDirs[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    static const int kOrthDirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    static const int kAllDirs[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
                                       {0, 1},   {1, -1}, {1, 0},  {1, 1}};

    switch (piece_upper) {
      case 'B':
        GenerateBabyMoves(r, c, side_white, moves);
        break;
      case 'P':
        for (const auto& d : kAllDirs) {
          TryAddMove(r, c, r + d[0], c + d[1], side_white, moves);
        }
        break;
      case 'X':
        AddSlidingMoves(r, c, side_white, 3, kAllDirs, 8, moves);
        break;
      case 'Y':
        for (const auto& d : kDiagDirs) {
          TryAddMove(r, c, r + d[0], c + d[1], side_white, moves);
        }
        break;
      case 'G':
        AddSlidingMoves(r, c, side_white, 2, kOrthDirs, 4, moves);
        break;
      case 'T':
        AddSlidingMoves(r, c, side_white, 2, kDiagDirs, 4, moves);
        break;
      case 'S':
        GenerateScoutMoves(r, c, side_white, moves);
        break;
      case 'N':
        GenerateSiblingMoves(r, c, side_white, moves);
        break;
      default:
        break;
    }
  }

  std::vector<Move> GenerateAllMoves(bool side_white) const {
    std::vector<Move> moves;
    moves.reserve(192);

    for (int r = 0; r < BOARD_SIZE; ++r) {
      for (int c = 0; c < BOARD_SIZE; ++c) {
        const char piece = board_[r][c];
        if (piece == EMPTY) {
          continue;
        }
        if (!IsFriendly(piece, side_white)) {
          continue;
        }
        GeneratePieceMoves(r, c, side_white, &moves);
      }
    }

    return moves;
  }

  int MoveOrderScore(const Move& move, bool side_white, bool allow_hint) const {
    int score = 0;
    if (allow_hint && has_root_hint_ && SameMove(move, root_hint_)) {
      score += 1000000;
    }

    if (move.captured != EMPTY) {
      const int victim = PieceValue(static_cast<char>(std::toupper(static_cast<unsigned char>(move.captured))));
      const char attacker_piece = board_[move.sr][move.sc];
      const int attacker = PieceValue(static_cast<char>(std::toupper(static_cast<unsigned char>(attacker_piece))));
      score += 200000 + victim * 16 - attacker;
    }

    const char attacker_piece = board_[move.sr][move.sc];
    const char attacker_upper = static_cast<char>(std::toupper(static_cast<unsigned char>(attacker_piece)));
    if (attacker_upper == 'B') {
      const int forward = side_white ? (move.sr - move.dr) : (move.dr - move.sr);
      score += forward * 6;
    } else {
      const int center_dist = std::abs(move.dr - 5) + std::abs(move.dc - 5);
      score += std::max(0, 6 - center_dist);
    }

    return score;
  }

  void OrderMoves(std::vector<Move>* moves, bool side_white, bool allow_hint) const {
    std::sort(moves->begin(), moves->end(),
              [&](const Move& a, const Move& b) {
                return MoveOrderScore(a, side_white, allow_hint) > MoveOrderScore(b, side_white, allow_hint);
              });
  }

  int Negamax(int depth, int alpha, int beta, bool side_white) {
    CheckTime();

    const bool white_prince = HasPrince(true);
    const bool black_prince = HasPrince(false);
    if (!white_prince || !black_prince || depth == 0) {
      return Evaluate(side_white);
    }

    std::vector<Move> moves = GenerateAllMoves(side_white);
    if (moves.empty()) {
      return Evaluate(side_white);
    }
    OrderMoves(&moves, side_white, false);

    int best = -INF_SCORE;
    for (const Move& move : moves) {
      MakeMove(move);
      const int score = -Negamax(depth - 1, -beta, -alpha, !side_white);
      UnmakeMove(move);

      if (score > best) {
        best = score;
      }
      if (score > alpha) {
        alpha = score;
      }
      if (alpha >= beta) {
        break;
      }
    }

    return best;
  }

  bool SearchAtDepth(int depth, Move* best_move, int* best_score) {
    std::vector<Move> moves = GenerateAllMoves(my_is_white_);
    if (moves.empty()) {
      return false;
    }
    OrderMoves(&moves, my_is_white_, true);

    int alpha = -INF_SCORE;
    const int beta = INF_SCORE;
    int local_best_score = -INF_SCORE;
    Move local_best = moves.front();

    for (const Move& move : moves) {
      CheckTime();
      MakeMove(move);
      const int score = -Negamax(depth - 1, -beta, -alpha, !my_is_white_);
      UnmakeMove(move);

      if (score > local_best_score) {
        local_best_score = score;
        local_best = move;
      }
      if (score > alpha) {
        alpha = score;
      }
    }

    *best_move = local_best;
    *best_score = local_best_score;
    return true;
  }
};

}  // namespace

int main() {
  InputState state{};
  if (!ReadInput("input.txt", &state)) {
    WriteOutput("output.txt", "a1 a1");
    return 0;
  }

  Agent agent(state);
  Move best_move{};
  if (!agent.SelectMove(&best_move)) {
    WriteOutput("output.txt", "");
    return 0;
  }

  WriteOutput("output.txt", MoveToString(best_move));
  return 0;
}
