# Plan: Heirs Game Agent (Minimax with Alpha-Beta Pruning) — C++

## Context
HW2 for CSCI-561 (FoAI). Build an agent for "Heirs", a 12x12 chess-like board game. The agent reads `input.txt`, computes a move using minimax, and writes `output.txt`. Must beat a random agent and a simple minimax agent (no alpha-beta, adaptive depth 1-3). C++ chosen for speed — allows deeper search.

**Compilation** (from compete.py): `g++ -std=c++17 -O2 -o homework homework.cpp`
**Time measurement**: Wall clock (`time.time()`) — not Unix `time` CPU. No multi-threading benefit.
**Initial time**: 300.0 seconds per side (default).

## Project Layout
- `agent_dev/homework.cpp` — active development version (dir1, WHITE by default)
- `agent_baseline/homework.cpp` — frozen baseline to test against (dir2, BLACK by default)
- `compete.py` — game harness, takes two dirs, manages game loop, validates via engine
  - `--swap` flag to flip colors
  - Auto-recompiles if `.cpp` source exists (deletes old binary first)
- `engine.cpp` / `check_moves.cpp` — move validator + legal move counter for compete.py
- `run_matches.py` — batch runner for N games, alternating colors, writes `outcome.txt`
  - Resolves `$ASNLIB/public/compete.py` automatically, or `--compete` flag

## Persistent Files (per-game)
- **`playdata.txt`** — deleted at game start by engine. Agent can read/write between moves.
  - Useful for: position history (repetition avoidance), opening book data
  - Format used: one Zobrist hash (uint64_t decimal) per line
- **`calibrate.cpp`** → `calibration.txt` — run once before grading to benchmark CPU speed.
  - Agent can read `calibration.txt` to adjust search depth/time allocation.

## Initial Board Layout
```
Row 12: g y t s n x p n s t y g   (Black back rank)
Row 11: b b b b b b b b b b b b   (Black babies)
Rows 10-3: empty (.)
Row  2: B B B B B B B B B B B B   (White babies)
Row  1: G Y T S N X P N S T Y G   (White back rank)
Cols:   a b c d e f g h j k m n   (skip 'i' and 'l')
```

## Architecture (current agent_dev implementation)

### Board Representation & I/O
- Board: `char board[12][12]` — row 0 = rank 12 (top), row 11 = rank 1 (bottom)
- Column mapping: `a,b,c,d,e,f,g,h,j,k,m,n` → indices 0-11
- Output: `"<src_col><src_rank> <dst_col><dst_rank>\n"` to `output.txt`

### Move Generation
Uses stack-allocated `MoveList` (fixed array of 256 moves, no heap allocation).

| Piece | Char | Movement Rules |
|-------|------|----------------|
| Baby (B/b) | 1-2 forward, no jump, captures straight forward (NOT diagonal) |
| Prince (P/p) | 1 step any direction (8 dirs) |
| Princess (X/x) | 1-3 sliding any direction (8 dirs) |
| Pony (Y/y) | 1 diagonal step (4 dirs) |
| Guard (G/g) | 1-2 sliding orthogonal (4 dirs) |
| Tutor (T/t) | 1-2 sliding diagonal (4 dirs) |
| Scout (S/s) | 1-3 forward + optional ±1 sideways, CAN jump, captures at landing |
| Sibling (N/n) | 1 step any (8 dirs), dest must be adjacent to ≥1 friendly (excl self) |

### Evaluation Function
- **Material**: Prince=100000, Princess=900, Scout=500, Guard=450, Tutor=400, Sibling=350, Pony=300, Baby=100
- **Baby advancement**: `advance * 8` (white: `10 - r`, black: `r - 1`)
- **Center bonus**: `max(0, 6 - manhattan_dist_to_5_5) * 4` for non-Baby, non-Prince
- **Prince safety**: `friendly_neighbors * 18`
- **Terminal**: Missing prince → ±WIN_SCORE (100M)

### Search
- **Negamax** with alpha-beta pruning
- **Iterative deepening** (depth 1..64), time check before each new depth
- **Quiescence search** — captures-only at depth 0 with stand-pat pruning
- **Transposition table** — Zobrist hashing, 1M entries, stores score/depth/flag/best-move
  - Compact TTEntry: key(8) + score(4) + depth(2) + flag(1) + from_sq(1) + to_sq(1) bytes
- **Move ordering** (pre-computed scores, incremental selection sort):
  1. TT move (10M)
  2. Root hint from previous depth (9M)
  3. Captures: MVV-LVA (5M + victim*16 - attacker)
  4. Killer moves — 2 per ply, quiet moves only (4M)
  5. History heuristic — `history[side][from_sq][to_sq]`, incremented by `depth²` on cutoffs
- **Prince tracking** — `white_prince_`/`black_prince_` bools updated incrementally in make/unmake (no board scan)
- **Time check** — every 1024 nodes via `steady_clock`, throws `TimeUpException`

### Time Management
- `time_for_move = min(remaining / 40, CAP)` — CAP currently set to 0.1s
- Floor: 0.05s. Low-time fallback (<0.5s): return first ordered move, no search.
- Time check before starting each new iterative deepening depth.

## Known Issues & Attempted Fixes

### 3-Fold Repetition Draws
**Problem**: Agent repeats the same move from the same position, opponent repeats response, causing draw by 3-fold repetition.

**Attempted approach** (reverted — didn't work well):
- Store all position hashes (Zobrist) in `playdata.txt` across moves
- On each turn: read history, add current hash, search with repetition-aware scoring
- In Negamax/Quiescence: if `CountRepetitions(hash_) >= 2`, return 0 (draw)
- After choosing move: apply move, add resulting hash, write back to playdata.txt

**Issues with this approach**:
- Linear scan of game_history_ at every search node (O(n) per node, n = game length)
- Returning 0 for repeated positions may be too aggressive — agent avoids perfectly good moves
- The search already has TT which caches scores; repetition-return conflicts with TT entries
- May need a more nuanced approach: only penalize at root, or use contempt factor

**Ideas to revisit**:
- Root-only repetition avoidance: only penalize at root, not deep in search
- Track specific move played from each position, penalize only THAT move
- Use playdata.txt to store (hash, move_played) pairs instead of just hashes
- Small negative score instead of 0 for repetitions (contempt)
- Only check reversible moves (non-captures, non-baby) for repetition

## Future Improvements
- Better evaluation: mobility, piece-square tables, threat detection
- Null move pruning
- Late move reductions (LMR)
- Aspiration windows in iterative deepening
- Opening book / endgame logic
- `calibrate.cpp` for CPU-adaptive time management

## Verification
- Compile: `g++ -std=c++17 -O2 -o homework homework.cpp`
- Test: `python run_matches.py agent_baseline agent_dev -n 10`
- Swap colors: `python compete.py agent_dev agent_baseline --swap -v`
- Verify: Scout jumping, Sibling adjacency, Baby forward-only capture
