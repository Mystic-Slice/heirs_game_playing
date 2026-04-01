#!/usr/bin/env python3
"""
compete.py – Play two homework agents against each other.

Usage:
    python compete.py <dir1> <dir2> [--time SECONDS] [--engine ENGINE_PATH] [--max-moves N]

dir1 plays WHITE, dir2 plays BLACK.
Each directory must contain a compiled homework (homework.cpp -> homework, homework.java,
or homework.py).

The compiled engine binary must be accessible (defaults to ./engine in cwd, or specify
with --engine).
"""

import argparse
import os
# import resource
import shutil
import subprocess
import sys
import collections
import tempfile
import time

# ── Game Constants ─────────────────────────────────────────────────────────────
MAX_MOVES_WITHOUT_ACTION = 100  # 50 moves per player (100 ply) without capture/Baby

# ── ASNLIB paths ───────────────────────────────────────────────────────────────
_ASNLIB = os.environ.get("ASNLIB", "")
ASNLIB_ENGINE_PATH  = os.path.join(_ASNLIB, "public", "engine")  if _ASNLIB else "engine"
ASNLIB_CHECK_MOVES_PATH  = os.path.join(_ASNLIB, "public", "check_moves")  if _ASNLIB else "check_moves"
ASNLIB_RANDOM_PATH  = os.path.join(_ASNLIB, "public", "random")  if _ASNLIB else "random"
ASNLIB_MINIMAX_PATH = os.path.join(_ASNLIB, "public", "minimax") if _ASNLIB else "minimax"

# Built-in agent keywords that map to ASNLIB binaries
BUILTIN_AGENTS = {
    "random":  ASNLIB_RANDOM_PATH,
    "minimax": ASNLIB_MINIMAX_PATH,
}

# ── Initial board ──────────────────────────────────────────────────────────────
INITIAL_BOARD = """\
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
GYTSNXPNSTYG"""

INITIAL_TIME = 300.0
MAX_MOVES_DEFAULT = 1000 # NOTE: THERE IS NO MAX NUMBER OF MOVES IN THE REAL GAME


# ── Helpers ────────────────────────────────────────────────────────────────────

def setup_builtin_agent(keyword):
    """Create a temp working directory for a built-in agent ('random' or
    'minimax') whose binary lives in $ASNLIB/public/.  Returns (dir, cmd, desc)
    or exits on error."""
    binary = BUILTIN_AGENTS.get(keyword)
    if binary is None or not os.path.isfile(binary):
        sys.exit(f"[compete] Cannot find ASNLIB binary for '{keyword}'.\n"
                 f"  Set $ASNLIB or provide a real directory.\n"
                 f"  Looked for: {binary}")
    # Create a persistent temp dir (cleaned up at process exit)
    tmpdir = tempfile.mkdtemp(prefix=f"compete_{keyword}_")
    # Copy binary into tmpdir as "homework"
    dst = os.path.join(tmpdir, "homework")
    shutil.copy2(binary, dst)
    os.chmod(dst, 0o755)
    return tmpdir, [dst], f"built-in {keyword} ({binary})"


def find_agent(directory):
    """Locate the homework executable/script inside *directory*.
    Returns (cmd_list, description) or raises SystemExit."""
    d = os.path.abspath(directory)

    # If C++ source exists, always recompile to pick up changes
    cpp = os.path.join(d, "homework.cpp")
    if os.path.isfile(cpp):
        # Remove stale binaries first
        for name in ["homework", "homework.exe"]:
            p = os.path.join(d, name)
            if os.path.isfile(p):
                os.remove(p)
        binary = os.path.join(d, "homework")
        print(f"[compete] Compiling {cpp} …")
        rc = subprocess.call(["g++", "-std=c++17", "-O2", "-o", binary, cpp],
                             cwd=d)
        # On Windows, g++ produces homework.exe
        binary_exe = os.path.join(d, "homework.exe")
        if rc == 0 and (os.path.isfile(binary) or os.path.isfile(binary_exe)):
            actual = binary_exe if os.path.isfile(binary_exe) else binary
            return ([actual], f"C++ binary {actual}")
        else:
            sys.exit(f"[compete] Failed to compile {cpp}")

    # No source — check for pre-compiled binary
    for name in ["homework", "homework.exe"]:
        p = os.path.join(d, name)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return ([p], f"C++ binary {p}")

    # Java
    java_class = os.path.join(d, "homework.class")
    java_src = os.path.join(d, "homework.java")
    if os.path.isfile(java_class):
        return (["java", "-cp", d, "homework"], f"Java class {java_class}")
    elif os.path.isfile(java_src):
        print(f"[compete] Compiling {java_src} …")
        rc = subprocess.call(["javac", java_src], cwd=d)
        if rc == 0:
            return (["java", "-cp", d, "homework"], f"Java class in {d}")
        else:
            sys.exit(f"[compete] Failed to compile {java_src}")

    # Python
    py = os.path.join(d, "homework.py")
    if os.path.isfile(py):
        return ([sys.executable, py], f"Python script {py}")

    sys.exit(f"[compete] No homework agent found in {d}\n"
             f"  Expected one of: homework (binary), homework.java, homework.py")


def write_input(path, color, my_time, opp_time, board_lines, also_print):
    """Write an input.txt file at *path*."""
    with open(path, "w") as f:
        f.write(f"{color}\n")
        f.write(f"{my_time:.1f} {opp_time:.1f}\n")
        for line in board_lines:
            f.write(line + "\n")
    if also_print:
        print("---------- input.txt:")
        print(f"{color}")
        print(f"{my_time:.1f} {opp_time:.1f}")
        for line in board_lines:
            print(line + "")
        print("---------------------")
    
def flip_board(board_lines):
    """Flip the board so that the opponent sees it from their perspective.
    The board encoding already uses uppercase = WHITE, lowercase = BLACK,
    so we just need to reverse row order AND swap case? No – the engine
    stores the board in a fixed orientation.  We do NOT flip; we only swap
    the color header and remaining-time order."""
    return board_lines  # board stays as-is


def read_board_lines(path):
    """Read 12 board lines from a file (ignoring any header lines)."""
    with open(path, "r") as f:
        lines = [l.rstrip("\n") for l in f.readlines()]
    # Filter to lines that look like board rows (exactly 12 chars of valid chars)
    board = [l for l in lines if len(l) == 12 and all(c in ".BPNXYGSTbpnxygst" for c in l)]
    return board


def has_prince(board_lines, color):
    """Check whether the given color still has a Prince ('P' for WHITE,
    'p' for BLACK) on the board."""
    target = 'P' if color == "WHITE" else 'p'
    for line in board_lines:
        if target in line:
            return True
    return False


def run_agent(cmd, cwd, timeout):
    """Run the agent, return (cpu_user_seconds, wall_seconds, success).
    Uses resource.getrusage(RUSAGE_CHILDREN) to measure total user-mode CPU
    time across all cores, so parallel agents are fairly charged."""
    # ru_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    before = time.time()
    wall_start = time.monotonic()
    try:
        result = subprocess.run(
            cmd, cwd=cwd, timeout=timeout + 1,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        wall_elapsed = time.monotonic() - wall_start
        # ru_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        after = time.time()
        # cpu_user = ru_after.ru_utime - ru_before.ru_utime
        cpu_user = after - before  # Use wall clock for CPU time to be more robust to parallelism
        if result.returncode != 0:
            print(f"  [agent stderr] {result.stderr.decode(errors='replace').strip()}")
        return cpu_user, wall_elapsed, (result.returncode == 0)
    except subprocess.TimeoutExpired:
        wall_elapsed = time.monotonic() - wall_start
        # ru_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        after = time.time()
        # cpu_user = ru_after.ru_utime - ru_before.ru_utime
        cpu_user = after - before  # Use wall clock for CPU time to be more robust to parallelism
        return cpu_user, wall_elapsed, False


def run_engine(engine_path, work_dir):
    """Run the engine in *work_dir*.  Returns (success, message_or_board_lines).
    The engine reads input.txt + output.txt from cwd, writes next.txt."""
    # Copy engine to work_dir, preserving extension (.exe on Windows)
    _, ext = os.path.splitext(engine_path)
    engine_dest = os.path.join(work_dir, "engine" + ext)
    if os.path.abspath(engine_path) != os.path.abspath(engine_dest):
        shutil.copy2(engine_path, engine_dest)
        if sys.platform != "win32":
            os.chmod(engine_dest, 0o755)

    result = subprocess.run(
        [engine_dest], cwd=work_dir,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    next_path = os.path.join(work_dir, "next.txt")
    if not os.path.isfile(next_path):
        return False, "engine did not produce next.txt"

    with open(next_path, "r") as f:
        content = f.read()

    lines = content.strip().splitlines()
    # If it's an error message (not a 12-line board), report it
    if len(lines) != 12:
        return False, content.strip()

    # Verify each line is 12 chars (board)
    for line in lines:
        if len(line) != 12:
            return False, content.strip()

    return True, lines


def check_can_move(check_bin, input_path):
    """Run check_moves binary on input_path. Returns True if moves > 0."""
    # check_moves expects "input.txt" in cwd
    d = os.path.dirname(input_path)
    # Ensure input.txt exists there (it should)
    # Run the binary in that dir
    try:
        res = subprocess.run([check_bin], cwd=d, stdout=subprocess.PIPE, 
                           stderr=subprocess.PIPE, text=True, timeout=5)
        if res.returncode != 0:
            return True # Fallback: assume moves exist if checker fails
        count = int(res.stdout.strip())
        return count > 0
    except:
        return True # Fallback


def count_pieces(board_lines):
    """Return (white_count, black_count)."""
    w = sum(row.count(c) for row in board_lines for c in "BNPXYGTS")
    b = sum(row.count(c) for row in board_lines for c in "bnpxygts")
    return w, b


def resolve_draw(times, board_lines):
    """Resolve draw by Time then Pieces. Returns (winner_idx, reason).
    winner_idx: 0 (White), 1 (Black), or None (True Draw)."""
    if times[0] > times[1]:
        return 0, "Draw resolved by Time (White > Black)"
    elif times[1] > times[0]:
        return 1, "Draw resolved by Time (Black > White)"
    else:
        w, b = count_pieces(board_lines)
        if w > b:
            return 0, f"Draw resolved by Pieces (White {w} > Black {b})"
        elif b > w:
            return 1, f"Draw resolved by Pieces (Black {b} > White {w})"
        else:
            return None, "True Draw (Equal Time and Pieces)"


def get_state_key(board_lines, turn_color):
    """Return string representing board state for repetition check."""
    return f"{turn_color}\n" + "\n".join(board_lines)


def is_action(move_str, prev_board_lines):
    """Return True if move is a Capture or Baby move."""
    # prev_board_lines starts at rank 12 (index 0).
    # Move format: "a2 a3"
    try:
        parts = move_str.split()
        s1, s2 = parts[0], parts[1]
        
        col_map = {c: i for i, c in enumerate("abcdefghjkmn")}
        c1 = col_map[s1[0]]
        r1 = int(s1[1:]) - 1
        c2 = col_map[s2[0]]
        r2 = int(s2[1:]) - 1
        
        # In board_lines, index 0 is rank 12 (r=11).
        # So row_idx = 11 - r.
        row1 = 11 - r1
        row2 = 11 - r2
        
        piece = prev_board_lines[row1][c1]
        target = prev_board_lines[row2][c2]
        
        # Baby move?
        if piece in 'Bb':
            return True
            
        # Capture? (Target occupied)
        if target != '.':
            return True
            
        return False
    except:
        return False


def print_board(board_lines, move_num, player_label):
    """Pretty-print the board."""
    col_letters = "  a b c d e f g h j k m n"
    print(f"\n  Board after move {move_num} ({player_label}):")
    print(f"    {col_letters}")
    for i, row in enumerate(board_lines):
        rank = 12 - i  # top row is rank 12
        spaced = " ".join(row)
        print(f"  {rank:2d}  {spaced}")
    print(f"    {col_letters}")


# ── Main loop ─────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Play two homework agents against each other.")
    parser.add_argument("dir1", help="Directory for WHITE agent")
    parser.add_argument("dir2", help="Directory for BLACK agent")
    parser.add_argument("--time", type=float, default=INITIAL_TIME,
                        help=f"Total time per side in seconds (default {INITIAL_TIME})")
    parser.add_argument("--engine", default=None,
                        help="Path to compiled engine binary (default: ./engine)")
    parser.add_argument("--max-moves", type=int, default=MAX_MOVES_DEFAULT,
                        help=f"Max total moves before declaring a draw (default {MAX_MOVES_DEFAULT})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print board after every move")
    parser.add_argument("--swap", action="store_true",
                        help="Swap colors: dir1 plays BLACK, dir2 plays WHITE")
    args = parser.parse_args()

    # If --swap, flip the two directories so dir2 plays WHITE and dir1 plays BLACK
    if args.swap:
        args.dir1, args.dir2 = args.dir2, args.dir1

    # Locate engine
    engine_loc = args.engine
    if engine_loc is None:
        # Search: ASNLIB, cwd — try .exe variants on Windows
        candidates = ["./engine"]
        if sys.platform == "win32":
            candidates = ["./engine.exe"] + candidates
        if ASNLIB_ENGINE_PATH:
            candidates.insert(0, ASNLIB_ENGINE_PATH)
            if sys.platform == "win32":
                candidates.insert(0, ASNLIB_ENGINE_PATH + ".exe")
        for candidate in candidates:
            if os.path.isfile(candidate):
                engine_loc = os.path.abspath(candidate)
                break
    if engine_loc is None:
        sys.exit("[compete] Cannot find engine binary. "
                 "Set $ASNLIB or use --engine to specify its path.")
    engine_loc = os.path.abspath(engine_loc)
    print(f"[compete] Engine: {engine_loc}")

    # Resolve agent directories — handle 'random' / 'minimax' keywords
    tmp_dirs = []  # track temp dirs for cleanup
    agent_dirs = []
    agent_cmds = []
    agent_descs = []
    for label_idx, raw_dir in enumerate([args.dir1, args.dir2]):
        if raw_dir.lower() in BUILTIN_AGENTS:
            d, cmd, desc = setup_builtin_agent(raw_dir.lower())
            tmp_dirs.append(d)
            agent_dirs.append(d)
            agent_cmds.append(cmd)
            agent_descs.append(desc)
        else:
            d = os.path.abspath(raw_dir)
            cmd, desc = find_agent(d)
            agent_dirs.append(d)
            agent_cmds.append(cmd)
            agent_descs.append(desc)

    dir1 = agent_dirs[0]
    dir2 = agent_dirs[1]
    agent1_cmd = agent_cmds[0]
    agent2_cmd = agent_cmds[1]
    print(f"[compete] WHITE (dir1): {agent_descs[0]}")
    print(f"[compete] BLACK (dir2): {agent_descs[1]}")

    # Initialize times
    time_white = args.time
    time_black = args.time

    # Initialize board
    board_lines = INITIAL_BOARD.strip().splitlines()

    # Game state
    colors        = ["WHITE", "BLACK"]
    dirs          = [dir1, dir2]
    agent_cmds    = [agent1_cmd, agent2_cmd]
    agent_labels  = [f"WHITE ({os.path.basename(dir1)})",
                     f"BLACK ({os.path.basename(dir2)})"]
    times         = [time_white, time_black]  # index 0 = white, 1 = black
    move_count    = 0
    current       = 0  # 0 = WHITE's turn, 1 = BLACK's turn

    print(f"\n{'='*60}")
    print(f"  Game start — {args.time:.1f}s per side, max {args.max_moves} moves")
    print(f"{'='*60}")

    if args.verbose:
        print_board(board_lines, 0, "initial")

    # Game loop
    winner = None      # The directory string of the winner, or None for draw
    winner_label = None # For printing
    reason = ""
    
    history = collections.Counter()
    no_action_ply = 0
    
    # Record initial state
    history[get_state_key(board_lines, colors[0])] += 1

    while move_count < args.max_moves:
        current_color = colors[current]
        
        # ── Pre-move Draw Checks ─────────────────────
        
        # 1. 3-Fold Repetition (Input state is already recorded if this is start of turn)
        # Actually, we record state at end of turn.
        # But we need to check if *current* state has appeared 3 times.
        state_key = get_state_key(board_lines, current_color)
        # Note: we increment history AFTER move? No, standard is: if position about to occur...
        # But simpler: if current position has appeared 3 times.
        # We initialized history with start pos.
        if history[state_key] >= 3:
            # DRAW
            w_idx, reason = resolve_draw(times, board_lines)
            if w_idx is not None:
                winner = [args.dir1, args.dir2][w_idx]
                winner_label = agent_labels[w_idx]
                reason = "3-fold repetition -> " + reason
            else:
                reason = "3-fold repetition (True Draw)"
            break

        # 2. 50-Move Rule (100 ply)
        if no_action_ply >= MAX_MOVES_WITHOUT_ACTION:
            w_idx, reason = resolve_draw(times, board_lines)
            if w_idx is not None:
                winner = [args.dir1, args.dir2][w_idx]
                winner_label = agent_labels[w_idx]
                reason = "50-move rule -> " + reason
            else:
                reason = "50-move rule (True Draw)"
            break
            
        # 3. No Moves Available (Stalemate)
        # We need to write input.txt for the checker to verify moves
        d_current = dirs[current]
        # We can write input.txt to d_current (it's reused)
        input_path_check = os.path.join(d_current, "input.txt")
        # Write input.txt with CURRENT times (approximate is fine for valid moves check)
        write_input(input_path_check, current_color, times[current], times[1-current], board_lines, False)

        check_bin=ASNLIB_CHECK_MOVES_PATH
        # On Windows, try .exe variant
        if sys.platform == "win32" and check_bin and not os.path.isfile(check_bin):
            exe_variant = check_bin + ".exe"
            if os.path.isfile(exe_variant):
                check_bin = exe_variant
        if check_bin and not check_can_move(check_bin, input_path_check):
             # Draw by No Moves
             w_idx, reason = resolve_draw(times, board_lines)
             if w_idx is not None:
                 winner = [args.dir1, args.dir2][w_idx]
                 winner_label = agent_labels[w_idx]
                 reason = "Stalemate (No moves) -> " + reason
             else:
                 reason = "Stalemate (No moves) (True Draw)"
             break

        # ── Valid Move Execution ─────────────────────
        
        move_count += 1
        color = colors[current]
        opp   = 1 - current
        d     = dirs[current]
        cmd   = agent_cmds[current]
        label = agent_labels[current]
        
        # Identifiers as passed by user (for outcome file)
        current_id = [args.dir1, args.dir2][current]
        opp_id     = [args.dir1, args.dir2][opp]

        my_time  = times[current]
        opp_time = times[opp]

        if my_time <= 0:
            winner = opp_id
            winner_label = agent_labels[opp]
            reason = f"{label} ran out of time before move"
            break

        # 1) Write input.txt for current agent
        input_path = os.path.join(d, "input.txt")
        write_input(input_path, color, my_time, opp_time, board_lines, False)

        # Remove stale output.txt if present
        output_path = os.path.join(d, "output.txt")
        if os.path.exists(output_path):
            os.remove(output_path)

        # 2) Run agent, measure user CPU time
        print(f"\nMove {move_count}: {label} (CPU time left: {my_time:.2f}s)")
        cpu_time, wall_time, success = run_agent(cmd, d, my_time)
        print(f"  Agent finished in {cpu_time:.3f}s CPU, {wall_time:.3f}s wall")

        # Deduct user CPU time (fair across parallel agents)
        times[current] -= cpu_time

        if times[current] <= 0:
            winner = opp_id
            winner_label = agent_labels[opp]
            reason = f"{label} exceeded CPU time limit"
            break

        if not success:
            winner = opp_id
            winner_label = agent_labels[opp]
            reason = f"{label} agent crashed"
            break

        # Read agent's move
        if not os.path.isfile(output_path):
            winner = opp_id
            winner_label = agent_labels[opp]
            reason = f"{label} did not produce output.txt"
            break

        with open(output_path, "r") as f:
            move_str = f.read().strip()

        if not move_str:
            winner = opp_id
            winner_label = agent_labels[opp]
            reason = f"{label} produced empty output.txt (no valid moves?)"
            break

        print(f"  Move: {move_str}")

        # 3) Run engine to validate the move
        engine_ok, result = run_engine(engine_loc, d)

        if not engine_ok:
            print(f"  Engine rejected move: {result}")
            winner = opp_id
            winner_label = agent_labels[opp]
            reason = f"{label} made an invalid move ({result})"
            break

        # Check action (Capture/Baby) BEFORE updating board_lines
        # (We need to see if target was occupied in OLD board)
        action_flag = is_action(move_str, board_lines) # old board_lines
        if action_flag:
            no_action_ply = 0
        else:
            no_action_ply += 1

        # result is the new board_lines
        board_lines = result
        
        # Update history with NEW state
        # Next turn will be OPPONENT.
        next_color = colors[opp]
        history[get_state_key(board_lines, next_color)] += 1

        if args.verbose:
            print_board(board_lines, move_count, label)

        # 4) Check for game-over: Prince captured
        opp_color = colors[opp]
        if not has_prince(board_lines, opp_color):
            winner = current_id
            winner_label = label
            reason = f"{opp_color}'s Prince has been captured"
            break

        # 5) Switch turns
        current = opp

    # End of loop
    print(f"\n{'='*60}")
    # End of loop
    print(f"\n{'='*60}")
    
    # If no reason set, it means we ran out of moves (Max Moves)
    if not reason:
        w_idx, r = resolve_draw(times, board_lines)
        if w_idx is not None:
            winner = [args.dir1, args.dir2][w_idx]
            winner_label = agent_labels[w_idx]
            reason = f"Max moves ({args.max_moves}) -> " + r
        else:
            reason = f"Max moves ({args.max_moves}) (True Draw)"

    if winner:
        print(f"GAME OVER: {winner_label} wins!")
        print(f"Reason: {reason}")
    else:
        print(f"GAME OVER: Draw!")
        print(f"Reason: {reason}")
    
    print(f"Final CPU times: WHITE {times[0]:.2f}s, BLACK {times[1]:.2f}s")
    print(f"{'='*60}")

    # Write outcome file
    safe_d1 = os.path.basename(os.path.normpath(args.dir1))
    safe_d2 = os.path.basename(os.path.normpath(args.dir2))
    outcome_filename = f"outcome_{safe_d1}_{safe_d2}.txt"
    
    with open(outcome_filename, "w") as f:
        if winner == args.dir1:
            f.write(f"{args.dir1}\n{args.dir1}\n{args.dir1}\n")
        elif winner == args.dir2:
            f.write(f"{args.dir2}\n{args.dir2}\n{args.dir2}\n")
        else:
            # Draw: write both values once (assuming one per line)
            f.write(f"{args.dir1}\n{args.dir2}\n")
    
    print(f"[compete] Outcome written to {outcome_filename}")


if __name__ == "__main__":
    main()
