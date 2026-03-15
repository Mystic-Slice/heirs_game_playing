// engine.cpp — Move validator for the Heirs game.
// Reads input.txt (board + color) and output.txt (move),
// validates the move, applies it, writes next.txt (new board).
// If invalid, next.txt contains an error message instead of a board.

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

constexpr int BOARD_SIZE = 12;
constexpr char EMPTY = '.';
constexpr std::array<char, BOARD_SIZE> kCols = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'j', 'k', 'm', 'n'};

struct Move {
  int sr = 0, sc = 0, dr = 0, dc = 0;
  char captured = EMPTY;
};

// ── helpers ──────────────────────────────────────────────────────────────────

bool IsWhitePiece(char p) { return std::isupper(static_cast<unsigned char>(p)) != 0; }
bool IsBlackPiece(char p) { return std::islower(static_cast<unsigned char>(p)) != 0; }

bool IsFriendly(char p, bool white) {
  if (p == EMPTY) return false;
  return white ? IsWhitePiece(p) : IsBlackPiece(p);
}
bool IsEnemy(char p, bool white) {
  if (p == EMPTY) return false;
  return white ? IsBlackPiece(p) : IsWhitePiece(p);
}
bool InBounds(int r, int c) {
  return r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE;
}

int ColToIndex(char c) {
  for (int i = 0; i < BOARD_SIZE; ++i)
    if (kCols[i] == c) return i;
  return -1;
}

// ── I/O ──────────────────────────────────────────────────────────────────────

struct GameState {
  bool side_white = true;
  double my_time = 0, opp_time = 0;
  char board[BOARD_SIZE][BOARD_SIZE];
};

bool ReadInput(const std::string& path, GameState* s) {
  std::ifstream in(path);
  if (!in) return false;
  std::string color;
  if (!(in >> color)) return false;
  s->side_white = (color == "WHITE");
  if (!(in >> s->my_time >> s->opp_time)) return false;
  std::string line;
  for (int r = 0; r < BOARD_SIZE; ++r) {
    if (!(in >> line) || (int)line.size() != BOARD_SIZE) return false;
    for (int c = 0; c < BOARD_SIZE; ++c) s->board[r][c] = line[c];
  }
  return true;
}

bool ReadMove(const std::string& path, std::string* move_str) {
  std::ifstream in(path);
  if (!in) return false;
  std::getline(in, *move_str);
  // trim trailing whitespace
  while (!move_str->empty() &&
         (move_str->back() == ' ' || move_str->back() == '\r' ||
          move_str->back() == '\n'))
    move_str->pop_back();
  return !move_str->empty();
}

void WriteNext(const std::string& path, const char board[BOARD_SIZE][BOARD_SIZE]) {
  std::ofstream out(path);
  for (int r = 0; r < BOARD_SIZE; ++r) {
    for (int c = 0; c < BOARD_SIZE; ++c) out << board[r][c];
    out << '\n';
  }
}

void WriteError(const std::string& path, const std::string& msg) {
  std::ofstream out(path);
  out << msg << '\n';
}

// ── Parse move string ────────────────────────────────────────────────────────

bool ParseSquare(const std::string& s, int* row, int* col) {
  if (s.size() < 2 || s.size() > 3) return false;
  int c = ColToIndex(s[0]);
  if (c < 0) return false;
  int rank = 0;
  for (size_t i = 1; i < s.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    rank = rank * 10 + (s[i] - '0');
  }
  if (rank < 1 || rank > BOARD_SIZE) return false;
  *row = BOARD_SIZE - rank;
  *col = c;
  return true;
}

bool ParseMoveStr(const std::string& move_str, int* sr, int* sc, int* dr, int* dc) {
  auto sp = move_str.find(' ');
  if (sp == std::string::npos) return false;
  std::string src = move_str.substr(0, sp);
  std::string dst = move_str.substr(sp + 1);
  while (!dst.empty() && dst[0] == ' ') dst.erase(dst.begin());
  return ParseSquare(src, sr, sc) && ParseSquare(dst, dr, dc);
}

// ── Move generation (mirrors homework.cpp) ───────────────────────────────────

static const int kDiagDirs[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
static const int kOrthDirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
static const int kAllDirs[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
                                    {0, 1},   {1, -1}, {1, 0},  {1, 1}};

void TryAdd(int sr, int sc, int dr, int dc, bool side_white,
            const char board[BOARD_SIZE][BOARD_SIZE], std::vector<Move>* moves) {
  if (!InBounds(dr, dc)) return;
  char t = board[dr][dc];
  if (IsFriendly(t, side_white)) return;
  moves->push_back({sr, sc, dr, dc, t});
}

void AddSliding(int sr, int sc, bool side_white, int max_steps,
                const int dirs[][2], int dir_count,
                const char board[BOARD_SIZE][BOARD_SIZE],
                std::vector<Move>* moves) {
  for (int i = 0; i < dir_count; ++i) {
    for (int step = 1; step <= max_steps; ++step) {
      int nr = sr + dirs[i][0] * step, nc = sc + dirs[i][1] * step;
      if (!InBounds(nr, nc)) break;
      char t = board[nr][nc];
      if (IsFriendly(t, side_white)) break;
      moves->push_back({sr, sc, nr, nc, t});
      if (t != EMPTY) break;
    }
  }
}

bool HasAdjacentFriendly(int sr, int sc, int dr, int dc, bool side_white,
                         const char board[BOARD_SIZE][BOARD_SIZE]) {
  for (int rr = -1; rr <= 1; ++rr)
    for (int cc = -1; cc <= 1; ++cc) {
      if (rr == 0 && cc == 0) continue;
      int nr = dr + rr, nc = dc + cc;
      if (!InBounds(nr, nc)) continue;
      if (nr == sr && nc == sc) continue;
      if (IsFriendly(board[nr][nc], side_white)) return true;
    }
  return false;
}

void GenBaby(int r, int c, bool w, const char b[BOARD_SIZE][BOARD_SIZE],
             std::vector<Move>* m) {
  int dir = w ? -1 : 1;
  for (int step = 1; step <= 2; ++step) {
    int nr = r + dir * step;
    if (!InBounds(nr, c)) break;
    if (step == 2 && b[r + dir][c] != EMPTY) break;
    char t = b[nr][c];
    if (IsFriendly(t, w)) break;
    m->push_back({r, c, nr, c, t});
    if (t != EMPTY) break;
  }
}

void GenScout(int r, int c, bool w, const char b[BOARD_SIZE][BOARD_SIZE],
              std::vector<Move>* m) {
  int dir = w ? -1 : 1;
  for (int fwd = 1; fwd <= 3; ++fwd) {
    int nr = r + dir * fwd;
    if (!InBounds(nr, c)) break;
    for (int side = -1; side <= 1; ++side) {
      int nc = c + side;
      if (!InBounds(nr, nc)) continue;
      char t = b[nr][nc];
      if (IsFriendly(t, w)) continue;
      m->push_back({r, c, nr, nc, t});
    }
  }
}

void GenSibling(int r, int c, bool w, const char b[BOARD_SIZE][BOARD_SIZE],
                std::vector<Move>* m) {
  for (auto& d : kAllDirs) {
    int nr = r + d[0], nc = c + d[1];
    if (!InBounds(nr, nc)) continue;
    char t = b[nr][nc];
    if (IsFriendly(t, w)) continue;
    if (HasAdjacentFriendly(r, c, nr, nc, w, b))
      m->push_back({r, c, nr, nc, t});
  }
}

std::vector<Move> GenerateAllMoves(const char board[BOARD_SIZE][BOARD_SIZE],
                                   bool side_white) {
  std::vector<Move> moves;
  moves.reserve(192);
  for (int r = 0; r < BOARD_SIZE; ++r)
    for (int c = 0; c < BOARD_SIZE; ++c) {
      char p = board[r][c];
      if (p == EMPTY || !IsFriendly(p, side_white)) continue;
      char pu = (char)std::toupper((unsigned char)p);
      switch (pu) {
        case 'B': GenBaby(r, c, side_white, board, &moves); break;
        case 'P':
          for (auto& d : kAllDirs) TryAdd(r, c, r + d[0], c + d[1], side_white, board, &moves);
          break;
        case 'X': AddSliding(r, c, side_white, 3, kAllDirs, 8, board, &moves); break;
        case 'Y':
          for (auto& d : kDiagDirs) TryAdd(r, c, r + d[0], c + d[1], side_white, board, &moves);
          break;
        case 'G': AddSliding(r, c, side_white, 2, kOrthDirs, 4, board, &moves); break;
        case 'T': AddSliding(r, c, side_white, 2, kDiagDirs, 4, board, &moves); break;
        case 'S': GenScout(r, c, side_white, board, &moves); break;
        case 'N': GenSibling(r, c, side_white, board, &moves); break;
      }
    }
  return moves;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
  GameState state{};
  if (!ReadInput("input.txt", &state)) {
    WriteError("next.txt", "ERROR: cannot read input.txt");
    return 0;
  }

  std::string move_str;
  if (!ReadMove("output.txt", &move_str)) {
    WriteError("next.txt", "ERROR: cannot read output.txt");
    return 0;
  }

  int sr, sc, dr, dc;
  if (!ParseMoveStr(move_str, &sr, &sc, &dr, &dc)) {
    WriteError("next.txt", "ERROR: bad move format '" + move_str + "'");
    return 0;
  }

  // Verify source piece belongs to the moving side
  char piece = state.board[sr][sc];
  if (piece == EMPTY || !IsFriendly(piece, state.side_white)) {
    WriteError("next.txt",
               "ERROR: no friendly piece at source in '" + move_str + "'");
    return 0;
  }

  // Generate legal moves and check if the parsed move is among them
  auto legal = GenerateAllMoves(state.board, state.side_white);
  bool found = false;
  for (auto& m : legal) {
    if (m.sr == sr && m.sc == sc && m.dr == dr && m.dc == dc) {
      found = true;
      break;
    }
  }

  if (!found) {
    WriteError("next.txt", "ERROR: illegal move '" + move_str + "'");
    return 0;
  }

  // Apply move
  state.board[dr][dc] = state.board[sr][sc];
  state.board[sr][sc] = EMPTY;

  // Write new board
  WriteNext("next.txt", state.board);
  return 0;
}
