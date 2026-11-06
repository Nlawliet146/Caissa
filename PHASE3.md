# Phase 3 baseline

Engine revision `617dc5b`, Linux x86-64, Intel Core i5-1035G1. Both modes
used the same binary. The checkpoint SHA-256 was
`bca6248aff979a3d215044b01a48199f1e5b53127a22af8cc00f02a9c78f883f`.
See [the README](README.md) for reproduction commands.

## Checkpoint validation

The supplied processed corpus yielded 36,345 validation positions across
400 games. Unmasked cross-entropy was **2.6369**, top-1 accuracy was
**30.67%**, and top-5 accuracy was **64.71%**. Accuracy here means agreement
with the recorded human move's from/to label. The partition is held back
from gradient updates under the checked-in training recipe, and its loss
also selects the checkpoint. The weights do not contain a corpus manifest
to independently establish training provenance.

## UCI benchmark

Four positions, three runs per position per mode and limit. Values are means.

| Limit | ML | Completed depth | Nodes at completed depth | Engine ms at completed depth | Wall ms to bestmove |
| --- | --- | ---: | ---: | ---: | ---: |
| Depth 5 | Off | 5.00 | 403,005 | 423.75 | 424.62 |
| Depth 5 | On | 5.00 | 411,969 | 439.08 | 439.75 |
| 200 ms | Off | 3.75 | 104,250 | 86.00 | 171.41 |
| 200 ms | On | 3.75 | 113,462 | 103.83 | 171.38 |

At fixed depth, ML searched 2.2% more nodes and took 3.6% longer on
average. At fixed time the mean completed depth was equal. The timed node
counts exclude work in the interrupted iteration, so their difference
cannot establish a throughput gain. The four positions are a small
diagnostic suite and individual results are in `build/phase3-benchmark.json`.

The current engine uses the model once per root search to order non-captures.
Capture scores occupy a higher band, and child searches receive an empty
prior vector. In alpha–beta, once a child establishes a lower bound at
least as large as beta, any additional child can only leave the maximizing
node's value unchanged or raise it. The caller already has enough
information to cut off. Ordering therefore changes how soon a cutoff can
occur and how many nodes are searched. Human-move prediction accuracy
does not measure how often the model puts a cutoff-producing move first.

## Match

Cute Chess CLI 1.5.1 ran 100 games at 60 ms per move, with one game at a
time and both color assignments for each of 50 openings. From ML's
perspective, the result was **32 wins, 33 draws, 35 losses**, or **48.5%**
of available points. The log verified successful priors on 6,004 ML
searches and ML disabled on 6,005 control searches.

All 67 decisive games ended in checkmate. The 33 draws comprised 24 by
threefold repetition, five by insufficient material, and four at the
100-full-move cap. There were zero time forfeits. The PGN and UCI log
are in `build/phase3-baseline.pgn` and `build/phase3-baseline.log`.

The match showed no strength gain for the current root ordering at this
time control on this suite. It gives a baseline for later changes. The
fast time control and selected openings limit how broadly the result
can be generalized.

For a sense of sampling variation across these openings, a percentile
bootstrap that resampled whole opening pairs (20,000 replicates, seed 3)
gave an approximate 95% interval of **40.0% to 56.5%** for ML's score.
Resampling a pair together preserves the dependence from playing the same
opening twice. The suite was selected deterministically, so this interval
does not establish coverage for a broader population of openings. Median
root inference time in the match log was 7 ms, with a 95th percentile of
9 ms, against the engine's 30 ms working budget.
