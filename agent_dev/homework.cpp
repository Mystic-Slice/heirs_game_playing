#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace {

// ── Constants ────────────────────────────────────────────────────────────────
constexpr int BOARD_SIZE = 12;
constexpr int NUM_SQUARES = BOARD_SIZE * BOARD_SIZE;
constexpr char EMPTY = '.';
constexpr int WIN_SCORE  = 100000000;
constexpr int INF_SCORE  = 1000000000;
constexpr int MAX_PLY    = 64;
constexpr int MAX_MOVES  = 256;
constexpr std::array<char, BOARD_SIZE> kCols =
    {'a','b','c','d','e','f','g','h','j','k','m','n'};

// ── Direction tables ─────────────────────────────────────────────────────────
static constexpr int kDiagDirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
static constexpr int kOrthDirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
static constexpr int kAllDirs[8][2]  = {{-1,-1},{-1,0},{-1,1},{0,-1},
                                         {0,1},{1,-1},{1,0},{1,1}};

// ── Zobrist ──────────────────────────────────────────────────────────────────
constexpr int NUM_PIECE_TYPES = 16;
constexpr int PIECE_NONE = -1;

std::uint64_t zobrist_piece[NUM_PIECE_TYPES][BOARD_SIZE][BOARD_SIZE];
std::uint64_t zobrist_side;

int PieceIndex(char p) {
  switch (p) {
    case 'B': return 0;  case 'P': return 1;  case 'X': return 2;  case 'Y': return 3;
    case 'G': return 4;  case 'T': return 5;  case 'S': return 6;  case 'N': return 7;
    case 'b': return 8;  case 'p': return 9;  case 'x': return 10; case 'y': return 11;
    case 'g': return 12; case 't': return 13; case 's': return 14; case 'n': return 15;
    default:  return PIECE_NONE;
  }
}

void InitZobrist() {
  std::uint64_t s = 0x12345678ABCDEF01ULL;
  auto next = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
  for (int p = 0; p < NUM_PIECE_TYPES; ++p)
    for (int r = 0; r < BOARD_SIZE; ++r)
      for (int c = 0; c < BOARD_SIZE; ++c)
        zobrist_piece[p][r][c] = next();
  zobrist_side = next();
}

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

// ── Transposition table ──────────────────────────────────────────────────────
constexpr int TT_SIZE = 1 << 20;
constexpr int TT_MASK = TT_SIZE - 1;
enum TTFlag : std::uint8_t { TT_EXACT, TT_ALPHA, TT_BETA };

struct TTEntry {
  std::uint64_t key = 0;
  std::int32_t  score = 0;
  std::int16_t  depth = -1;
  TTFlag        flag  = TT_EXACT;
  std::uint8_t  from_sq = 0;   // sr*12+sc
  std::uint8_t  to_sq   = 0;   // dr*12+dc
};

// ── Helpers ──────────────────────────────────────────────────────────────────
bool IsWhite(char p)  { return std::isupper(static_cast<unsigned char>(p)) != 0; }
bool IsFriendly(char p, bool w) {
  if (p == EMPTY) return false;
  return w ? IsWhite(p) : !IsWhite(p);
}
bool InBounds(int r, int c) { return (unsigned)r < BOARD_SIZE && (unsigned)c < BOARD_SIZE; }

int ColToIndex(char c) {
  for (int i = 0; i < BOARD_SIZE; ++i) if (kCols[i] == c) return i;
  return -1;
}
char IndexToCol(int i) { return (i >= 0 && i < BOARD_SIZE) ? kCols[i] : 'a'; }

std::string MoveToString(const Move& m) {
  return std::string(1, IndexToCol(m.sc)) + std::to_string(BOARD_SIZE - m.sr) +
         " " +
         std::string(1, IndexToCol(m.dc)) + std::to_string(BOARD_SIZE - m.dr);
}

int PieceValue(char pu) {
  switch (pu) {
    case 'P': return 100000; case 'X': return 900; case 'S': return 500;
    case 'G': return 450;    case 'T': return 400; case 'N': return 350;
    case 'Y': return 300;    case 'B': return 100; default:  return 0;
  }
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

// ── TimeUp ───────────────────────────────────────────────────────────────────
class TimeUpException {};

// ── Agent ────────────────────────────────────────────────────────────────────
class Agent {
 public:
  explicit Agent(const InputState& st)
      : my_is_white_(st.my_is_white), my_time_(st.my_time) {
    hash_ = 0;
    white_prince_ = false;
    black_prince_ = false;
    for (int r = 0; r < BOARD_SIZE; ++r)
      for (int c = 0; c < BOARD_SIZE; ++c) {
        board_[r][c] = st.board[r][c];
        int pi = PieceIndex(board_[r][c]);
        if (pi != PIECE_NONE) hash_ ^= zobrist_piece[pi][r][c];
        if (board_[r][c] == 'P') white_prince_ = true;
        if (board_[r][c] == 'p') black_prince_ = true;
      }
    if (!my_is_white_) hash_ ^= zobrist_side;

    tt_.reset(new TTEntry[TT_SIZE]());
    std::memset(killers_, 0, sizeof(killers_));
    std::memset(history_, 0, sizeof(history_));
  }

  bool SelectMove(Move* best_move) {
    MoveList legal;
    GenerateAllMoves(my_is_white_, legal);
    if (legal.count == 0) return false;

    // Fallback: pick the capture-first ordered move
    int scores[MAX_MOVES];
    ComputeScores(legal, my_is_white_, 0, false, nullptr, scores);
    PickBest(legal, scores, 0);
    *best_move = legal.moves[0];

    if (my_time_ < 0.5) return true;

    double time_for_move = std::min(my_time_ / 40.0, 0.05);
    if (time_for_move < 0.03) time_for_move = 0.03;

    deadline_ = std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(time_for_move));

    has_root_hint_ = false;
    for (int depth = 1; depth <= MAX_PLY; ++depth) {
      if (std::chrono::steady_clock::now() >= deadline_) break;
      node_count_ = 0;
      try {
        Move db{}; int ds = -INF_SCORE;
        if (!SearchAtDepth(depth, &db, &ds)) break;
        *best_move = db;
        root_hint_ = db;
        has_root_hint_ = true;
        if (ds >= WIN_SCORE - 1000) break;
      } catch (const TimeUpException&) {
        break;
      }
    }
    return true;
  }

 private:
  // ── State ──────────────────────────────────────────────────────────────────
  bool my_is_white_;
  double my_time_;
  char board_[BOARD_SIZE][BOARD_SIZE];
  std::uint64_t hash_ = 0;
  bool white_prince_ = true, black_prince_ = true;

  std::chrono::steady_clock::time_point deadline_{};
  std::uint64_t node_count_ = 0;
  bool has_root_hint_ = false;
  Move root_hint_{};

  std::unique_ptr<TTEntry[]> tt_;
  Move killers_[MAX_PLY][2];
  int  history_[2][NUM_SQUARES][NUM_SQUARES];

  // ── Inline helpers ─────────────────────────────────────────────────────────
  static int Sq(int r, int c) { return r * BOARD_SIZE + c; }

  static bool SameMove(const Move& a, const Move& b) {
    return a.sr == b.sr && a.sc == b.sc && a.dr == b.dr && a.dc == b.dc;
  }

  void CheckTime() {
    ++node_count_;
    if ((node_count_ & 1023ULL) == 0ULL &&
        std::chrono::steady_clock::now() >= deadline_)
      throw TimeUpException();
  }

  void HashPiece(char p, int r, int c) {
    int pi = PieceIndex(p);
    if (pi != PIECE_NONE) hash_ ^= zobrist_piece[pi][r][c];
  }

  // ── Make / Unmake ──────────────────────────────────────────────────────────
  void MakeMove(const Move& m) {
    char mv = board_[m.sr][m.sc];
    HashPiece(mv, m.sr, m.sc);
    if (m.captured != EMPTY) {
      HashPiece(m.captured, m.dr, m.dc);
      if (m.captured == 'P') white_prince_ = false;
      if (m.captured == 'p') black_prince_ = false;
    }
    HashPiece(mv, m.dr, m.dc);
    board_[m.sr][m.sc] = EMPTY;
    board_[m.dr][m.dc] = mv;
    hash_ ^= zobrist_side;
  }

  void UnmakeMove(const Move& m) {
    char mv = board_[m.dr][m.dc];
    hash_ ^= zobrist_side;
    HashPiece(mv, m.dr, m.dc);
    if (m.captured != EMPTY) {
      HashPiece(m.captured, m.dr, m.dc);
      if (m.captured == 'P') white_prince_ = true;
      if (m.captured == 'p') black_prince_ = true;
    }
    HashPiece(mv, m.sr, m.sc);
    board_[m.dr][m.dc] = m.captured;
    board_[m.sr][m.sc] = mv;
  }

  // ── Move generation ────────────────────────────────────────────────────────
  void TryAdd(int sr, int sc, int dr, int dc, bool w, MoveList& ml) const {
    if (!InBounds(dr, dc)) return;
    char t = board_[dr][dc];
    if (IsFriendly(t, w)) return;
    ml.push(sr, sc, dr, dc, t);
  }

  void AddSliding(int sr, int sc, bool w, int ms,
                  const int d[][2], int nd, MoveList& ml) const {
    for (int i = 0; i < nd; ++i)
      for (int s = 1; s <= ms; ++s) {
        int nr = sr + d[i][0]*s, nc = sc + d[i][1]*s;
        if (!InBounds(nr, nc)) break;
        char t = board_[nr][nc];
        if (IsFriendly(t, w)) break;
        ml.push(sr, sc, nr, nc, t);
        if (t != EMPTY) break;
      }
  }

  bool HasAdjFriendly(int sr, int sc, int dr, int dc, bool w) const {
    for (int rr = -1; rr <= 1; ++rr)
      for (int cc = -1; cc <= 1; ++cc) {
        if (!rr && !cc) continue;
        int nr = dr+rr, nc = dc+cc;
        if (!InBounds(nr, nc) || (nr == sr && nc == sc)) continue;
        if (IsFriendly(board_[nr][nc], w)) return true;
      }
    return false;
  }

  void GenBaby(int r, int c, bool w, MoveList& ml) const {
    int dir = w ? -1 : 1;
    for (int s = 1; s <= 2; ++s) {
      int nr = r + dir*s;
      if (!InBounds(nr, c)) break;
      if (s == 2 && board_[r+dir][c] != EMPTY) break;
      char t = board_[nr][c];
      if (IsFriendly(t, w)) break;
      ml.push(r, c, nr, c, t);
      if (t != EMPTY) break;
    }
  }

  void GenScout(int r, int c, bool w, MoveList& ml) const {
    int dir = w ? -1 : 1;
    for (int f = 1; f <= 3; ++f) {
      int nr = r + dir*f;
      if (!InBounds(nr, c)) break;
      for (int s = -1; s <= 1; ++s) {
        int nc = c + s;
        if (!InBounds(nr, nc)) continue;
        char t = board_[nr][nc];
        if (IsFriendly(t, w)) continue;
        ml.push(r, c, nr, nc, t);
      }
    }
  }

  void GenSibling(int r, int c, bool w, MoveList& ml) const {
    for (const auto& d : kAllDirs) {
      int nr = r+d[0], nc = c+d[1];
      if (!InBounds(nr, nc)) continue;
      char t = board_[nr][nc];
      if (IsFriendly(t, w)) continue;
      if (HasAdjFriendly(r, c, nr, nc, w))
        ml.push(r, c, nr, nc, t);
    }
  }

  void GeneratePieceMoves(int r, int c, bool w, MoveList& ml) const {
    char pu = (char)std::toupper((unsigned char)board_[r][c]);
    switch (pu) {
      case 'B': GenBaby(r, c, w, ml); break;
      case 'P': for (const auto& d : kAllDirs)
                  TryAdd(r, c, r+d[0], c+d[1], w, ml); break;
      case 'X': AddSliding(r, c, w, 3, kAllDirs, 8, ml); break;
      case 'Y': for (const auto& d : kDiagDirs)
                  TryAdd(r, c, r+d[0], c+d[1], w, ml); break;
      case 'G': AddSliding(r, c, w, 2, kOrthDirs, 4, ml); break;
      case 'T': AddSliding(r, c, w, 2, kDiagDirs, 4, ml); break;
      case 'S': GenScout(r, c, w, ml); break;
      case 'N': GenSibling(r, c, w, ml); break;
    }
  }

  void GenerateAllMoves(bool w, MoveList& ml) const {
    ml.clear();
    for (int r = 0; r < BOARD_SIZE; ++r)
      for (int c = 0; c < BOARD_SIZE; ++c)
        if (board_[r][c] != EMPTY && IsFriendly(board_[r][c], w))
          GeneratePieceMoves(r, c, w, ml);
  }

  void GenerateCaptures(bool w, MoveList& ml) const {
    ml.clear();
    for (int r = 0; r < BOARD_SIZE; ++r)
      for (int c = 0; c < BOARD_SIZE; ++c) {
        if (board_[r][c] == EMPTY || !IsFriendly(board_[r][c], w)) continue;
        int old = ml.count;
        GeneratePieceMoves(r, c, w, ml);
        // compact: keep only captures
        int wr = old;
        for (int i = old; i < ml.count; ++i)
          if (ml.moves[i].captured != EMPTY)
            ml.moves[wr++] = ml.moves[i];
        ml.count = wr;
      }
  }

  // ── Evaluation ─────────────────────────────────────────────────────────────
  int Evaluate(bool side_white) const {
    if (!white_prince_ && !black_prince_) return 0;
    if (!white_prince_) return side_white ? -WIN_SCORE : WIN_SCORE;
    if (!black_prince_) return side_white ? WIN_SCORE  : -WIN_SCORE;

    int ws = 0, bs = 0;
    int wpr = -1, wpc = -1, bpr = -1, bpc = -1;

    // First pass: find princes
    for (int r = 0; r < BOARD_SIZE; ++r)
      for (int c = 0; c < BOARD_SIZE; ++c) {
        char p = board_[r][c];
        if (p == 'P') { wpr = r; wpc = c; }
        else if (p == 'p') { bpr = r; bpc = c; }
      }

    // Second pass: material, positional, and attack pressure in one scan
    for (int r = 0; r < BOARD_SIZE; ++r)
      for (int c = 0; c < BOARD_SIZE; ++c) {
        char p = board_[r][c];
        if (p == EMPTY) continue;
        char pu = (char)std::toupper((unsigned char)p);
        int val = PieceValue(pu);
        int pos = 0;
        if (pu == 'B') {
          int adv = IsWhite(p) ? (10 - r) : (r - 1);
          if (adv > 0) pos = adv * 12;
        } else if (pu == 'P') {
          // Prince: no extra positional bonus
        } else {
          // Centrality
          int cd = std::abs(r - 5) + std::abs(c - 5);
          pos = std::max(0, 6 - cd) * 5;
          // Attack pressure: bonus for non-baby pieces near enemy prince
          if (IsWhite(p) && bpr >= 0) {
            int dist = std::abs(r - bpr) + std::abs(c - bpc);
            if (dist <= 3) pos += (4 - dist) * 8;
          } else if (!IsWhite(p) && wpr >= 0) {
            int dist = std::abs(r - wpr) + std::abs(c - wpc);
            if (dist <= 3) pos += (4 - dist) * 8;
          }
        }
        if (IsWhite(p)) ws += val + pos;
        else            bs += val + pos;
      }

    // Prince safety: friendly neighbors vs threats
    auto safety = [&](int pr, int pc, bool w) -> int {
      if (pr < 0) return 0;
      int friends = 0, threats = 0;
      for (const auto& d : kAllDirs) {
        int nr = pr+d[0], nc = pc+d[1];
        if (!InBounds(nr, nc)) continue;
        char adj = board_[nr][nc];
        if (adj == EMPTY) continue;
        if (IsFriendly(adj, w)) ++friends;
        else ++threats;
      }
      return friends * 20 - threats * 25;
    };
    ws += safety(wpr, wpc, true);
    bs += safety(bpr, bpc, false);

    return side_white ? (ws - bs) : (bs - ws);
  }

  // ── Move ordering ─────────────────────────────────────────────────────────
  void ComputeScores(const MoveList& ml, bool w, int ply, bool is_root,
                     const Move* tt_move, int scores[]) const {
    for (int i = 0; i < ml.count; ++i) {
      const Move& m = ml.moves[i];
      int s = 0;
      // TT move
      if (tt_move && SameMove(m, *tt_move)) { scores[i] = 10000000; continue; }
      // Root hint
      if (is_root && has_root_hint_ && SameMove(m, root_hint_))
        { scores[i] = 9000000; continue; }
      // Captures: MVV-LVA
      if (m.captured != EMPTY) {
        int victim  = PieceValue((char)std::toupper((unsigned char)m.captured));
        int attacker = PieceValue((char)std::toupper((unsigned char)board_[m.sr][m.sc]));
        scores[i] = 5000000 + victim * 16 - attacker;
        continue;
      }
      // Killer
      if (ply >= 0 && ply < MAX_PLY &&
          (SameMove(m, killers_[ply][0]) || SameMove(m, killers_[ply][1])))
        { scores[i] = 4000000; continue; }
      // History
      int side = w ? 0 : 1;
      scores[i] = history_[side][Sq(m.sr, m.sc)][Sq(m.dr, m.dc)];
    }
  }

  // Incremental selection: swap best remaining to position idx
  static void PickBest(MoveList& ml, int scores[], int idx) {
    int best = idx;
    for (int j = idx + 1; j < ml.count; ++j)
      if (scores[j] > scores[best]) best = j;
    if (best != idx) {
      std::swap(ml.moves[idx], ml.moves[best]);
      std::swap(scores[idx], scores[best]);
    }
  }

  // ── Killer & history updates ───────────────────────────────────────────────
  void StoreKiller(const Move& m, int ply) {
    if (ply < 0 || ply >= MAX_PLY || m.captured != EMPTY) return;
    if (!SameMove(m, killers_[ply][0])) {
      killers_[ply][1] = killers_[ply][0];
      killers_[ply][0] = m;
    }
  }

  void UpdateHistory(const Move& m, bool w, int depth) {
    if (m.captured != EMPTY) return;
    history_[w ? 0 : 1][Sq(m.sr, m.sc)][Sq(m.dr, m.dc)] += depth * depth;
  }

  // ── TT helpers ─────────────────────────────────────────────────────────────
  TTEntry* TTProbe(std::uint64_t key) {
    TTEntry& e = tt_[key & TT_MASK];
    return (e.key == key && e.depth >= 0) ? &e : nullptr;
  }

  void TTStore(std::uint64_t key, int score, int depth,
               TTFlag flag, const Move& best) {
    TTEntry& e = tt_[key & TT_MASK];
    if (e.key == key && e.depth > depth) return; // keep deeper
    e.key   = key;
    e.score = score;
    e.depth = (std::int16_t)depth;
    e.flag  = flag;
    e.from_sq = (std::uint8_t)Sq(best.sr, best.sc);
    e.to_sq   = (std::uint8_t)Sq(best.dr, best.dc);
  }

  Move TTMoveDecode(const TTEntry* e) const {
    Move m;
    m.sr = e->from_sq / BOARD_SIZE;
    m.sc = e->from_sq % BOARD_SIZE;
    m.dr = e->to_sq   / BOARD_SIZE;
    m.dc = e->to_sq   % BOARD_SIZE;
    m.captured = EMPTY;
    return m;
  }

  // ── Quiescence search ─────────────────────────────────────────────────────
  int Quiescence(int alpha, int beta, bool side_white) {
    CheckTime();

    if (!white_prince_ || !black_prince_)
      return Evaluate(side_white);

    int stand_pat = Evaluate(side_white);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    MoveList caps;
    GenerateCaptures(side_white, caps);

    int scores[MAX_MOVES];
    ComputeScores(caps, side_white, -1, false, nullptr, scores);

    constexpr int DELTA_MARGIN = 200;
    for (int i = 0; i < caps.count; ++i) {
      PickBest(caps, scores, i);
      const Move& m = caps.moves[i];
      // Delta pruning: skip if captured piece value + margin can't raise alpha
      if (m.captured != EMPTY) {
        char cu = (char)std::toupper((unsigned char)m.captured);
        if (cu != 'P' && stand_pat + PieceValue(cu) + DELTA_MARGIN <= alpha)
          continue;
      }
      MakeMove(m);
      int score = -Quiescence(-beta, -alpha, !side_white);
      UnmakeMove(m);
      if (score >= beta) return beta;
      if (score > alpha) alpha = score;
    }
    return alpha;
  }

  // ── Negamax with alpha-beta + TT + killers + history ──────────────────────
  int Negamax(int depth, int alpha, int beta, bool side_white, int ply,
              bool do_null = true) {
    CheckTime();

    // Terminal: prince captured
    if (!white_prince_ || !black_prince_)
      return Evaluate(side_white);

    // Leaf: quiescence
    if (depth <= 0)
      return Quiescence(alpha, beta, side_white);

    // TT probe
    std::uint64_t key = hash_;
    Move  tt_move{};
    Move* tt_move_ptr = nullptr;
    TTEntry* tte = TTProbe(key);
    if (tte) {
      if (tte->depth >= depth) {
        if (tte->flag == TT_EXACT) return tte->score;
        if (tte->flag == TT_ALPHA && tte->score <= alpha) return alpha;
        if (tte->flag == TT_BETA  && tte->score >= beta)  return beta;
      }
      tt_move = TTMoveDecode(tte);
      tt_move_ptr = &tt_move;
    }

    // Reverse futility pruning (static null move): if eval - margin >= beta
    // at low depth, the position is so good we can return beta immediately.
    if (depth <= 3 && beta > -WIN_SCORE + 1000 && beta < WIN_SCORE - 1000) {
      int rfp_eval = Evaluate(side_white);
      int rfp_margin = depth * 200;
      if (rfp_eval - rfp_margin >= beta) return beta;
    }

    // Null Move Pruning
    if (do_null && depth >= 3 && beta < WIN_SCORE - 1000) {
      int R = depth >= 6 ? 3 : 2;
      hash_ ^= zobrist_side;
      int null_score = -Negamax(depth - 1 - R, -beta, -beta + 1, !side_white,
                                ply + 1, false);
      hash_ ^= zobrist_side;
      if (null_score >= beta) return beta;
    }

    // Futility pruning: at low depths, if static eval is far below alpha,
    // quiet moves are unlikely to raise it enough.
    bool futile = false;
    if (depth <= 2 && alpha < WIN_SCORE - 1000 && alpha > -WIN_SCORE + 1000) {
      int static_eval = Evaluate(side_white);
      int margin = depth == 1 ? 300 : 600;
      if (static_eval + margin <= alpha) futile = true;
    }

    // Generate & order
    MoveList moves;
    GenerateAllMoves(side_white, moves);
    if (moves.count == 0) return Evaluate(side_white);

    int scores[MAX_MOVES];
    ComputeScores(moves, side_white, ply, false, tt_move_ptr, scores);

    Move best_move = moves.moves[0];
    int  best = -INF_SCORE;
    TTFlag flag = TT_ALPHA;

    for (int i = 0; i < moves.count; ++i) {
      PickBest(moves, scores, i);
      const Move& m = moves.moves[i];

      // Futility: skip quiet moves at low depth if position is hopeless
      if (futile && m.captured == EMPTY && i > 0) continue;

      MakeMove(m);
      int score;
      if (i == 0) {
        // First move: full window search
        score = -Negamax(depth - 1, -beta, -alpha, !side_white, ply + 1);
      } else {
        // LMR for late quiet moves
        bool do_lmr = (i >= 3 && depth >= 3 && m.captured == EMPTY);
        if (do_lmr) {
          int reduction = 1 + (i >= 6 ? 1 : 0);
          score = -Negamax(depth - 1 - reduction, -alpha - 1, -alpha,
                           !side_white, ply + 1);
        } else {
          // PVS: null window search
          score = -Negamax(depth - 1, -alpha - 1, -alpha, !side_white,
                           ply + 1);
        }
        // Re-search with full window if it beats alpha
        if (score > alpha && score < beta) {
          score = -Negamax(depth - 1, -beta, -alpha, !side_white, ply + 1);
        }
      }
      UnmakeMove(m);

      if (score > best) { best = score; best_move = m; }
      if (score > alpha) { alpha = score; flag = TT_EXACT; }
      if (alpha >= beta) {
        StoreKiller(m, ply);
        UpdateHistory(m, side_white, depth);
        flag = TT_BETA;
        break;
      }
    }

    TTStore(key, best, depth, flag, best_move);
    return best;
  }

  // ── Root search at a given depth ───────────────────────────────────────────
  bool SearchAtDepth(int depth, Move* out_move, int* out_score) {
    MoveList moves;
    GenerateAllMoves(my_is_white_, moves);
    if (moves.count == 0) return false;

    Move  tt_move{};
    Move* tt_ptr = nullptr;
    TTEntry* tte = TTProbe(hash_);
    if (tte) { tt_move = TTMoveDecode(tte); tt_ptr = &tt_move; }

    int scores[MAX_MOVES];
    ComputeScores(moves, my_is_white_, 0, true, tt_ptr, scores);

    int alpha = -INF_SCORE, beta = INF_SCORE;
    int local_best = -INF_SCORE;
    Move local_move = moves.moves[0];

    for (int i = 0; i < moves.count; ++i) {
      PickBest(moves, scores, i);
      CheckTime();
      const Move& m = moves.moves[i];

      MakeMove(m);
      int score = -Negamax(depth - 1, -beta, -alpha, !my_is_white_, 1);
      UnmakeMove(m);

      if (score > local_best) { local_best = score; local_move = m; }
      if (score > alpha) alpha = score;
    }

    *out_move  = local_move;
    *out_score = local_best;
    TTStore(hash_, local_best, depth, TT_EXACT, local_move);
    return true;
  }
};

}  // namespace

int main() {
  InitZobrist();
  InputState state{};
  if (!ReadInput("input.txt", &state)) {
    WriteOutput("output.txt", "a1 a1");
    return 0;
  }
  Agent agent(state);
  Move best{};
  if (!agent.SelectMove(&best)) {
    WriteOutput("output.txt", "");
    return 0;
  }
  WriteOutput("output.txt", MoveToString(best));
  return 0;
}
