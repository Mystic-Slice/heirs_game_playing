# Heirs Agent Code Design

This document describes the structure and design decisions for `homework.cpp`.

## High-Level Structure

`homework.cpp` is a single-file implementation with five logical sections:
1. Board representation and I/O
2. Move generation
3. Evaluation
4. Alpha-beta search
5. Time management and iterative deepening

## Core Data Types

- `InputState`: parsed data from `input.txt` (side to move, time controls, board)
- `Move`: move payload used across generation and search (`sr`, `sc`, `dr`, `dc`, `captured`)
- `Agent`: encapsulates board state, evaluation, and search pipeline

## Board and Coordinate Decisions

- Board stored as 12x12 `char` array
- Row 0 is rank 12 (top), row 11 is rank 1 (bottom)
- Columns are `abcdefghjkmn` (skip `i` and `l`)
- Output move format: `<src_col><src_row> <dst_col><dst_row>`

## Search Design

- Negamax with alpha-beta pruning
- Iterative deepening until time budget is exhausted
- Periodic time checks (every 1024 nodes)
- Root fallback move retained in case deeper search is interrupted

## Evaluation Design

- Material values:
  - Prince 100000, Princess 900, Scout 500, Guard 450, Tutor 400, Sibling 350, Pony 300, Baby 100
- Positional terms:
  - Baby advancement bonus
  - Prince safety via adjacent friendly count
  - Center bonus for non-Baby, non-Prince pieces
- Terminal handling:
  - Missing opponent prince -> large positive score
  - Missing own prince -> large negative score

## Move Generation Coverage

- Baby: 1-2 forward, no jumping, forward capture
- Prince: 1 step in 8 directions
- Princess: sliding up to 3 in 8 directions
- Pony: 1 diagonal
- Guard: sliding 1-2 orthogonal
- Tutor: sliding 1-2 diagonal
- Scout: 1-3 forward plus optional sideways offset, can jump blockers
- Sibling: king-step move with adjacent-friendly destination constraint
