# Profiling Notes

## Tool Availability

The installed MinGW `gprof` produced `gmon.out` but accumulated no samples inside the `std::jthread` worker, so its empty report is not used as optimization evidence. Windows Performance Recorder exposes a CPU profile, but collection was rejected by system profiling policy with error `0xc5585011`; no trace session remained active. No supported user-mode sampling collector is installed.

These limitations mean the current evidence is process-level, not a function-level CPU profile. Parser, hash, and I/O percentages must not be inferred from it individually.

## CPU Saturation Experiment

A warm-cache 1B run was measured with Windows process `TotalProcessorTime`. Average occupied logical cores equal CPU seconds divided by wall seconds.

| Threads | Wall seconds | CPU seconds | Average logical cores | Share of 24 logical processors |
|---:|---:|---:|---:|---:|
| 8 | 7.15 | 51.91 | 7.26 | 30.2% |
| 16 | 4.50 | 63.19 | 14.03 | 58.5% |
| 24 | 3.51 | 71.83 | 20.46 | 85.2% |
| 32 | 3.26 | 68.05 | 20.85 | 86.9% |

This supports a limited inference: execution approaches CPU/scheduler saturation by 24 threads, and raising the count to 32 adds almost no CPU occupancy. It does not prove whether parsing or hash lookup dominates. The next safe step is an isolated, byte-identical parser/hash experiment rather than more threads or a speculative SIMD rewrite.

## Fixed-Branch Parser Experiment

An isolated target replaced the existing validated integer parser with direct branches for the four legal temperature lengths. It passed 81 correctness tests and raw-output comparison, but did not produce a stable improvement:

- 1B rows, 32 threads, seven interleaved runs: median improvement 1.65%, with the fast parser winning 5 of 7 pairs.
- 100M rows, one thread, nine interleaved runs: median improvement 0.33%, with the fast parser winning 6 of 9 pairs.

Both differences are within observed run-to-run variation. The experiment was removed rather than adding permanent parser complexity. This narrows the next investigation toward hash lookup or broader record scanning, while still requiring an isolated benchmark before any implementation is retained.
