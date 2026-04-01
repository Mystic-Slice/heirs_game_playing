#!/usr/bin/env python3
"""
Run 100 matches between two agents, alternating colors.
Usage: python run_matches.py <minimax_dir> <my_agent_dir>

Results written to outcome.txt as WIN/LOSE/DRAW (from my_agent's perspective).
"""

import argparse
import os
import subprocess
import sys


def read_outcome(outcome_path, my_agent_dir):
    """Read outcome file and return WIN, LOSE, or DRAW from my_agent's perspective."""
    with open(outcome_path, "r") as f:
        lines = [l.strip() for l in f.readlines() if l.strip()]

    if len(lines) == 3 and lines[0] == lines[1] == lines[2]:
        # Clear winner
        winner = lines[0]
        if os.path.basename(os.path.normpath(winner)) == os.path.basename(os.path.normpath(my_agent_dir)):
            return "WIN"
        else:
            return "LOSE"
    else:
        return "DRAW"


def main():
    parser = argparse.ArgumentParser(description="Run 100 matches between two agents.")
    parser.add_argument("my_agent", help="Directory for your agent")
    parser.add_argument("minimax", help="Directory for the minimax agent")
    parser.add_argument("-n", type=int, default=100, help="Number of matches (default 100)")
    parser.add_argument("--compete", default=None,
                        help="Path to compete.py (default: $ASNLIB/public/compete.py, else ./compete.py)")
    args = parser.parse_args()

    # Resolve compete.py path
    compete_path = args.compete
    if compete_path is None:
        asnlib = os.environ.get("ASNLIB", "")
        if asnlib:
            compete_path = os.path.join(asnlib, "public", "compete.py")
        else:
            compete_path = "compete.py"
    if not os.path.isfile(compete_path):
        sys.exit(f"Cannot find compete.py at: {compete_path}")

    minimax_dir = args.minimax
    my_agent_dir = args.my_agent
    num_matches = args.n

    results = []
    wins = losses = draws = 0

    for i in range(num_matches):
        game_num = i + 1

        # Alternate colors: even games my_agent is WHITE, odd games my_agent is BLACK
        if i % 2 == 0:
            dir1, dir2 = my_agent_dir, minimax_dir
            my_color = "WHITE"
        else:
            dir1, dir2 = minimax_dir, my_agent_dir
            my_color = "BLACK"

        safe_d1 = os.path.basename(os.path.normpath(dir1))
        safe_d2 = os.path.basename(os.path.normpath(dir2))
        outcome_filename = f"outcome_{safe_d1}_{safe_d2}.txt"

        # Remove stale outcome file
        if os.path.exists(outcome_filename):
            os.remove(outcome_filename)

        print(f"--- Game {game_num}/{num_matches} | my_agent={my_color} ---")

        rc = subprocess.call([sys.executable, compete_path, dir1, dir2])

        if not os.path.exists(outcome_filename):
            print(f"  WARNING: {outcome_filename} not found, recording as DRAW")
            result = "DRAW"
        else:
            result = read_outcome(outcome_filename, my_agent_dir)

        results.append(result)
        if result == "WIN":
            wins += 1
        elif result == "LOSE":
            losses += 1
        else:
            draws += 1

        print(f"  Result: {result}  (Running: {wins}W-{losses}L-{draws}D)\n")

    # Write results
    with open("outcome.txt", "w") as f:
        for idx, r in enumerate(results):
            color = "WHITE" if idx % 2 == 0 else "BLACK"
            f.write(f"Game {idx+1} ({color}): {r}\n")
        f.write(f"\nSummary: {wins}W-{losses}L-{draws}D out of {num_matches}\n")
        f.write(f"Win rate: {wins/num_matches*100:.1f}%\n")

    print(f"{'='*60}")
    print(f"FINAL: {wins}W-{losses}L-{draws}D  ({wins/num_matches*100:.1f}% win rate)")
    print(f"Results written to outcome.txt")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
