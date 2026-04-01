// Random agent — picks a legal move uniformly at random.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>

namespace {

// ── Constants ────────────────────────────────────────────────────────────────
constexpr int BOARD_SIZE = 12;
constexpr char EMPTY = '.';
constexpr int MAX_MOVES = 256;
constexpr std::array<char, BOARD_SIZE> kCols =
    {'a','b','c','d','e','f','g','h','j','k','m','n'};

// ── Direction tables ─────────────────────────────────────────────────────────
static constexpr int kDiagDirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
static constexpr int kOrthDirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
static constexpr int kAllDirs[8][2]  = {{-1,-1},{-1,0},{-1,1},{0,-1},
                                         {0,1},{1,-1},{1,0},{1,1}};

// ── Data types ───────────────────────────────────────────────────────────────
struct Move {
  int sr = 0, sc = 0, dr = 0, dc = 0;
  char captured = EMPTY;
};

struct MoveList {
  Move moves[MAX_MOVES];
  int count = 0;
  void push(int sr, int sc, int dr, int dc, char cap) {
    moves[count++] = {sr, sc, dr, dc, cap};
  }
  void clear() { count = 0; }
};

struct InputState {
  bool my_is_white = true;
  double my_time = 0.0, opp_time = 0.0;
  char board[BOARD_SIZE][BOARD_SIZE];
};

// ── Helpers ──────────────────────────────────────────────────────────────────
bool IsWhite(char p)  { return std::isupper(static_cast<unsigned char>(p)) != 0; }
bool IsFriendly(char p, bool w) {
  if (p == EMPTY) return false;
  return w ? IsWhite(p) : !IsWhite(p);
}
bool InBounds(int r, int c) { return (unsigned)r < BOARD_SIZE && (unsigned)c < BOARD_SIZE; }

char IndexToCol(int i) { return (i >= 0 && i < BOARD_SIZE) ? kCols[i] : 'a'; }

std::string MoveToString(const Move& m) {
  return std::string(1, IndexToCol(m.sc)) + std::to_string(BOARD_SIZE - m.sr) +
         " " +
         std::string(1, IndexToCol(m.dc)) + std::to_string(BOARD_SIZE - m.dr);
}

bool ReadInput(const std::string& path, InputState* st) {
  std::ifstream in(path);
  if (!in) return false;
  std::string color;
  if (!(in >> color)) return false;
  st->my_is_white = (color == "WHITE");
  if (!(in >> st->my_time >> st->opp_time)) return false;
  std::string line;
  for (int r = 0; r < BOARD_SIZE; ++r) {
    if (!(in >> line) || (int)line.size() != BOARD_SIZE) return false;
    for (int c = 0; c < BOARD_SIZE; ++c) st->board[r][c] = line[c];
  }
  return true;
}

void WriteOutput(const std::string& path, const std::string& s) {
  std::ofstream out(path);
  if (out) out << s << "\n";
}

// ── Move generation ──────────────────────────────────────────────────────────
void TryAdd(const char board[][BOARD_SIZE], int sr, int sc, int dr, int dc,
            bool w, MoveList& ml) {
  if (!InBounds(dr, dc)) return;
  char t = board[dr][dc];
  if (IsFriendly(t, w)) return;
  ml.push(sr, sc, dr, dc, t);
}

void AddSliding(const char board[][BOARD_SIZE], int sr, int sc, bool w, int ms,
                const int d[][2], int nd, MoveList& ml) {
  for (int i = 0; i < nd; ++i)
    for (int s = 1; s <= ms; ++s) {
      int nr = sr + d[i][0]*s, nc = sc + d[i][1]*s;
      if (!InBounds(nr, nc)) break;
      char t = board[nr][nc];
      if (IsFriendly(t, w)) break;
      ml.push(sr, sc, nr, nc, t);
      if (t != EMPTY) break;
    }
}

bool HasAdjFriendly(const char board[][BOARD_SIZE], int sr, int sc,
                    int dr, int dc, bool w) {
  for (int rr = -1; rr <= 1; ++rr)
    for (int cc = -1; cc <= 1; ++cc) {
      if (!rr && !cc) continue;
      int nr = dr+rr, nc = dc+cc;
      if (!InBounds(nr, nc) || (nr == sr && nc == sc)) continue;
      if (IsFriendly(board[nr][nc], w)) return true;
    }
  return false;
}

void GenBaby(const char board[][BOARD_SIZE], int r, int c, bool w, MoveList& ml) {
  int dir = w ? -1 : 1;
  for (int s = 1; s <= 2; ++s) {
    int nr = r + dir*s;
    if (!InBounds(nr, c)) break;
    if (s == 2 && board[r+dir][c] != EMPTY) break;
    char t = board[nr][c];
    if (IsFriendly(t, w)) break;
    ml.push(r, c, nr, c, t);
    if (t != EMPTY) break;
  }
}

void GenScout(const char board[][BOARD_SIZE], int r, int c, bool w, MoveList& ml) {
  int dir = w ? -1 : 1;
  for (int f = 1; f <= 3; ++f) {
    int nr = r + dir*f;
    if (!InBounds(nr, c)) break;
    for (int s = -1; s <= 1; ++s) {
      int nc = c + s;
      if (!InBounds(nr, nc)) continue;
      char t = board[nr][nc];
      if (IsFriendly(t, w)) continue;
      ml.push(r, c, nr, nc, t);
    }
  }
}

void GenSibling(const char board[][BOARD_SIZE], int r, int c, bool w, MoveList& ml) {
  for (const auto& d : kAllDirs) {
    int nr = r+d[0], nc = c+d[1];
    if (!InBounds(nr, nc)) continue;
    char t = board[nr][nc];
    if (IsFriendly(t, w)) continue;
    if (HasAdjFriendly(board, r, c, nr, nc, w))
      ml.push(r, c, nr, nc, t);
  }
}

void GeneratePieceMoves(const char board[][BOARD_SIZE], int r, int c,
                        bool w, MoveList& ml) {
  char pu = (char)std::toupper((unsigned char)board[r][c]);
  switch (pu) {
    case 'B': GenBaby(board, r, c, w, ml); break;
    case 'P': for (const auto& d : kAllDirs)
                TryAdd(board, r, c, r+d[0], c+d[1], w, ml); break;
    case 'X': AddSliding(board, r, c, w, 3, kAllDirs, 8, ml); break;
    case 'Y': for (const auto& d : kDiagDirs)
                TryAdd(board, r, c, r+d[0], c+d[1], w, ml); break;
    case 'G': AddSliding(board, r, c, w, 2, kOrthDirs, 4, ml); break;
    case 'T': AddSliding(board, r, c, w, 2, kDiagDirs, 4, ml); break;
    case 'S': GenScout(board, r, c, w, ml); break;
    case 'N': GenSibling(board, r, c, w, ml); break;
  }
}

void GenerateAllMoves(const char board[][BOARD_SIZE], bool w, MoveList& ml) {
  ml.clear();
  for (int r = 0; r < BOARD_SIZE; ++r)
    for (int c = 0; c < BOARD_SIZE; ++c)
      if (board[r][c] != EMPTY && IsFriendly(board[r][c], w))
        GeneratePieceMoves(board, r, c, w, ml);
}

}  // namespace

int main() {
  std::srand((unsigned)std::time(nullptr));

  InputState state{};
  if (!ReadInput("input.txt", &state)) {
    WriteOutput("output.txt", "a1 a1");
    return 0;
  }

  MoveList legal;
  GenerateAllMoves(state.board, state.my_is_white, legal);

  if (legal.count == 0) {
    WriteOutput("output.txt", "a1 a1");
    return 0;
  }

  int pick = std::rand() % legal.count;
  WriteOutput("output.txt", MoveToString(legal.moves[pick]));
  return 0;
}
