# Plan: Heirs Game Agent (Minimax with Alpha-Beta Pruning) — C++

## Context
HW2 for CSCI-561 (FoAI). Build an agent for "Heirs", a 12x12 chess-like board game. The agent reads `input.txt`, computes a move using minimax, and writes `output.txt`. Must beat a random agent and a simple minimax agent (no alpha-beta, adaptive depth 1-3). C++ chosen for speed — allows deeper search.

**Compilation** (from compete.py): `g++ -std=c++17 -O2 -o homework homework.cpp`
**Time measurement**: Wall clock (`time.time()`) — not Unix `time` CPU. No multi-threading benefit.
**Initial time**: 300.0 seconds per side (default).

## File to Create
`D:\USCrelated\Sem-4\FoAI\HW\HW2\homework.cpp` — single C++ file (C++17).

## Initial Board Layout
```
Row 12: g y t s n x p n s t y g   (Black back rank)
Row 11: b b b b b b b b b b b b   (Black babies)
Rows 10-3: empty (.)
Row  2: B B B B B B B B B B B B   (White babies)
Row  1: G Y T S N X P N S T Y G   (White back rank)
Cols:   a b c d e f g h j k m n   (skip 'i' and 'l')
```

## Architecture (5 sections within the file)

### 1. Board Representation & I/O
- Parse `input.txt`: player color (WHITE/BLACK), time remaining (two doubles), 12x12 board grid
- Board: `char board[12][12]` — row index 0 = row 12 on display (top), index 11 = row 1 (bottom)
- Column mapping: `a,b,c,d,e,f,g,h,j,k,m,n` → indices 0-11
- Helper: `int colToIndex(char c)` and `char indexToCol(int i)`
- Output move as `"<src_col><src_row> <dst_col><dst_row>\n"` to `output.txt`
- Valid piece chars: `.BPNXYGSTbpnxygst` (N=Sibling, S=Scout)

### 2. Move Generation
A move: `struct Move { int sr, sc, dr, dc; char captured; }` (source row/col, dest row/col, captured piece for unmake)

| Piece | Char | Movement Rules |
|-------|------|----------------|
| Baby (B/b) | 1-2 forward (White=row index decreasing, Black=row index increasing), no jump, captures straight forward |
| Prince (P/p) | 1 step any direction (8 dirs) |
| Princess (X/x) | 1-3 steps any direction (8 dirs), sliding (blocked by any piece in path) |
| Pony (Y/y) | 1 diagonal step (4 dirs) |
| Guard (G/g) | 1-2 orthogonal (4 dirs), sliding |
| Tutor (T/t) | 1-2 diagonal (4 dirs), sliding |
| Scout (S/s) | 1-3 forward + optional ±1 sideways, CAN jump over pieces, captures only at landing |
| Sibling (N/n) | 1 step any direction (8 dirs), destination must be adjacent to ≥1 friendly piece (not counting self) |

**"Sliding" = blocked by any piece. Can capture first enemy in path but stop there.**

Key:
- White uppercase (`isupper`), Black lowercase (`islower`)
- Cannot move onto friendly piece
- Baby captures straight forward (same as movement direction)

### 3. Evaluation Function
Score from our player's perspective minus opponent's:

- **Material**: Prince=100000, Princess=900, Scout=500, Guard=450, Tutor=400, Sibling=350, Pony=300, Baby=100
- **Positional**: Babies bonus for advancing; Prince bonus for safety (friendly neighbors); center control bonus for major pieces
- **Game over**: If a prince is missing → return ±INF

### 4. Minimax with Alpha-Beta Pruning
- Standard negamax-style or min/max with alpha-beta
- Make/unmake move (save captured piece, restore)
- Move ordering: MVV-LVA (captures first, high-value victim prioritized)
- Check time periodically (every ~1000 nodes) via `chrono::steady_clock`
- If time exceeded, throw/return immediately with best move found so far

### 5. Time Management & Iterative Deepening
- `time_for_move = remaining_time / 40` (estimate ~40 moves per game)
- Minimum: 0.05s; cap at ~5s per move
- **Iterative deepening**: depth 1, 2, 3... until time budget exhausted
  - Store best move from each completed depth
  - Abort mid-depth → use last completed depth's best move
- Very low time (<0.5s): depth-1 only or first legal move

## Implementation Steps
1. Scaffolding: main(), file I/O, board parsing, output formatting
2. Column/row conversion helpers
3. Move struct and move generation for all 8 piece types
4. Make/unmake move
5. Evaluation function
6. Minimax with alpha-beta pruning
7. Iterative deepening + time management
8. Move ordering (MVV-LVA)
9. Test with opening position input.txt

## Verification
- Compile: `g++ -std=c++17 -O2 -o homework homework.cpp`
- Create `input.txt` with opening position, run `./homework`, check `output.txt` format
- Test both WHITE and BLACK perspectives
- Verify Scout jumping, Sibling adjacency constraint, Baby forward-only
