# Development Plan

## Priorities

Development decisions follow this order:

1. Correctness: valid inputs always produce canonical byte-identical output.
2. Stability: memory use is bounded, failures are diagnosed, and resources are released by RAII.
3. Performance: an optimization is retained only after the first two requirements pass.

Input size must not determine heap size. Implementations may stream or map the input file, but must not materialize the complete dataset in heap memory.

## Completed Milestones

- Portable baseline with deterministic fixtures and CTest.
- Fixed-format integer parsing.
- Allocation-free hot-loop lookup for known stations.
- Single-threaded comparison of `getline`, 4 MiB buffered I/O, and Windows file mapping.
- Reproducible 10M and 100M warm-cache benchmarks.
- Complete 1B validation: byte-identical output against the portable baseline with 5.05 MiB peak committed memory.
- Bounded parallel aggregation with newline-safe ranges, fixed 256 KiB worker buffers, private maps, and a single final merge.
- Parallel 1B validation at 24 threads: byte-identical output, 14.52 MiB peak working set, and 10.58 MiB peak committed memory.

## Current: Memory Resilience

Completed stability controls:

- The production buffer is fixed at 4 MiB and the input is never materialized in heap memory.
- The station map reserves the documented 10,000-key bound before processing.
- A 10,001st unique station is rejected before any further map allocation.
- Output sorting stores pointers to map entries instead of copying names and aggregates.
- `std::bad_alloc` produces a stable diagnostic and exit code 3.
- Windows Job Object tests cover success and deterministic allocation failure under a 32 MiB process limit.
- The Job runner owns Windows handles through move-only RAII, checks API failures, and enforces a 10-minute timeout.
- `scripts/measure-memory.ps1` records OS-maintained peak working set, committed/pagefile usage, and virtual bytes for the child solution process.

Remaining validation work:

- Run AddressSanitizer and UndefinedBehaviorSanitizer with a toolchain that provides their runtimes; the installed Strawberry MinGW distribution does not include `libasan` or `libubsan`.

Acceptance requires no uncaught allocation failures, no input-sized heap allocation, byte-identical output, and documented peak-memory measurements.

## Current: Parallel Scaling

The parallel target caps execution at 32 threads. Its resident working set is bounded by fixed per-thread buffers and station tables rather than input size. CTest exercises explicit thread counts, partition boundaries, propagated worker failures, the global station limit, and execution under a 64 MiB Job limit.

Next milestones:

1. Repeat scaling measurements and identify the saturation point under controlled system load.
2. Evaluate a custom hash table, branch reduction, or SIMD only when profiling identifies the relevant bottleneck.
3. Produce the final architecture explanation, limitations, and résumé-ready results.

Every milestone is committed separately and must include correctness tests, stability evidence, and same-machine before/after measurements.
