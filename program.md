# autoheirs

Experimentation to improve a game-playing AI agent for "Heirs".

## The game

Heirs is a two-player strategy board game on a 12x12 grid. Each side has 24 pieces. The goal is to **capture the opponent's Prince**. White moves first. There are 8 piece types:

| Piece | White/Black | Movement |
|-------|-------------|----------|
| Baby | B/b | 1-2 forward, no jump, captures straight forward |
| Prince | P/p | 1 step any direction (8 dirs). If captured, game over |
| Princess | X/x | 1-3 sliding any direction (8 dirs), no jump |
| Pony | Y/y | 1 diagonal step (4 dirs) |
| Guard | G/g | 1-2 sliding orthogonal (4 dirs), no jump |
| Tutor | T/t | 1-2 sliding diagonal (4 dirs), no jump |
| Scout | S/s | 1-3 forward + optional +/-1 sideways, CAN jump, captures at landing |
| Sibling | N/n | 1 step any (8 dirs), dest must be adjacent to >=1 friendly (excl self) |

Columns: `a b c d e f g h j k m n` (skip 'i' and 'l'). Uppercase = White, lowercase = Black, `.` = empty.

Initial board:
```
Row 12: g y t s n x p n s t y g   (Black back rank)
Row 11: b b b b b b b b b b b b   (Black babies)
Rows 10-3: empty
Row  2: B B B B B B B B B B B B   (White babies)
Row  1: G Y T S N X P N S T Y G   (White back rank)
```

Draw conditions (detected by game engine, not by your agent):
- No legal moves (stalemate)
- 3-fold repetition (same board + same side to move, 3 times)
- 50 consecutive moves without a capture or Baby move

Draw tiebreakers: most remaining time wins; if equal, most remaining pieces wins.

## Agent I/O

The agent reads `input.txt` and writes `output.txt` in its working directory:

**input.txt:**
```
WHITE
300 300
gytsnxpnstyg
bbbbbbbbbbbb
............
............
............
............
............
............
............
............
BBBBBBBBBBBB
GYTSNXPNSTYG
```
Line 1: color (`WHITE` or `BLACK`). Line 2: your remaining time, opponent's remaining time. Lines 3-14: 12x12 board (row 12 at top, row 1 at bottom).

**output.txt:**
```
c2 c4
```
Format: `<src_col><src_row> <dst_col><dst_row>`, using LF line endings.

## Compilation and time

- Compile: `g++ -std=c++17 -O2 -o homework homework.cpp`
- Time is measured via wall clock. Total budget per side: 300 seconds (likely). No multithreading benefit.
- If the agent exceeds its time budget, it loses immediately.

## Repository layout

```
algorithms/
  -- random/
        -- homework.cpp    <-- A sample random agent you can use as a baseline
  -- minimax/
        -- homework.cpp    <-- A sample minimax agent you can use as a baseline
  -- minimax_alphabetapruning_initial/
        -- homework.cpp    <-- The initial minimax agent with alpha-beta pruning. Provided here to test self play
agent_dev/
  -- homework.cpp        <-- THE FILE YOU MODIFY. This is your agent.
compete.py              <-- Game harness: runs two agents against each other
engine.cpp              <-- Move validator used by compete.py
check_moves.cpp         <-- Legal move counter used by compete.py
run_matches.py          <-- Batch runner for N games (You should mostly use this)
results.tsv             <-- Experiment log (you create and maintain this)
```

## Setup

If there is a `results.tsv` in the repo, read it to understand what experiments have already been done. You can continue from there. If not, start fresh.

To set up a new experiment, work with the user to:

1. **Agree on a run tag**: propose a tag based on today's date (e.g. `mar31`). The branch `autoheirs/<tag>` must not already exist.
2. **Create the branch**: `git checkout -b autoheirs/<tag>` from current HEAD.
3. **Read the in-scope files**: Read these for full context:
   - `agent_dev/homework.cpp` — the agent you will modify.
   - `compete.py` — the game harness (read-only, for understanding the game loop).
   - This file (`program.md`) — the rules and structure.
4. **Establish baseline**: Compile `agent_dev/homework.cpp`, run a match set against each opponent, and record baseline results.
5. **Initialize results.tsv**: Create `results.tsv` with just the header row.
6. **Confirm and go**: Confirm setup looks good.

Once you get confirmation, kick off the experimentation.

## Evaluation

The metric is **win rate** against opponent agents. You evaluate by running batch matches.

**Running a single game:**
```bash
python compete.py <dir1> <dir2> [-v]
```
`dir1` plays WHITE, `dir2` plays BLACK. The harness auto-compiles if `.cpp` source exists (deletes old binary, recompiles fresh). The `--swap` flag flips colors.

**Running a batch of N games:**
```bash
python run_matches.py algorithms/minimax . -n 10
```
This runs 10 games, alternating colors, and writes results to `outcome.txt`. It prints a summary like:
```
FINAL: 8W-1L-1D  (80.0% win rate)
```

**What to measure:**
- Primary: win rate against base minimax and random agents. They are available to you under `algorithms/`. Feel free to create more baselines using major checkpoints of your agent if needed to test against.
- Secondary: time remaining at end of wins (higher is better), since draws are resolved by remaining time.

**Extracting results from outcome.txt:**
```bash
tail -2 outcome.txt
```

## What you CAN do

- Modify `agent_dev/homework.cpp` — this is the **only file you edit**. Everything is fair game: search algorithm, evaluation function, move ordering, time management, data structures.
- Use `playdata.txt` for cross-move persistence within a game (the engine deletes it before each new game). Your agent can read/write this file between moves.
- Use `calibrate.cpp` / `calibration.txt` for CPU-speed-adaptive time management (the engine runs calibrate once before the game starts). But use this only if you really need it. 

## What you CANNOT do

- Modify `compete.py`, `engine.cpp`, `check_moves.cpp`, `run_matches.py`. These are read-only.
- Use external libraries beyond the C++ standard library. No Makefile, no CMakeLists.txt, no linker flags.
- Use multithreading (time is measured as total CPU time across all threads).
- Use network or GPU.

## The goal

**Maximize win rate against the basic agents namely minimax and random.** 

Ensure 100% win rate against the random agent.

Apart from these, you can create more baseline agents from your previous versions and try to beat them as well. The more you can beat, the better.

## Logging results

When an experiment is done, log it to `results.tsv` (tab-separated, NOT comma-separated). This file is not tracked by git — it is your local log of experiments.

The TSV has a header row and 6 columns:

```
commit	win_rate	wins	losses	draws	status	description
```

1. git commit hash (short, 7 chars)
2. win rate as percentage (e.g. 90.0)
3. wins count
4. losses count
5. draws count
6. status: `keep`, `discard`, or `crash`
7. short text description of what this experiment tried

Example:

```
commit	win_rate	wins	losses	draws	status	description
a1b2c3d	85.0	17	2	1	keep	baseline
b2c3d4e	95.0	19	1	0	keep	add quiescence search
c3d4e5f	80.0	16	3	1	discard	aggressive LMR (loses tactical sharpness)
d4e5f6g	0.0	0	0	0	crash	OOM from 4M entry TT
```

## The experiment loop

The experiment runs on a dedicated branch (e.g. `autoheirs/mar31`).

LOOP FOREVER:

1. Look at the current git state and results so far.
2. Choose an experimental idea. Modify `agent_dev/homework.cpp` with the change.
3. Compile to verify it builds: `g++ -std=c++17 -O2 -o homework homework.cpp`
4. If compilation fails, fix the error and try again. If you can't fix it after a few attempts, give up on this idea.
5. git commit the change.
6. Run the evaluation: `python run_matches.py agent_dev algorithms/minimax . -n 10 > run.log 2>&1`. Feel free to use fewer matches for quick iteration, but in major checkpoints, ensure you have enough to get a reliable signal (e.g. 10). Note: The `run_matches.py` script assumes the first agent is your agent and the second is the opponent, so the results are from your agent's perspective. Run only one evaluation at a time.
7. Read the results: `tail -3 run.log`
8. If the tail output doesn't contain a FINAL summary line, the run crashed or timed out. Run `tail -n 50 run.log` to diagnose. Attempt a fix if trivial.
9. Record the results in `results.tsv` (do NOT commit results.tsv — leave it untracked).
10. If win rate improved (higher) or is equal with fewer losses, **keep** — advance the branch.
11. If win rate is worse, **discard** — `git reset --hard HEAD~1` to revert.

**Crashes**: If the agent crashes during a game (compilation error, runtime crash, illegal move), use your judgment. Typo or missing import? Fix and re-run. Fundamentally broken idea? Log as crash, revert, move on.

**NEVER STOP**: Once the experiment loop has begun, do NOT pause to ask the human if you should continue. The loop runs until the human interrupts you. If you run out of ideas, think harder — re-read the code for new angles, try combining previous near-misses, try more radical changes.

For any more details, refer "hw2-csci561-sp26.pdf"