# Phase 4 search experiments

Both UCI switches are opt-in and require `UseML=true`. The control for
each experiment is the existing ML root ordering. See the README for
reproduction commands. The tests used the shipped checkpoint on the
same Intel Core i5-1035G1 machine as Phase 3.

## Root late move reductions

Starting at depth 4, the engine considers moves in the latter half of
root order after at least the first three moves. A move is eligible if
its prior is positive and below the uniform share of legal moves.
Captures, promotions, castling, checking moves, moves out of check,
and probes when alpha already has a mate score are excluded.

For integer scores, improving the current root alpha requires a score
of at least `alpha + 1`. By negamax, the reduced child needs to score
at most `-alpha - 1`, so the child probe uses the window
`[-alpha - 1, -alpha]`. If the reduced result beats root alpha, the
engine repeats the search at full depth with the regular root window.
Only that full-depth result can replace the best move. A reduced
fail-low gives no bound on what a deeper search would find. Skipping
the full search in that case is the heuristic and can miss a tactic.

## Ply-one inference

At a non-leaf ply-one position that needs move generation after the
transposition-table probe, Caissa evaluates the policy and caches the
priors by position hash for the current root search. Later iterations
can reuse them. The existing move-ordering priorities still apply.
Calls are charged to the move clock and the deadline is checked before
and after inference. An in-flight Python call cannot be interrupted
by the search thread.

## Benchmark

Four positions, three repeats per limit and mode. Each experiment ran
with its own interleaved control. Timed nodes are from the last
completed iteration.

| Experiment | Depth-5 nodes, control → experiment | Depth-5 wall ms | Mean depth at 200 ms |
| --- | ---: | ---: | ---: |
| Root LMR | 411,969 → 383,093 | 406.53 → 383.83 | 3.75 → 4.00 |
| Ply one | 411,969 → 407,789 | 394.84 → 583.77 | 3.92 → 2.83 |

Root LMR averaged 20.50 reductions and 1.75 full re-searches per
depth-5 search. It used 7.0% fewer nodes and 5.6% less wall time.
Ply-one ordering saved about 1% of nodes but spent 181.55 ms on
22 usable inference calls per depth-5 search. At 200 ms it spent
94.73 ms on 12 calls on average. The engine reserves 30 ms from
the 200 ms UCI limit. All four benchmark positions returned the
same best move in every paired comparison.

Raw benchmark records are in `build/phase4-lmr.json` and
`build/phase4-ply-one.json`.

## Root LMR match

In 100 games at 60 ms per move, with 50 color-swapped opening pairs,
root LMR scored **57.0%** against root ordering alone (44 wins, 26 draws,
30 losses). The log verified 72,227 reduced probes and 633 full re-searches.
All 74 decisive games ended in mate. The draws included 18 by repetition,
two by insufficient material, and six at the 100-full-move cap. There
were no time forfeits. The PGN and UCI log are in `build/phase4-lmr.pgn`
and `build/phase4-lmr.log`.

Resampling the 50 opening pairs together gave a descriptive 95% bootstrap
interval of 48.5% to 65.0% (20,000 replicates, seed 3). The result favors
LMR on this run, but the interval includes an even score and the selected
openings and short clock limit generalization.
